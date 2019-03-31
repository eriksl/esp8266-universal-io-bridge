#include "lwip-interface.h"

#include <user_interface.h>

#include "attribute.h"
#include "util.h"
#include "stats.h"

/* don't bail on wrongly declared functions in old version of lwip, some parameters should be const really */
#pragma GCC diagnostic ignored "-Wdiscarded-qualifiers"
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wpacked"
#pragma GCC diagnostic push

#include <lwip/udp.h>
#include <lwip/tcp.h>
#include <lwip/igmp.h>

#pragma GCC diagnostic pop

enum
{
	lwip_ethernet_max_payload =	1500,
	lwip_ip_header_size =		20,
	lwip_udp_header_size = 		8,
	lwip_tcp_header_size =		20,
	lwip_udp_max_payload =		lwip_ethernet_max_payload - lwip_ip_header_size - lwip_udp_header_size,
	lwip_tcp_max_payload =		lwip_ethernet_max_payload - lwip_ip_header_size - lwip_tcp_header_size,
};

static const char * const lwip_error_strings[] roflash =
{
	"ok",
	"out of memory",
	"buffer error",
	"timeout",
	"routing error",
	"in progress",
	"illegal value",
	"operation would block",
	"address in use",
	"already connecting",
	"already connected",
	"not connected",
	"low level netif error",
	"connection aborted",
	"connection reset",
	"connection closed",
	"illegal argument",
	"unknown",
};

static void log_error(err_t error)
{
	int ix = error;

	if(ix < 0)
		ix = 0 - ix;

	if(ix > 17)
		ix = 17;

	log("error: ");
	log_from_flash(lwip_error_strings[ix]);
	log("\n");
}

_Bool attr_nonnull attr_pure lwip_if_received_tcp(lwip_if_socket_t *socket)
{
	return(socket->peer.port == 0);
}

_Bool attr_nonnull attr_pure lwip_if_received_udp(lwip_if_socket_t *socket)
{
	return(socket->peer.port != 0);
}

attr_nonnull void lwip_if_receive_buffer_unlock(lwip_if_socket_t *socket)
{
	socket->receive_buffer_locked = 0;
}

attr_nonnull attr_pure _Bool lwip_if_send_buffer_locked(lwip_if_socket_t *socket)
{
	return((socket->sending_remaining > 0) || (socket->sent_remaining > 0));
}

static void received_callback(_Bool tcp, lwip_if_socket_t *socket, struct pbuf *pbuf_received, const ip_addr_t *address, u16_t port)
{
	struct pbuf *pbuf;
	unsigned int length;

	if(socket->receive_buffer_locked)
	{
		log("received callback: %s: receive buffer locked\n", tcp ? "tcp" : "udp");
		return;
	}

	socket->peer.address = *address;
	socket->peer.port = port;

	for(pbuf = pbuf_received, length = 0; pbuf; pbuf = pbuf->next)
	{
		length += pbuf->len;
		string_append_bytes(socket->receive_buffer, pbuf->payload, pbuf->len);
	}

    pbuf_free(pbuf_received);

	socket->receive_buffer_locked = 1;

	socket->callback_data_received(socket, length);
}

static void *udp_received_callback(void *callback_arg, struct udp_pcb *pcb, struct pbuf *pbuf_received, const ip_addr_t *address, u16_t port)
{
	lwip_if_socket_t *socket = (lwip_if_socket_t *)callback_arg;

	received_callback(false, socket, pbuf_received, address, port);

	return((void *)0);
}

static err_t tcp_received_callback(void *callback_arg, struct tcp_pcb *pcb, struct pbuf *pbuf, err_t error)
{
	lwip_if_socket_t *socket = (lwip_if_socket_t *)callback_arg;
	struct tcp_pcb **pcb_tcp = (struct pcb_tcp **)&socket->tcp.pcb;

	/* connection closed */
	if((pcb == (struct tcp_pcb *)0) || (pbuf == (struct pbuf *)0))
	{
		if(socket->reboot_pending)
		{
			reset();
			return(ERR_ABRT);
		}

		if(pbuf)
			pbuf_free(pbuf);

		if(*pcb_tcp)
		{
			if((error = tcp_close(*pcb_tcp)) != ERR_OK)
			{
				log("tcp received callback: tcp close: error: ");
				log_error(error);
			}
		}

		*pcb_tcp = (struct pcb_tcp *)0;
		socket->sending_remaining = 0;
		socket->sent_remaining = 0;
		return(ERR_OK);
	}

	if(error != ERR_OK)
	{
		log("tcp received callback: error:");
		log_error(error);

		if(pbuf)
			pbuf_free(pbuf);

		if(*pcb_tcp)
			tcp_abort(*pcb_tcp);

		*pcb_tcp = (struct pcb_tcp *)0;
		socket->sending_remaining = 0;
		socket->sent_remaining = 0;
		return(ERR_ABRT);
	}

	if(pcb != *pcb_tcp)
		log("tcp received callback: pcb != *pcb_tcp\n");

	received_callback(true, socket, pbuf, IP_ADDR_ANY, 0);

	tcp_recved(pcb, pbuf->tot_len);

	return(ERR_OK);
}

