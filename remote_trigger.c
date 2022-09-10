#include "attribute.h"
#include "application.h"
#include "io.h"
#include "remote_trigger.h"
#include "lwip-interface.h"
#include "config.h"
#include "dispatch.h"

enum
{
	remote_trigger_local_udp_port = 1,
	remote_trigger_remote_udp_port = 24,
	remote_trigger_max_remotes = 2,
};

typedef struct
{
	union
	{
		unsigned int int_value;
		struct
		{
			unsigned int	io:8;
			unsigned int	pin:8;
			unsigned int	index:8;
			io_trigger_t	action:8;
		};
	};
} remote_trigger_t;

assert_size(remote_trigger_t, 4);

string_new(static, remote_trigger_socket_send_buffer, 64);
string_new(static, remote_trigger_socket_receive_buffer, 64);

static lwip_if_socket_t trigger_socket;
static ip_addr_t		remote_trigger_address[remote_trigger_max_remotes];
static bool				remote_trigger_active;

static void socket_remote_trigger_callback_data_received(lwip_if_socket_t *socket, const lwip_if_callback_context_t *context)
{
	string_clear(&remote_trigger_socket_receive_buffer);
    lwip_if_receive_buffer_unlock(socket);
}

bool remote_trigger_init(void)
{
	unsigned int remote_index;
	ip_addr_t remote_address;
	string_new(, ip, 32);

	remote_trigger_active = false;

	for(remote_index = 0; remote_index < remote_trigger_max_remotes; remote_index++)
	{
		if(config_get_string("trigger.remote.%u.%u", &ip, 0, remote_index) && !string_match_cstr(&ip, "0.0.0.0"))
		{
			remote_trigger_active = true;
			remote_address = ip_addr(string_to_cstr(&ip));
		}
		else
			remote_address = ip_addr(0);

		remote_trigger_address[remote_index] = remote_address;
	}

	if(remote_trigger_active)
		lwip_if_socket_create(&trigger_socket, "trigger", &remote_trigger_socket_receive_buffer, &remote_trigger_socket_send_buffer, remote_trigger_local_udp_port,
				false, socket_remote_trigger_callback_data_received);

	return(true);
}

bool remote_trigger_add(unsigned int remote_index, unsigned int io, unsigned int pin, io_trigger_t action)
{
	remote_trigger_t trigger;

	if(!remote_trigger_active)
		return(false);

	if(remote_index > remote_trigger_max_remotes)
		return(false);

	trigger.index = remote_index;
	trigger.io = io;
	trigger.pin = pin;
	trigger.action = action;

	dispatch_post_task(2, task_remote_trigger, trigger.int_value);

	return(true);
}

void remote_trigger_send(unsigned int argument)
{
	const char *trigger_action = (char *)0;
	remote_trigger_t trigger;
	string_new(, ip, 32);

	if(!remote_trigger_active)
		return;

	if(lwip_if_send_buffer_locked(&trigger_socket))
	{
		log("remote trigger send overflow\n");
		return;
	}

	trigger.int_value = argument;

	if(trigger.index > remote_trigger_max_remotes)
		return;

	switch(trigger.action)
	{
		case(io_trigger_up):	{ trigger_action = "up";	break; }
		case(io_trigger_down):	{ trigger_action = "down";	break; }
		default:				{ return; }
	}

	string_clear(&remote_trigger_socket_send_buffer);
	string_format(&remote_trigger_socket_send_buffer, "it %u %u %s\n", trigger.io, trigger.pin, trigger_action);

	string_ip(&ip, remote_trigger_address[trigger.index]);

	if(!lwip_if_sendto(&trigger_socket, &remote_trigger_address[trigger.index], remote_trigger_remote_udp_port))
		log("remote trigger send failed\n");
}

app_action_t application_function_trigger_remote(app_params_t *parameters)
{
	unsigned int remote_index;
	string_new(, remote_ip, 32);

	if(parse_uint(1, parameters->src, &remote_index, 0, ' ') != parse_ok)
	{
		string_format(parameters->dst, "usage: io-trigger-remote <index 0-%d> [ip address of remote]\n", remote_trigger_max_remotes - 1);
		return(app_action_error);
	}

	if(remote_index >= remote_trigger_max_remotes)
	{
		string_format(parameters->dst, "index must be [0 - %d]\n", remote_trigger_max_remotes - 1);
		return(app_action_error);
	}

	if(parse_string(2, parameters->src, &remote_ip, ' ') == parse_ok)
	{
		if(!config_open_write())
		{
			string_append(parameters->dst, "cannot set config (open)\n");
			return(app_action_error);
		}

		config_delete("trigger.remote.%u.%u", false, 0, remote_index);

		if(!string_match_cstr(&remote_ip, "0.0.0.0"))
		{
			if(!config_set_string("trigger.remote.%u.%u", string_to_cstr(&remote_ip), 0, remote_index))
			{
				config_abort_write();
				string_append(parameters->dst, "cannot set config\n");
				return(app_action_error);
			}
		}

		if(!config_close_write())
		{
			string_append(parameters->dst, "cannot set config (close)\n");
			return(app_action_error);
		}
	}

	string_clear(&remote_ip);
	if(!config_get_string("trigger.remote.%u.%u", &remote_ip, 0, remote_index))
	{
		string_clear(&remote_ip);
		string_append(&remote_ip, "<unset>");
	}

	string_format(parameters->dst, "io-trigger-remote: index: %u, server: %s\n", remote_index, string_to_cstr(&remote_ip));

	return(app_action_normal);
}

