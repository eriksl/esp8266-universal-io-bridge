#ifndef lwip_interface_h
#define lwip_interface_h

#include "util.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpacked"

#include <lwip/ip_addr.h>

#pragma GCC diagnostic pop

struct _lwip_if_socket_t;

typedef void (*callback_data_received_fn_t)(struct _lwip_if_socket_t *, unsigned int);

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
		unsigned int receive_buffer_locked:1;
		unsigned int reboot_pending:1;
		unsigned int udp_term_empty:1;
	};

	struct
	{
		ip_addr_t		address;
		unsigned int	port;
	} peer;

	string_t	*receive_buffer;
	string_t	*send_buffer;
	int			sending_remaining;
	int			sent_remaining;

	callback_data_received_fn_t callback_data_received;

} lwip_if_socket_t;

assert_size(lwip_if_socket_t, 48);

_Bool	attr_nonnull lwip_if_received_tcp(lwip_if_socket_t *);
_Bool	attr_nonnull lwip_if_received_udp(lwip_if_socket_t *);
void	attr_nonnull lwip_if_receive_buffer_unlock(lwip_if_socket_t *);
_Bool	attr_nonnull lwip_if_send_buffer_locked(lwip_if_socket_t *);
_Bool	attr_nonnull lwip_if_send(lwip_if_socket_t *socket);
_Bool	attr_nonnull lwip_if_close(lwip_if_socket_t *socket);
_Bool	attr_nonnull lwip_if_reboot(lwip_if_socket_t *socket);
_Bool	attr_nonnull lwip_if_socket_create(lwip_if_socket_t *socket, string_t *receive_buffer, string_t *send_buffer,
			unsigned int port, _Bool flag_udp_term_empty, callback_data_received_fn_t callback_data_received);
_Bool	attr_nonnull lwip_if_join_mc(int o1, int o2, int o3, int o4);
#endif