static _Bool tcp_try_send_buffer(lwip_if_socket_t *socket)
{
	struct tcp_pcb *pcb_tcp = (struct pcb_tcp *)socket->tcp.pcb;
	unsigned int chunk_size, offset, apiflags;
	_Bool sent_one = false;
	err_t error;

	while(socket->sending_remaining > 0)
	{
		chunk_size = socket->sending_remaining;

		apiflags = 0;
		if(chunk_size > lwip_tcp_max_payload)
		{
			chunk_size = lwip_tcp_max_payload;
			apiflags |= TCP_WRITE_FLAG_MORE;
		}

		offset = string_length(socket->send_buffer) - socket->sending_remaining;

		if((error = tcp_write(pcb_tcp, string_buffer(socket->send_buffer) + offset, chunk_size, apiflags)) != ERR_OK)
		{
			log("lwip: tcp write: error: ");
			log_error(error);
			break;
		}

		sent_one = true;
		socket->sending_remaining -= chunk_size;
		socket->sent_remaining += chunk_size;
	}

	if((error = tcp_output(pcb_tcp)) != ERR_OK)
	{
		log("lwip: tcp send: tcp_output: error: ");
		log_error(error);
	}

	return(sent_one);
}

static err_t tcp_sent_callback(void *callback_arg, struct tcp_pcb *pcb, u16_t len)
{
	lwip_if_socket_t *socket = (lwip_if_socket_t *)callback_arg;

	if(len > socket->sent_remaining)
	{
		log("tcp sent callback: acked (%u) > sent_remaining (%u)\n", len, socket->sent_remaining);
		socket->sent_remaining = 0;
	}
	else
		socket->sent_remaining -= len;

	if((socket->sending_remaining > 0) && (!tcp_try_send_buffer(socket)))
		socket->sending_remaining = 0;

	return(ERR_OK);
}

static void tcp_error_callback(void *callback_arg, err_t error)
{
	lwip_if_socket_t *socket = (lwip_if_socket_t *)callback_arg;
	struct tcp_pcb **pcb_tcp = (struct pcb_tcp **)&socket->tcp.pcb;

	log("tcp error callback: socket %p, tcp pcb: %p, error: ", socket, *pcb_tcp);
	log_error(error);

	if(socket->reboot_pending)
		reset();

	*pcb_tcp = (struct tcp_pcb *)0;
	socket->sending_remaining = 0;
	socket->sent_remaining = 0;
}

static err_t tcp_accepted_callback(void *callback_arg, struct tcp_pcb *pcb, err_t error)
{
	lwip_if_socket_t *socket = (lwip_if_socket_t *)callback_arg;
	struct tcp_pcb **pcb_tcp = (struct tcp_pcb **)&socket->tcp.pcb;

	if(error != ERR_OK)
	{
		log("tcp accepted callback: socket  %p, pcb: %p, tcp_pcb: %p, error: ", socket, pcb, *pcb_tcp);
		log_error(error);
	}

	if(*pcb_tcp != (struct tcp_pcb *)0)
	{
		log("tcp accepted callback: abort current\n");
		tcp_abort(*pcb_tcp);
	}

	*pcb_tcp = pcb;

	tcp_nagle_disable(*pcb_tcp);

	tcp_arg(*pcb_tcp, socket);
	tcp_err(*pcb_tcp, tcp_error_callback);
	tcp_recv(*pcb_tcp, tcp_received_callback);
	tcp_sent(*pcb_tcp, tcp_sent_callback);

	return(ERR_OK);
}

attr_nonnull _Bool lwip_if_close(lwip_if_socket_t *socket)
{
	err_t error;

	if(lwip_if_received_udp(socket))
		return(false);

	if(!socket->tcp.listen_pcb)
	{
		log("lwip if close: tcp pcb is null\n");
		return(false);
	}

	if(!socket->tcp.pcb)
	{
		log("lwip if close: not tcp connected\n");
		return(false);
	}

	if(socket->reboot_pending)
	{
		if((error = tcp_close(socket->tcp.pcb)) != ERR_OK)
		{
			log("lwip if close: tcp_close failed, error: ");
			log_error(error);
		}
	}
	else
		tcp_abort(socket->tcp.pcb);

	return(true);
}

