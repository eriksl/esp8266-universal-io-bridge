#ifndef lwip_interface_h
#define lwip_interface_h

#include "util.h"

#include <lwip/ip_addr.h>

#include <stdint.h>
#include <stdbool.h>

typedef enum
{
	lwip_if_proto_tcp = 1 << 0,
	lwip_if_proto_udp = 1 << 1,
	lwip_if_proto_all = lwip_if_proto_tcp | lwip_if_proto_udp,
} lwip_if_proto_t;

struct _lwip_if_socket_t;

typedef struct
{
	bool		tcp;
	bool		udp;
	bool		multicast;
	bool		broadcast;
	int			length;
	int			overflow;
	int			original_length;
	int			parts;
	string_t	*buffer_string;
	int			buffer_size;
	char		*buffer;
	const char	*name;
} lwip_if_callback_context_t;

typedef void (*callback_data_received_fn_t)(struct _lwip_if_socket_t *, const lwip_if_callback_context_t *context);

typedef struct _lwip_if_socket_t
{
	struct
	{
		void *pcb;
		void *pbuf_send;
	} udp;

	struct
	{
		void *listen_pcb;
		void *pcb;
	} tcp;

	struct
	{
		unsigned int reboot_pending:1;

		struct
		{
			unsigned int tcp:1;
			unsigned int udp:1;
		} receive_buffer_locked;
	};

	struct
	{
		unsigned int	port;
	} local;

	struct
	{
		ip_addr_t		address;
		unsigned int	port;
	} peer;

	string_t	*receive_buffer;
	string_t	*send_buffer;
	int			sending_remaining;
	int			sent_unacked;

	char		name[8];

	callback_data_received_fn_t callback_data_received;

} lwip_if_socket_t;

assert_size(lwip_if_socket_t, 64);

attr_nonnull bool			lwip_if_received_tcp(lwip_if_socket_t *);
attr_nonnull bool			lwip_if_received_udp(lwip_if_socket_t *);
attr_nonnull void			lwip_if_receive_buffer_lock(lwip_if_socket_t *, lwip_if_proto_t);
attr_nonnull void			lwip_if_receive_buffer_unlock(lwip_if_socket_t *, lwip_if_proto_t);
attr_nonnull bool			lwip_if_send_buffer_locked(lwip_if_socket_t *);
attr_nonnull unsigned int	lwip_if_send_buffer_unacked(lwip_if_socket_t *);
attr_nonnull bool			lwip_if_send(lwip_if_socket_t *socket);
attr_nonnull bool			lwip_if_sendto(lwip_if_socket_t *socket, const ip_addr_t *address, unsigned int port);
attr_nonnull bool			lwip_if_close(lwip_if_socket_t *socket);
attr_nonnull bool			lwip_if_reboot(lwip_if_socket_t *socket);
attr_nonnull bool			lwip_if_socket_create(lwip_if_socket_t *socket, const char *name, string_t *receive_buffer, string_t *send_buffer,
								unsigned int port, bool create_tcp_socket, callback_data_received_fn_t callback_data_received);
attr_nonnull bool			lwip_if_join_mc(ip_addr_t);
attr_nonnull void			lwip_netstat_bound(string_t *);
attr_nonnull void			lwip_netstat_listening(string_t *);
attr_nonnull void			lwip_netstat_active(string_t *);
attr_nonnull void			lwip_netstat_timewait(string_t *);
#endif
