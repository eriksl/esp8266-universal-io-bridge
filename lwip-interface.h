#ifndef lwip_interface_h
#define lwip_interface_h

#include "util.h"

#include <lwip/ip_addr.h>

#include <stdint.h>
#include <stdbool.h>

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

assert_size(lwip_if_socket_t, 52);

bool	attr_nonnull lwip_if_received_tcp(lwip_if_socket_t *);
bool	attr_nonnull lwip_if_received_udp(lwip_if_socket_t *);
void	attr_nonnull lwip_if_receive_buffer_unlock(lwip_if_socket_t *);
bool	attr_nonnull lwip_if_send_buffer_locked(lwip_if_socket_t *);
bool	attr_nonnull lwip_if_send(lwip_if_socket_t *socket);
bool	attr_nonnull lwip_if_sendto(lwip_if_socket_t *socket, const ip_addr_t *address, unsigned int port);
bool	attr_nonnull lwip_if_close(lwip_if_socket_t *socket);
bool	attr_nonnull lwip_if_reboot(lwip_if_socket_t *socket);
bool	attr_nonnull lwip_if_socket_create(lwip_if_socket_t *socket, const char *name, string_t *receive_buffer, string_t *send_buffer,
			unsigned int port, bool create_tcp_socket, callback_data_received_fn_t callback_data_received);
bool	attr_nonnull lwip_if_join_mc(ip_addr_t);
#endif
