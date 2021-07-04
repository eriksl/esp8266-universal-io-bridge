#include "lwip-interface.h"

#include "attribute.h"
#include "util.h"
#include "sys_string.h"
#include "stats.h"
#include "sdk.h"

/* don't bail on wrongly declared functions in old version of lwip, some parameters should be const really */
#pragma GCC diagnostic ignored "-Wdiscarded-qualifiers"
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wpacked"
#pragma GCC diagnostic push

#include <lwip/udp.h>
#include <lwip/tcp.h>
#include <lwip/igmp.h>

#pragma GCC diagnostic pop

#include <stdint.h>
#include <stdbool.h>

enum
{
	lwip_ethernet_max_payload =	1500,
	lwip_ip_header_size =		20,
	lwip_udp_header_size = 		8,
	lwip_tcp_header_size =		20,
	lwip_udp_max_payload =		lwip_ethernet_max_payload - lwip_ip_header_size - lwip_udp_header_size,
};

enum
{
	lwip_error_strings_size = 17
};

static const char * const lwip_error_strings[lwip_error_strings_size] roflash =
{
	"ok",						// 0		ERR_OK
	"out of memory",			// -1		ERR_MEM
	"buffer error",				// -2		ERR_BUF
	"timeout",					// -3		ERR_TIMEOUT
	"routing error",			// -4		ERR_RTE
	"in progress",				// -5		ERR_INPROGRESS
	"illegal value",			// -6		ERR_VAL
	"operation would block",	// -7		ERR_WOULDBLOCK
	"address in use",			// -8		ERR_USE
	"already connected",		// -9		ERR_ISCONN
	"connection aborted",		// -10		ERR_ABRT
	"connection reset",			// -11		ERR_RST
	"connection closed",		// -12		ERR_CLSD
	"not connected",			// -13		ERR_CONN
	"illegal argument",			// -14		ERR_ARG
	"low level netif error",	// -15		ERR_IF
	"unknown",
};

static void log_error(err_t error)
{
	int ix = error;

	if(ix < 0)
		ix = 0 - ix;

	if(ix >= lwip_error_strings_size)
		ix = lwip_error_strings_size - 1;

	log("error: ");
	log_from_flash(lwip_error_strings[ix]);
	log("\n");
}

bool attr_nonnull attr_pure lwip_if_received_tcp(lwip_if_socket_t *socket)
{
	return(socket->peer.port == 0);
}

bool attr_nonnull attr_pure lwip_if_received_udp(lwip_if_socket_t *socket)
{
	return(socket->peer.port != 0);
}

attr_nonnull void lwip_if_receive_buffer_unlock(lwip_if_socket_t *socket)
{
	socket->receive_buffer_locked = 0;
}

attr_nonnull attr_pure bool lwip_if_send_buffer_locked(lwip_if_socket_t *socket)
{
	return((socket->sending_remaining > 0) || (socket->sent_remaining > 0));
}

static void received_callback(bool tcp, lwip_if_socket_t *socket, struct pbuf *pbuf_received, const ip_addr_t *address, u16_t port)
{
	struct pbuf *pbuf;
	unsigned int length;

	if(socket->receive_buffer_locked)
	{
		stat_cmd_receive_buffer_overflow++;
		pbuf_free(pbuf_received); // still processing previous buffer, drop the received data
		return;
	}

	if(((unsigned int)address >= 0x3ffe8000) && ((unsigned int)address < 0x40000000))
	{
		socket->peer.address = *address;
		socket->peer.port = port;
	}
	else
	{
		socket->peer.address = *IP_ADDR_ANY;
		socket->peer.port = 0;
	}

	length = string_length(socket->receive_buffer);

	for(pbuf = pbuf_received; pbuf; pbuf = pbuf->next)
	{
		if(tcp)
		{
			stat_lwip_tcp_received_packets++;
			stat_lwip_tcp_received_bytes += pbuf->len;
		}
		else
		{
			stat_lwip_udp_received_packets++;
			stat_lwip_udp_received_bytes += pbuf->len;
		}
		string_append_bytes(socket->receive_buffer, pbuf->payload, pbuf->len);
	}

    pbuf_free(pbuf_received);

	socket->receive_buffer_locked = 1;

	socket->callback_data_received(socket, string_length(socket->receive_buffer) - length);
}

