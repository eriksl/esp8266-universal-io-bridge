#include "lwip-interface.h"

#include "attribute.h"
#include "util.h"
#include "stats.h"
#include "sdk.h"

#include <lwip/udp.h>
#include <lwip/tcp.h>
#include <lwip/igmp.h>
#include <stdint.h>
#include <stdbool.h>

enum
{
	lwip_udp_max_payload = 4800,
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

static void _log_error(const char *name, const char *flash_message, err_t error)
{
	string_new(, message, 128);

	int ix = error;

	if(ix < 0)
		ix = 0 - ix;

	if(ix >= lwip_error_strings_size)
		ix = lwip_error_strings_size - 1;

	string_append_cstr(&message, name);
	string_append(&message, ": ");
	string_append_cstr_flash(&message, flash_message);
	string_append(&message, ", lwip_error: ");
	string_append_cstr_flash(&message, lwip_error_strings[ix]);
	string_append(&message, "\n");

	log_from_flash_0(string_to_cstr(&message));
}

#define log_error(n, s, e) \
do { \
	static roflash const char log_str_flash[] = s; \
	_log_error(n, log_str_flash, e); \
} while(0)

bool attr_nonnull attr_pure lwip_if_received_tcp(lwip_if_socket_t *socket)
{
	return(socket->peer.port == 0);
}

bool attr_nonnull attr_pure lwip_if_received_udp(lwip_if_socket_t *socket)
{
	return(socket->peer.port != 0);
}

attr_nonnull void lwip_if_receive_buffer_lock(lwip_if_socket_t *socket, lwip_if_proto_t proto)
{
	if(proto & lwip_if_proto_tcp)
		socket->receive_buffer_locked.tcp = 1;

	if(proto & lwip_if_proto_udp)
		socket->receive_buffer_locked.udp = 1;
}

attr_nonnull void lwip_if_receive_buffer_unlock(lwip_if_socket_t *socket, lwip_if_proto_t proto)
{
	if(proto & lwip_if_proto_tcp)
		socket->receive_buffer_locked.tcp = 0;

	if(proto & lwip_if_proto_udp)
		socket->receive_buffer_locked.udp = 0;
}

attr_nonnull attr_pure bool lwip_if_send_buffer_locked(lwip_if_socket_t *socket)
{
	return(socket->sending_remaining > 0);
}

static err_t received_callback(bool tcp, lwip_if_socket_t *socket, struct pbuf *pbuf_received, const ip_addr_t *address, u16_t port)
{
	struct pbuf *pbuf;
	ip_addr_t _ip_addr_any = { IPADDR_ANY };
	lwip_if_callback_context_t context;
	bool locked = false;
	int size;

	context.tcp = !!tcp;
	context.udp = !tcp;
	context.multicast = false;
	context.broadcast = false;
	context.overflow = 0;
	context.length = 0;
	context.original_length = string_length(socket->receive_buffer);
	context.parts = 0;

	if(pbuf_received->flags & PBUF_FLAG_LLBCAST)
	{
		stat_lwip_broadcast_received++;
		context.broadcast = true;
	}
	else
	{
		if(pbuf_received->flags & PBUF_FLAG_LLMCAST)
		{
			stat_lwip_multicast_received++;
			context.multicast = true;
		}
		else
			stat_lwip_unicast_received++;
	}

	if(tcp && socket->receive_buffer_locked.tcp)
	{
		stat_lwip_tcp_locked++;
		locked = true;
	}

	if(!tcp && socket->receive_buffer_locked.udp)
	{
		stat_lwip_udp_locked++;
		locked = true;
	}

	if(locked)
	{
		if(context.broadcast)
			stat_lwip_broadcast_dropped++;
		else
			if(context.multicast)
				stat_lwip_multicast_dropped++;
			else
				stat_lwip_unicast_dropped++;

		pbuf_free(pbuf_received); // still processing previous buffer, simply drop the received data
		return(ERR_OK);
	}

	if(((unsigned int)address >= 0x3ffe8000) && ((unsigned int)address < 0x40000000))
	{
		socket->peer.address = *address;
		socket->peer.port = port;
	}
	else
	{
		if(address != (const ip_addr_t *)0)
			log("lwip-interface: received callback address invalid: %p\n", address);
		socket->peer.address = _ip_addr_any;
		socket->peer.port = 0;
	}

	size = string_size(socket->receive_buffer);

	for(pbuf = pbuf_received; pbuf; pbuf = pbuf->next)
	{
		context.parts++;

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

		if((pbuf->len + string_length(socket->receive_buffer)) > size)
			context.overflow += pbuf->len;
		else
			string_append_bytes(socket->receive_buffer, pbuf->payload, pbuf->len);
	}

    pbuf_free(pbuf_received);

	socket->receive_buffer_locked.tcp = 1;
	socket->receive_buffer_locked.udp = 1;

	context.length = string_length(socket->receive_buffer);
	context.buffer_string = socket->receive_buffer;
	context.buffer_size = string_size(socket->receive_buffer);
	context.buffer = string_buffer_nonconst(socket->receive_buffer);

	socket->callback_data_received(socket, &context);

	return(ERR_OK);
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
	err_t local_error;

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
				log_error(socket->name, "tcp received callback: tcp close: error", error);
		}

