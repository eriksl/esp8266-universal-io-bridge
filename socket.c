#include "socket.h"
#include "util.h"
#include "stats.h"

static unsigned int sockets_length = 0;
static socket_t *sockets[2];

iram static socket_t *find_socket(struct espconn *esp_socket)
{
	unsigned int ix;

	switch(esp_socket->type)
	{
		case(ESPCONN_TCP):
		{
			for(ix = 0; ix < sockets_length; ix++)
				if(sockets[ix]->tcp.config.local_port == esp_socket->proto.tcp->local_port)
					return(sockets[ix]);
			break;
		}

		case(ESPCONN_UDP):
		{
			for(ix = 0; ix < sockets_length; ix++)
				if(sockets[ix]->udp.config.local_port == esp_socket->proto.udp->local_port)
					return(sockets[ix]);
			break;
		}

		default: break;
	}

	return((socket_t *)0);
}

iram static void socket_callback_received(void *arg, char *buffer, unsigned short length)
{
	socket_t		*socket;
	struct espconn	*esp_socket = (struct espconn *)arg;

	if(!(socket = find_socket(esp_socket)))
		return;

	switch(esp_socket->type)
	{
		case(ESPCONN_TCP):
		{
			socket->remote.proto			= proto_tcp;
			socket->remote.port				= 0;
			socket->remote.address.byte[0]	= 0;
			socket->remote.address.byte[1]	= 0;
			socket->remote.address.byte[2]	= 0;
			socket->remote.address.byte[3]	= 0;

			break;
		}

		case(ESPCONN_UDP):
		{
			remot_info *remote = (remot_info *)0;

			espconn_get_connection_info(esp_socket, &remote, 0);

			socket->remote.proto			= proto_udp;
			socket->remote.port				= remote->remote_port;
			socket->remote.address.byte[0]	= remote->remote_ip[0];
			socket->remote.address.byte[1]	= remote->remote_ip[1];
			socket->remote.address.byte[2]	= remote->remote_ip[2];
			socket->remote.address.byte[3]	= remote->remote_ip[3];

			break;
		}

		default:
		{
			socket->remote.proto			= proto_none;
			socket->remote.port				= 0;
			socket->remote.address.byte[0]	= 0;
			socket->remote.address.byte[1]	= 0;
			socket->remote.address.byte[2]	= 0;
			socket->remote.address.byte[3]	= 0;

			break;
		}
	}

	if(socket->callback_received)
		socket->callback_received(socket, length, buffer);
}

iram static void socket_callback_sent(void *arg)
{
	struct espconn *esp_socket = (struct espconn *)arg;
	socket_t *socket;

	if(!(socket = find_socket(esp_socket)))
		return;

	if(socket->callback_sent)
		socket->callback_sent(socket);

	string_clear(socket->send_buffer);
	socket->send_busy = false;
}

irom static void socket_callback_error(void *arg, int8_t error)
{
	struct espconn *esp_socket = (struct espconn *)arg;
	socket_t *socket;

	if(!(socket = find_socket(esp_socket)))
		return;

	if(socket->callback_error)
		socket->callback_error(socket, error);

	string_clear(socket->send_buffer);
	socket->send_busy = false;
}

irom static void socket_callback_disconnect(void *arg)
{
	struct espconn *esp_socket = (struct espconn *)arg;
	socket_t *socket;

	if(!(socket = find_socket(esp_socket)))
		return;

	if(socket->callback_disconnect)
		socket->callback_disconnect(socket);

	socket->tcp.child_socket	= (struct espconn *)0;
	socket->remote.proto		= proto_none;

	string_clear(socket->send_buffer);
	socket->send_busy = false;
}

irom static void socket_callback_accept(void *arg)
{
	struct espconn *new_esp_socket = (struct espconn *)arg;
	socket_t *socket;

	if(!(socket = find_socket(new_esp_socket)))
		goto disconnect;

	if(socket->tcp.child_socket)
		goto disconnect;

	socket->tcp.child_socket = new_esp_socket;

	espconn_regist_recvcb(socket->tcp.child_socket,		socket_callback_received);
	espconn_regist_sentcb(socket->tcp.child_socket,		socket_callback_sent);
	espconn_regist_disconcb(socket->tcp.child_socket,	socket_callback_disconnect);
	espconn_regist_reconcb(socket->tcp.child_socket,	socket_callback_error);

	espconn_set_opt(socket->tcp.child_socket, ESPCONN_REUSEADDR | ESPCONN_NODELAY);

	if(socket->callback_accept)
		socket->callback_accept(socket);

	return;

disconnect:
	espconn_disconnect(new_esp_socket); // actually not allowed but not supposed to occur
}