static void udp_received_callback(void *callback_arg, struct udp_pcb *pcb, struct pbuf *pbuf_received, ip_addr_t *address, u16_t port)
{
	lwip_if_socket_t *socket = (lwip_if_socket_t *)callback_arg;

	received_callback(false, socket, pbuf_received, address, port);
}

static err_t tcp_received_callback(void *callback_arg, struct tcp_pcb *pcb, struct pbuf *pbuf, err_t error)
{
	lwip_if_socket_t *socket = (lwip_if_socket_t *)callback_arg;
	struct tcp_pcb **pcb_tcp = (struct tcp_pcb **)&socket->tcp.pcb;

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

		*pcb_tcp = (struct tcp_pcb *)0;
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

		*pcb_tcp = (struct tcp_pcb *)0;
		socket->sending_remaining = 0;
		socket->sent_remaining = 0;
		return(ERR_ABRT);
	}

	if(pcb != *pcb_tcp)
		log("tcp received callback: pcb != *pcb_tcp\n");

	received_callback(true, socket, pbuf, 0, 0);

	tcp_recved(pcb, pbuf->tot_len);

	return(ERR_OK);
}

static bool tcp_try_send_buffer(lwip_if_socket_t *socket)
{
	struct tcp_pcb *pcb_tcp = (struct tcp_pcb *)socket->tcp.pcb;
	unsigned int chunk_size, offset, apiflags;
	unsigned int max_payload;
	unsigned int sent;
	err_t error;

	sent = 0;
	max_payload = tcp_sndbuf(pcb_tcp);

	while(socket->sending_remaining > 0)
	{
		chunk_size = socket->sending_remaining;

		apiflags = TCP_WRITE_FLAG_COPY;
		if(chunk_size > max_payload)
		{
			chunk_size = max_payload;
			apiflags |= TCP_WRITE_FLAG_MORE;
		}

		offset = string_length(socket->send_buffer) - socket->sending_remaining;

		if((error = tcp_write(pcb_tcp, string_buffer(socket->send_buffer) + offset, chunk_size, apiflags)) != ERR_OK)
		{
			if(error == ERR_MEM)
				stat_lwip_tcp_send_segmentation++;
			else
			{
				stat_lwip_tcp_send_error++;
				log("lwip tcp write: error: ");
				log_error(error);
			}

			break;
		}

		stat_lwip_tcp_sent_packets++;
		stat_lwip_tcp_sent_bytes += chunk_size;

		sent++;
		socket->sending_remaining -= chunk_size;
		socket->sent_remaining += chunk_size;
	}

	if((error = tcp_output(pcb_tcp)) != ERR_OK)
	{
		log("lwip: tcp send: tcp_output: error: ");
		log_error(error);
	}

	return(sent > 0);
}

static err_t tcp_sent_callback(void *callback_arg, struct tcp_pcb *pcb, u16_t len)
{
	lwip_if_socket_t *socket = (lwip_if_socket_t *)callback_arg;

	if(len > socket->sent_remaining)
	{
		logf("tcp sent callback: acked (%u) > sent_remaining (%d)\n", len, socket->sent_remaining);
		socket->sent_remaining = 0;
	}
	else
		socket->sent_remaining -= len;

	if(socket->sending_remaining > 0)
		if(!tcp_try_send_buffer(socket))
			socket->sending_remaining = 0;

	return(ERR_OK);
}