		*pcb_tcp = (struct tcp_pcb *)0;
		socket->sending_remaining = 0;
		socket->sent_unacked = 0;
		return(ERR_OK);
	}

	if(error != ERR_OK)
	{
		log_error(socket->name, "tcp received callback: error", error);

		if(pbuf)
			pbuf_free(pbuf);

		if(*pcb_tcp)
			tcp_abort(*pcb_tcp);

		*pcb_tcp = (struct tcp_pcb *)0;
		socket->sending_remaining = 0;
		return(ERR_ABRT);
	}

	if(pcb != *pcb_tcp)
		log("tcp received callback: pcb != *pcb_tcp\n");

	if((local_error = received_callback(true, socket, pbuf, 0, 0)) != ERR_OK)
		return(local_error);

	tcp_recved(pcb, pbuf->tot_len);

	return(ERR_OK);
}

static bool tcp_try_send_buffer(lwip_if_socket_t *socket)
{
	struct tcp_pcb *pcb_tcp = (struct tcp_pcb *)socket->tcp.pcb;
	unsigned int chunk_size, offset, apiflags;
	unsigned int tcp_send_buffer_size;
	bool success = false;
	err_t error;

	if(socket->sending_remaining == 0)
	{
		log("lwip tcp try send buffer: no more data left in send buffer\n");
		return(true);
	}

	if((tcp_send_buffer_size = tcp_sndbuf(pcb_tcp)) == 0)
	{
		log("lwip tcp try send buffer: no more data left in tcp send buffer\n");
		return(false);
	}

	chunk_size = socket->sending_remaining;
	apiflags = TCP_WRITE_FLAG_COPY;

	if(chunk_size > tcp_send_buffer_size)
	{
		chunk_size = tcp_send_buffer_size;
		apiflags |= TCP_WRITE_FLAG_MORE;
	}

	offset = string_length(socket->send_buffer) - socket->sending_remaining;

	if((error = tcp_write(pcb_tcp, string_buffer(socket->send_buffer) + offset, chunk_size, apiflags)) != ERR_OK)
	{
		stat_lwip_tcp_send_error++;
		log_error(socket->name, "lwip tcp write: error", error);
	}
	else
	{
		stat_lwip_tcp_sent_packets++;
		stat_lwip_tcp_sent_bytes += chunk_size;
		socket->sending_remaining -= chunk_size;
		socket->sent_unacked += chunk_size;
		success = true;
	}

	if((error = tcp_output(pcb_tcp)) != ERR_OK)
		log_error(socket->name, "lwip: tcp send: tcp_output: error", error);

	return(success);
}

static err_t tcp_sent_callback(void *callback_arg, struct tcp_pcb *pcb, u16_t len)
{
	lwip_if_socket_t *socket = (lwip_if_socket_t *)callback_arg;

	if(len > socket->sent_unacked)
	{
		log("tcp sent callback: acked (%u) > sent_unacked (%d)\n", len, socket->sent_unacked);
		socket->sent_unacked = 0;
	}
	else
		socket->sent_unacked -= len;

	if((socket->sending_remaining > 0) && (!tcp_try_send_buffer(socket)))
		socket->sending_remaining = 0;

	return(ERR_OK);
}

static void tcp_error_callback(void *callback_arg, err_t error)
{
	lwip_if_socket_t *socket = (lwip_if_socket_t *)callback_arg;
	struct tcp_pcb **pcb_tcp = (struct tcp_pcb **)&socket->tcp.pcb;

	if(error != ERR_ISCONN)
		log_error(socket->name, "tcp error callback", error);

	if(socket->reboot_pending)
		reset();

	*pcb_tcp = (struct tcp_pcb *)0;
	socket->sending_remaining = 0;
}