iram void socket_send(socket_t *socket, string_t *buffer)
{
	struct espconn *esp_socket;

	if(socket->send_busy)
		return;

	switch(socket->remote.proto)
	{
		case(proto_tcp):
		{
			esp_socket = socket->tcp.child_socket;
			break;
		}

		case(proto_udp):
		{

			esp_socket = &socket->udp.socket;
			esp_socket->proto.udp->remote_port	= socket->remote.port;
			esp_socket->proto.udp->remote_ip[0]	= socket->remote.address.byte[0];
			esp_socket->proto.udp->remote_ip[1]	= socket->remote.address.byte[1];
			esp_socket->proto.udp->remote_ip[2]	= socket->remote.address.byte[2];
			esp_socket->proto.udp->remote_ip[3]	= socket->remote.address.byte[3];
			break;
		}

		default:
		{
			return;
		}
	}

	socket->send_busy = true;

	if(espconn_send(esp_socket, buffer, length) == 0)
		socket->send_busy = false;
	else
		stat_send_buffer_full++;

	return;
}

irom void socket_create(bool tcp, bool udp, socket_t *socket, string_t *send_buffer, int port, int timeout,
		void (*callback_received)(socket_t *, int, char *),
		void (*callback_sent)(socket_t *),
		void (*callback_error)(socket_t *, int),
		void (*callback_disconnect)(socket_t *),
		void (*callback_accept)(socket_t *))
{
	if(sockets_length >= (sizeof(sockets) / sizeof(*sockets)))
		return;

	sockets[sockets_length++] = socket;

	if(tcp)
	{
		memset(&socket->tcp.config, 0, sizeof(socket->tcp.config));
		memset(&socket->tcp.listen_socket, 0, sizeof(socket->tcp.listen_socket));
		socket->tcp.child_socket = (struct espconn *)0;

		socket->tcp.config.local_port		= port;
		socket->tcp.listen_socket.proto.tcp	= &socket->tcp.config;
		socket->tcp.listen_socket.type		= ESPCONN_TCP;
		socket->tcp.listen_socket.state		= ESPCONN_NONE;

		espconn_regist_connectcb(&socket->tcp.listen_socket, socket_callback_accept);
		espconn_tcp_set_max_con_allow(&socket->tcp.listen_socket, 1);

		espconn_accept(&socket->tcp.listen_socket);
		espconn_regist_time(&socket->tcp.listen_socket, timeout, 0); // this must come after accept()
	}

	if(udp)
	{
		memset(&socket->udp.config, 0, sizeof(socket->udp.config));
		memset(&socket->udp.socket, 0, sizeof(socket->udp.socket));

		socket->udp.config.local_port	= port;
		socket->udp.socket.proto.udp	= &socket->udp.config;
		socket->udp.socket.type			= ESPCONN_UDP;
		socket->udp.socket.state		= ESPCONN_NONE;

		espconn_regist_recvcb(&socket->udp.socket, socket_callback_received);
		espconn_regist_sentcb(&socket->udp.socket, socket_callback_sent);

		espconn_create(&socket->udp.socket);
	}

	socket->send_buffer		= send_buffer;
	socket->remote.proto	= proto_none;
	socket->receive_ready	= false;
	socket->send_busy		= false;

	socket->remote.proto			= proto_none;
	socket->remote.port				= 0;
	socket->remote.address.byte[0]	= 0;
	socket->remote.address.byte[1]	= 0;
	socket->remote.address.byte[2]	= 0;
	socket->remote.address.byte[3]	= 0;

	socket->callback_received	= callback_received;
	socket->callback_sent		= callback_sent;
	socket->callback_error		= callback_error;
	socket->callback_disconnect	= callback_disconnect;
	socket->callback_accept		= callback_accept;

	string_clear(&socket->receive_buffer);
	string_clear(socket->send_buffer);
}