static void tcp_error_callback(void *callback_arg, err_t error)
{
	lwip_if_socket_t *socket = (lwip_if_socket_t *)callback_arg;
	struct tcp_pcb **pcb_tcp = (struct tcp_pcb **)&socket->tcp.pcb;

	if(error != ERR_ISCONN)
	{
		logf("tcp error callback: socket %p, tcp pcb: %p, error: ", socket, *pcb_tcp);
		log_error(error);
	}

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
		logf("tcp accepted callback: socket  %p, pcb: %p, tcp_pcb: %p, error: ", socket, pcb, *pcb_tcp);
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

attr_nonnull bool lwip_if_close(lwip_if_socket_t *socket)
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

attr_nonnull bool lwip_if_sendto(lwip_if_socket_t *socket, const ip_addr_t *address, unsigned int port)
{
	err_t error;

	struct pbuf *pbuf = (struct pbuf *)socket->udp.pbuf_send;
	struct udp_pcb *pcb_udp = (struct udp_pcb *)socket->udp.pcb;
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

		if((error = udp_sendto(pcb_udp, pbuf, address, port)) != ERR_OK)
		{
			stat_lwip_udp_send_error++;
			logf("lwip if sendto: udp send failed: offset: %u, length: %u, error: ", offset, length);
			log_error(error);
			return(false);
		}
	}

	if(socket->udp_term_empty)
	{
		pbuf->len = pbuf->tot_len = 0;
		pbuf->payload = string_buffer(socket->send_buffer);
		pbuf->eb = 0;

		if((error = udp_sendto(pcb_udp, pbuf, address, port)) != ERR_OK)
		{
			stat_lwip_udp_send_error++;
			log("lwip if sendto: udp terminate failed, error: ");
			log_error(error);
			return(false);
		}
	}

	return(true);
}

attr_nonnull bool lwip_if_send(lwip_if_socket_t *socket)
{
	err_t error;

	/* this means the output buffer been changed+sent while it still was sending
	 * very bad! */

	if(socket->sending_remaining > 0)
	{
		logf("lwip if send: still sending %d bytes\n", socket->sending_remaining);
		return(false);
	}

	if(socket->sent_remaining > 0)
	{
		logf("lwip if send: still waiting for %d bytes to be sent\n", socket->sent_remaining);
		return(false);
	}

	if(socket->peer.port) // received packet from UDP, reply using UDP
	{
		struct pbuf *pbuf = (struct pbuf *)socket->udp.pbuf_send;
		struct udp_pcb *pcb_udp = (struct udp_pcb *)socket->udp.pcb;
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
				stat_lwip_udp_send_error++;
				logf("lwip if send: udp send failed: offset: %u, length: %u, error: ", offset, length);
				log_error(error);
			}

			stat_lwip_udp_sent_packets++;
			stat_lwip_udp_sent_bytes += length;
		}

		if(socket->udp_term_empty)
		{
			pbuf->len = pbuf->tot_len = 0;
			pbuf->payload = string_buffer(socket->send_buffer);
			pbuf->eb = 0;

			if((error = udp_sendto(pcb_udp, pbuf, &socket->peer.address, socket->peer.port)) != ERR_OK)
			{
				stat_lwip_udp_send_error++;
				log("lwip if send: udp terminate failed, error: ");
				log_error(error);
			}

			stat_lwip_udp_sent_packets++;
		}
	}
	else // received packet from TCP, reply using TCP
	{
		struct tcp_pcb *pcb_tcp = (struct tcp_pcb *)socket->tcp.pcb;

		if(pcb_tcp == (struct tcp_pcb *)0)
		{
			log("lwip if send: tcp send: disconnected\n");
			socket->sending_remaining = 0;
			socket->sent_remaining = 0;
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

attr_nonnull bool lwip_if_reboot(lwip_if_socket_t *socket)
{
	/* reset after socket close, to be able to flush remaining data in buffer */

	socket->reboot_pending = 1;

	/* if connection is udp or tcp close failed, let the application layer handle the reset */

	if(!lwip_if_close(socket))
		return(false);

	return(true);
}

attr_nonnull bool lwip_if_socket_create(lwip_if_socket_t *socket, string_t *receive_buffer, string_t *send_buffer,
		unsigned int port, bool tcp, bool udp_term_empty, callback_data_received_fn_t callback_data_received)
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

	if(tcp)
	{
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
	}
	else
		socket->tcp.listen_pcb = (struct tcp_pcb *)0;

	return(true);
}

bool attr_nonnull lwip_if_join_mc(int o1, int o2, int o3, int o4)
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
