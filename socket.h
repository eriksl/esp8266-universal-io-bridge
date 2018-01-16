#ifndef socket_h
#define socket_h

#include "user_main.h"
#include <espconn.h>
#include <stdlib.h>

typedef enum
{
	proto_none,
	proto_tcp,
	proto_udp,
	proto_both,
} socket_proto_t;

typedef struct _socket_t
{
	struct
	{
		esp_udp			config;
		struct espconn	socket;
	} udp;

	struct
	{
		esp_tcp			config;
		struct espconn	listen_socket;
		struct espconn	*child_socket;
	} tcp;

	string_t 		receive_buffer;
	string_t 		*send_buffer;
	bool_t			receive_ready;
	bool_t			send_busy;

	struct
	{
		socket_proto_t		proto;
		int					port;
		ip_addr_to_bytes_t	address;
	} remote;

	void (*callback_received)(struct _socket_t *, int, char *);
	void (*callback_sent)(struct _socket_t *);
	void (*callback_error)(struct _socket_t *, int error);
	void (*callback_disconnect)(struct _socket_t *);
	void (*callback_accept)(struct _socket_t *);
} socket_t;

void socket_send(socket_t *socket, char *buffer, int length);

void socket_create(bool tcp, bool udp, socket_t *socket, string_t *send_buffer, int port, int timeout,
		void (*callback_received)(socket_t *, int, char *),
		void (*callback_sent)(socket_t *),
		void (*callback_error)(socket_t *, int),
		void (*callback_disconnect)(socket_t *),
		void (*callback_accept)(socket_t *));

always_inline static socket_proto_t socket_proto(socket_t *socket)
{
	return(socket->remote.proto);
}

always_inline static void *socket_userdata(socket_t *socket)
{
	return(socket->userdata);
}

always_inline static void socket_disconnect_accepted(socket_t *socket)
{
	if(socket->tcp.child_socket != (struct espconn *)0)
		espconn_disconnect(socket->tcp.child_socket);
}

#endif