static err_t tcp_accepted_callback(void *callback_arg, struct tcp_pcb *pcb, err_t error)
{
	lwip_if_socket_t *socket = (lwip_if_socket_t *)callback_arg;
	struct tcp_pcb **pcb_tcp = (struct tcp_pcb **)&socket->tcp.pcb;

	if(error != ERR_OK)
		log_error(socket->name, "tcp accepted callback", error);

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
			log_error(socket->name, "lwip if close: tcp_close failed", error);
	}
	else
		tcp_abort(socket->tcp.pcb);

	return(true);
}

attr_nonnull bool lwip_if_sendto(lwip_if_socket_t *socket, const ip_addr_t *address_in, unsigned int port)
{
	err_t error;

	struct pbuf *pbuf = (struct pbuf *)socket->udp.pbuf_send;
	struct udp_pcb *pcb_udp = (struct udp_pcb *)socket->udp.pcb;
	unsigned int offset, length, total_length;
	ip_addr_t address = *address_in;

	total_length = string_length(socket->send_buffer);

	for(offset = 0; total_length > 0; offset += length, total_length -= length)
	{
		length = total_length;

		if(length > lwip_udp_max_payload)
			length = lwip_udp_max_payload;

		pbuf->len = pbuf->tot_len = length;
		pbuf->payload = string_buffer_nonconst(socket->send_buffer) + offset;
		pbuf->eb = 0;

		if((error = udp_sendto(pcb_udp, pbuf, &address, port)) != ERR_OK)
		{
			stat_lwip_udp_send_error++;
			log_error(socket->name, "lwip if sendto: udp send failed", error);
			return(false);
		}
	}

	return(true);
}

attr_nonnull bool lwip_if_send(lwip_if_socket_t *socket)
{
	err_t error;

	/* this means the output buffer been changed+sent while it still was sending! */

	if(socket->sending_remaining > 0)
	{
		log("lwip if send: still sending %d bytes\n", socket->sending_remaining);
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
			pbuf->payload = string_buffer_nonconst(socket->send_buffer) + offset;
			pbuf->eb = 0;

			if((error = udp_sendto(pcb_udp, pbuf, &socket->peer.address, socket->peer.port)) != ERR_OK)
			{
				stat_lwip_udp_send_error++;
				log_error(socket->name, "lwip if send: udp send failed", error);
			}

			stat_lwip_udp_sent_packets++;
			stat_lwip_udp_sent_bytes += length;
		}
	}
	else // received packet from TCP, reply using TCP
	{
		struct tcp_pcb *pcb_tcp = (struct tcp_pcb *)socket->tcp.pcb;

		if(pcb_tcp == (struct tcp_pcb *)0)
		{
			log("lwip if send: tcp send: disconnected\n");
			socket->sending_remaining = 0;
			socket->sent_unacked = 0;
			return(false);
		}

		socket->sending_remaining = string_length(socket->send_buffer);

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

attr_nonnull bool lwip_if_socket_create(lwip_if_socket_t *socket, const char *name, string_t *receive_buffer, string_t *send_buffer,
		unsigned int port, bool create_tcp_socket, callback_data_received_fn_t callback_data_received)
{
	err_t error;
	ip_addr_t _ip_addr_any = { IPADDR_ANY };

	socket->udp.pcb = (struct udp_pcb *)0;
	socket->tcp.listen_pcb = (struct tcp_pcb *)0;
	socket->tcp.pcb = (struct tcp_pcb *)0;
	socket->peer.address = _ip_addr_any;
	socket->peer.port = 0;
	socket->receive_buffer = receive_buffer;
	socket->send_buffer = send_buffer;
	socket->sending_remaining = 0;
	socket->sent_unacked = 0;
	socket->receive_buffer_locked.tcp = 0;
	socket->receive_buffer_locked.udp = 0;
	socket->reboot_pending = 0;
	strecpy(socket->name, name, sizeof(socket->name));
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

	if((error = udp_bind(socket->udp.pcb, &_ip_addr_any, port)) != ERR_OK)
	{
		log_error(socket->name, "lwip if socket create: udp_bind failed", error);
		return(false);
	}

	if(create_tcp_socket)
	{
		if(!(socket->tcp.listen_pcb = tcp_new()))
		{
			log("lwip if socket create: tcp_new failed\n");
			return(false);
		}

		if((error = tcp_bind(socket->tcp.listen_pcb, &_ip_addr_any, port)) != ERR_OK)
		{
			log_error(socket->name, "lwip if socket create: tcp_bind failed", error);
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

	socket->local.port = port;

	return(true);
}

bool attr_nonnull lwip_if_join_mc(ip_addr_t mc_ip)
{
	ip_addr_t _ip_addr_any = { IPADDR_ANY };

	return(igmp_joingroup(&_ip_addr_any, &mc_ip) == ERR_OK);
}