attr_nonnull _Bool lwip_if_send(lwip_if_socket_t *socket)
{
	err_t error;

	/* this means the output buffer been changed+sent while it still was sending
	 * very bad! */

	if(socket->sending_remaining > 0)
	{
		log("lwip if send: still sending %u bytes\n", socket->sending_remaining);
		return(false);
	}

	if(socket->sent_remaining > 0)
	{
		log("lwip if send: still waiting for %u bytes to be sent\n", socket->sent_remaining);
		return(false);
	}

	if(socket->peer.port) // received packet from UDP, reply using UDP
	{
		struct pbuf *pbuf = (struct pbuf *)socket->udp.pbuf_send;
		struct udp_pcb *pcb_udp = (struct pcb_udp *)socket->udp.pcb;
		unsigned int offset, length, total_length;

		total_length = string_length(socket->send_buffer);

		for(offset = 0; total_length > 0; offset += length, total_length -= length)
		{
			length = total_length;

			if(length > lwip_udp_max_payload)
				length = lwip_udp_max_payload;

			pbuf->len = pbuf->tot_len = length;
			pbuf->payload = string_buffer(socket->send_buffer) + offset;
			pbuf->eb = 0;

			if((error = udp_sendto(pcb_udp, pbuf, &socket->peer.address, socket->peer.port)) != ERR_OK)
			{
				log("lwip if send: udp send failed: offset: %u, length: %u, error: ", offset, length);
				log_error(error);
			}
		}

		if(socket->udp_term_empty)
		{
			pbuf->len = pbuf->tot_len = 0;
			pbuf->payload = string_buffer(socket->send_buffer);
			pbuf->eb = 0;

			if((error = udp_sendto(pcb_udp, pbuf, &socket->peer.address, socket->peer.port)) != ERR_OK)
			{
				log("lwip if send: udp terminate failed, error: ");
				log_error(error);
			}
		}
	}
	else // received packet from TCP, reply using TCP
	{
		struct tcp_pcb *pcb_tcp = (struct pcb_tcp *)socket->tcp.pcb;

		if(pcb_tcp == (struct tcp_pcb *)0)
		{
			log("lwip if send: tcp send: disconnected\n");
			return(false);
		}

		socket->sending_remaining = string_length(socket->send_buffer);
		socket->sent_remaining = 0;

		if(!tcp_try_send_buffer(socket))
		{
			log("lwip if send: tcp try send buffer failed\n");
			socket->sending_remaining = 0;
			return(false);
		}
	}

	return(true);
}

attr_nonnull _Bool lwip_if_reboot(lwip_if_socket_t *socket)
{
	/* reset after socket close, to be able to flush remaining data in buffer */

	socket->reboot_pending = 1;

	/* if connection is udp or tcp close failed, let the application layer handle the reset */

	if(!lwip_if_close(socket))
		return(false);

	return(true);
}

attr_nonnull _Bool lwip_if_socket_create(lwip_if_socket_t *socket, string_t *receive_buffer, string_t *send_buffer,
		unsigned int port, _Bool udp_term_empty, callback_data_received_fn_t callback_data_received)
{
	err_t error;

	socket->udp.pcb = (struct udp_pcb *)0;
	socket->tcp.listen_pcb = (struct tcp_pcb *)0;
	socket->tcp.pcb = (struct tcp_pcb *)0;
	socket->peer.address = ip_addr_any;
	socket->peer.port = 0;
	socket->receive_buffer = receive_buffer;
	socket->send_buffer = send_buffer;
	socket->sending_remaining = 0;
	socket->sent_remaining = 0;
	socket->receive_buffer_locked = 0;
	socket->reboot_pending = 0;
	socket->udp_term_empty = udp_term_empty ? 1 : 0;
	socket->callback_data_received = callback_data_received;

	if(!(socket->udp.pbuf_send = pbuf_alloc(PBUF_TRANSPORT, 0, PBUF_ROM)))
	{
		log("lwip if socket create: pbuf_alloc failed\n");
		return(false);
	}

	if(!(socket->udp.pcb = udp_new()))
	{
		log("lwip if socket create: udp_new failed\n");
		return(false);
	}

	udp_recv(socket->udp.pcb, udp_received_callback, socket);

	if((error = udp_bind(socket->udp.pcb, IP_ADDR_ANY, port)) != ERR_OK)
	{
		log("lwip if socket create: udp_bind failed: ");
		log_error(error);
		return(false);
	}

	if(!(socket->tcp.listen_pcb = tcp_new()))
	{
		log("lwip if socket create: tcp_new failed\n");
		return(false);
	}

	if((error = tcp_bind(socket->tcp.listen_pcb, IP_ADDR_ANY, port)) != ERR_OK)
	{
		log("lwip if socket create: tcp_bind failed: ");
		log_error(error);
		return(false);
	}

	if(!(socket->tcp.listen_pcb = tcp_listen_with_backlog(socket->tcp.listen_pcb, 1)))
	{
		log("lwip if socket create: tcp_listen failed\n");
		return(false);
	}

	tcp_arg(socket->tcp.listen_pcb, socket);
	tcp_accept(socket->tcp.listen_pcb, tcp_accepted_callback);

	return(true);
}

_Bool attr_nonnull lwip_if_join_mc(int o1, int o2, int o3, int o4)
{
	struct ip_info info;
	ip_addr_to_bytes_t local_ip;
	ip_addr_to_bytes_t mc_ip;

	wifi_get_ip_info(STATION_IF, &info);
	local_ip.ip_addr = info.ip;

	mc_ip.byte[0] = o1;
	mc_ip.byte[1] = o2;
	mc_ip.byte[2] = o3;
	mc_ip.byte[3] = o4;

	return(igmp_joingroup(&local_ip.ip_addr, &mc_ip.ip_addr) == ERR_OK);
}
