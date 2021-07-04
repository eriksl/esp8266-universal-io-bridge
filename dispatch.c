#include "dispatch.h"

#include "util.h"
#include "sys_string.h"
#include "application.h"
#include "io.h"
#include "stats.h"
#include "i2c.h"
#include "display.h"
#include "sys_time.h"
#include "i2c_sensor.h"
#include "sequencer.h"
#include "init.h"
#include "config.h"
#include "lwip-interface.h"
#include "remote_trigger.h"
#include "ota.h"

#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>

typedef enum
{
	ts_copy,
	ts_dodont,
	ts_data,
} telnet_strip_state_t;

_Static_assert(sizeof(telnet_strip_state_t) == 4, "sizeof(telnet_strip_state) != 4");

enum
{
	task_queue_length = 10,
};

static os_event_t task_queue[3][task_queue_length];

flash_sector_buffer_use_t flash_sector_buffer_use;
string_new(attr_flash_align, flash_sector_buffer, 4096);

string_new(static attr_flash_align, command_socket_receive_buffer, 4096 + 64);
string_new(static attr_flash_align, command_socket_send_buffer, 4096 + 64);
static lwip_if_socket_t command_socket;

string_new(static, uart_socket_receive_buffer, 128);
string_new(static, uart_socket_send_buffer, 128);
static lwip_if_socket_t uart_socket;

bool uart_bridge_active = false;

static os_timer_t fast_timer;
static os_timer_t slow_timer;

typedef struct
{
	int	io;
	int	pin;
} trigger_t;

static trigger_t trigger_alert = { -1, -1 };
static trigger_t assoc_alert = { -1, -1 };

enum
{
	command_left_to_read_guard = 2 /* seconds */
};

typedef struct
{
	unsigned int length;
	unsigned int timeout;
} command_left_to_read_t;

static command_left_to_read_t command_left_to_read = { 0, 0 };

static void background_task_bridge_uart(void)
{
	unsigned int byte;

	if(uart_empty(0))
		return;

	if(config_flags_match(flag_cmd_from_uart))
	{
		while(!uart_empty(0))
		{
			byte = uart_receive(0);

			if((byte != '\r') && (byte != '\n') && (byte != ' ') && !isprint(byte))
				continue;

			if(byte == '\r')
				byte = '\n';

			string_append_byte(&command_socket_receive_buffer, byte);

			if(byte == '\n')
			{
				dispatch_post_task(1, task_received_command, 1);
				uart_clear_receive_queue(0);
			}
		}

		return;
	}

	if(lwip_if_send_buffer_locked(&uart_socket))
		return;

	string_clear(&uart_socket_send_buffer);

	while(!uart_empty(0) && string_space(&uart_socket_send_buffer))
		string_append_byte(&uart_socket_send_buffer, uart_receive(0));

	if(string_empty(&uart_socket_send_buffer))
		return;

	if(!lwip_if_send(&uart_socket))
	{
		stat_uart_send_buffer_overflow++;
		log("lwip uart send failed\n");
	}
}

static void generic_task_handler(unsigned int prio, task_id_t command, unsigned int argument)
{
	stat_task_executed[prio]++;

	if(stat_task_current_queue[prio] > 0)
		stat_task_current_queue[prio]--;
	else
		logf("task queue %u underrun\n", prio);

	switch(command)
	{
		case(task_invalid):
		{
			break;
		}

		case(task_uart_fetch_fifo):
		{
			uart_task_handler_fetch_fifo(argument);
			break;
		}

		case(task_uart_fill_fifo):
		{
			uart_task_handler_fill_fifo(argument);
			break;
		}

		case(task_reset):
		{
			reset();
			break;
		}

		case(task_uart_bridge):
		{
			background_task_bridge_uart();
			stat_update_uart++;
			break;
		}

		case(task_periodic_i2c_sensors):
		{
			i2c_sensors_periodic();
			break;
		}

		case(task_init_displays):
		{
			uint32_t now = system_get_time();
			display_init();
			stat_init_display_time_us = system_get_time() - now;
			break;
		}

		case(task_received_command):
		{
			app_action_t action;

			if(argument) // commands from uart enabled
			{
				string_init(static, uart_prompt, "\r\n>> ");

				uart_send_string(0, &uart_prompt);
				uart_send_string(0, &command_socket_receive_buffer);
				stat_update_command_uart++;
			}
			else
			{
				if(lwip_if_received_tcp(&command_socket))
					stat_update_command_tcp++;

				if(lwip_if_received_udp(&command_socket))
					stat_update_command_udp++;
			}

			if(lwip_if_send_buffer_locked(&command_socket))
			{
				stat_cmd_send_buffer_overflow++;
				string_clear(&command_socket_receive_buffer);
				lwip_if_receive_buffer_unlock(&command_socket);
				break;
			}

			string_clear(&command_socket_send_buffer);

			action = application_content(&command_socket_receive_buffer, &command_socket_send_buffer);

			string_clear(&command_socket_receive_buffer);
			lwip_if_receive_buffer_unlock(&command_socket);

			if(action == app_action_empty)
			{
				string_clear(&command_socket_send_buffer);
				string_append(&command_socket_send_buffer, "> empty command\n");
			}

			if(action == app_action_disconnect)
			{
				string_clear(&command_socket_send_buffer);
				string_append(&command_socket_send_buffer, "> disconnect\n");
			}

			if(action == app_action_reset)
			{
				string_clear(&command_socket_send_buffer);
				string_append(&command_socket_send_buffer, "> reset\n");
			}

			if(argument) // commands from uart enabled
				uart_send_string(0, &command_socket_send_buffer);

			if(!lwip_if_send(&command_socket))
				log("lwip send failed\n");

			if(action == app_action_disconnect)
				lwip_if_close(&command_socket);

			/*
			 * === ugly workaround ===
			 *
			 * For tcp connections we can use the "sent" callback to make sure all
			 * of our data has been sent before rebooting. For udp there is no such
			 * callback and waiting for it to happen does not work (need a return to
			 * SDK code to achieve it). So lwip_if_reboot will take care for it itself
			 * when possible (tcp), otherwise (udp) it will return false here and the
			 * application needs to finish the operation via a task call.
			 */

			if(action == app_action_reset)
				if(!lwip_if_reboot(&command_socket))
					dispatch_post_task(0, task_reset, 0);

			break;
		}

		case(task_display_update):
		{
			stat_update_display++;
			display_periodic();
			break;
		}

		case(task_fallback_wlan):
		{
			wlan_init_start_recovery();
			break;
		}

		case(task_update_time):
		{
			time_periodic();
			break;
		}

		case(task_run_sequencer):
		{
			sequencer_run();
			break;
		}

		case(task_alert_pin_changed):
		{
			if((trigger_alert.io >= 0) && (trigger_alert.pin >= 0))
				io_trigger_pin((string_t *)0, trigger_alert.io, trigger_alert.pin, io_trigger_on);

			break;
		}

		case(task_alert_association):
		{
			if((assoc_alert.io >= 0) && (assoc_alert.pin >= 0))
				io_trigger_pin((string_t *)0, assoc_alert.io, assoc_alert.pin, io_trigger_on);

			break;
		}

		case(task_alert_disassociation):
		{
			if((assoc_alert.io >= 0) && (assoc_alert.pin >= 0))
				io_trigger_pin((string_t *)0, assoc_alert.io, assoc_alert.pin, io_trigger_off);

			break;
		}

		case(task_remote_trigger):
		{
			remote_trigger_send(argument);
			break;
		}
	}
}

static void user_task_prio_2_handler(struct ETSEventTag *event)
{
	generic_task_handler(0, (task_id_t)event->sig, event->par);
}

static void user_task_prio_1_handler(struct ETSEventTag *event)
{
	generic_task_handler(1, (task_id_t)event->sig, event->par);
}

static void user_task_prio_0_handler(struct ETSEventTag *event)
{
	generic_task_handler(2, (task_id_t)event->sig, event->par);
}

void dispatch_post_task(unsigned int prio, task_id_t command, unsigned int argument)
{
	static roflash const unsigned int sdk_task_id[3] = { USER_TASK_PRIO_2, USER_TASK_PRIO_1, USER_TASK_PRIO_0 };

	if(system_os_post(sdk_task_id[prio], command, argument))
	{
		stat_task_posted[prio]++;
		stat_task_current_queue[prio]++;

		if(stat_task_current_queue[prio] > stat_task_max_queue[prio])
			stat_task_max_queue[prio] = stat_task_current_queue[prio];
	}
	else
		stat_task_post_failed[prio]++;
}

iram static void fast_timer_callback(void *arg)
{
	// timer runs every 10 ms = 100 Hz

	stat_fast_timer++;
	io_periodic_fast();
	os_timer_arm(&fast_timer, 10, 0);
}

static void slow_timer_callback(void *arg)
{
	// run background task every ~100 ms = ~10 Hz

	stat_slow_timer++;

	dispatch_post_task(1, task_update_time, 0);

	if(uart_bridge_active || config_flags_match(flag_cmd_from_uart))
		dispatch_post_task(0, task_uart_bridge, 0);

	if(display_detected())
		dispatch_post_task(2, task_display_update, 0);

	dispatch_post_task(2, task_periodic_i2c_sensors, 0);

	// fallback to config-ap-mode when not connected or no ip within 30 seconds

	if(!stat_flags.wlan_recovery_mode_active && (stat_slow_timer == 300) && (wifi_station_get_connect_status() != STATION_GOT_IP))
		dispatch_post_task(1, task_fallback_wlan, 0);

	/* Workaround for "pre-announced length commands" (like flash-send <length>) not being
	 * received completely (for any reason) and thereby completely locking any further command
	 * processing. This releases the buffer if the command doesn't get completely received within
	 * two seconds. This could be done at tcp close level as well, but it needs to work for udp
	 * as well and this is more robust. */

	if(command_left_to_read.length > 0)
	{
		if(command_left_to_read.timeout > 0)
			command_left_to_read.timeout--;
		else
		{
			logf("dispatch: static length command timeout at %u bytes, cleared command buffer\n", command_left_to_read.length);
			command_left_to_read.length = 0;
			command_left_to_read.timeout = 0;
			string_clear(&command_socket_receive_buffer);
			lwip_if_receive_buffer_unlock(&command_socket);
		}
	}

	io_periodic_slow();

	if(stack_stack_painted <= 0)
		stack_paint_stack();

	os_timer_arm(&slow_timer, 100, 0);
}

static void wlan_event_handler(System_Event_t *event)
{
	switch(event->event)
	{
		case(EVENT_STAMODE_CONNECTED):
		{
			if(stat_init_associate_time_us == 0)
				stat_init_associate_time_us = time_get_us();

			break;
		}
		case(EVENT_STAMODE_GOT_IP):
		{
			if(stat_init_ip_time_us == 0)
				stat_init_ip_time_us = time_get_us();

			if(!lwip_if_join_mc(239, 255, 255, 254))
				log("join mc group failed\n");

			time_sntp_start();

			[[fallthrough]];
		}
		case(EVENT_SOFTAPMODE_STACONNECTED):
		{
			dispatch_post_task(2, task_alert_association, 0);
			break;
		}

		case(EVENT_STAMODE_DISCONNECTED):
		{
			[[fallthrough]];
		}
		case(EVENT_SOFTAPMODE_STADISCONNECTED):
		{
			dispatch_post_task(2, task_alert_disassociation, 0);
			break;
		}
	}
}

static void socket_command_callback_data_received(lwip_if_socket_t *socket, unsigned int length)
{
	static const char command_string[] = "flash-send ";
	unsigned int chunk_length;
	int chunk_offset;

	if((command_left_to_read.length == 0) &&
			string_nmatch_cstr(&command_socket_receive_buffer, command_string, sizeof(command_string) - 1) &&
			(parse_uint(2, &command_socket_receive_buffer, &chunk_length, 10, ' ') == parse_ok) &&
			((chunk_offset = string_sep(&command_socket_receive_buffer, 0, 3, ' ')) >= 0))
	{
		command_left_to_read.timeout = command_left_to_read_guard;
		command_left_to_read.length = chunk_offset + chunk_length;
	}

	if(command_left_to_read.length > 0)
	{
		if(length > command_left_to_read.length)
			command_left_to_read.length = 0;
		else
			command_left_to_read.length -= length;
	}

	if((command_left_to_read.length == 0) && (string_trim_nl(&command_socket_receive_buffer) || lwip_if_received_udp(socket)))
		dispatch_post_task(1, task_received_command, 0);
	else
		lwip_if_receive_buffer_unlock(&command_socket);
}

static void socket_uart_callback_data_received(lwip_if_socket_t *socket, unsigned int received)
{
	int current, length;
	uint8_t byte;
	bool strip_telnet;
	telnet_strip_state_t telnet_strip_state;

	length = string_length(&uart_socket_receive_buffer);

	strip_telnet = config_flags_match(flag_strip_telnet);
	telnet_strip_state = ts_copy;

	for(current = 0; current < length; current++)
	{
		byte = string_at(&uart_socket_receive_buffer, current);

		switch(telnet_strip_state)
		{
			case(ts_copy):
			{
				if(strip_telnet && (byte == 0xff))
					telnet_strip_state = ts_dodont;
				else
				{
					if(uart_full(0))
						stat_uart_receive_buffer_overflow++;
					else
						uart_send(0, byte);
				}

				break;
			}
			case(ts_dodont):
			{
				telnet_strip_state = ts_data;
				break;
			}
			case(ts_data):
			{
				telnet_strip_state = ts_copy;
				break;
			}
		}
	}

	string_clear(&uart_socket_receive_buffer);
	lwip_if_receive_buffer_unlock(socket);
	uart_flush(0);
}

void dispatch_init1(void)
{
	flash_sector_buffer_use = fsb_free;

	system_os_task(user_task_prio_0_handler, USER_TASK_PRIO_0, task_queue[0], task_queue_length);
	system_os_task(user_task_prio_1_handler, USER_TASK_PRIO_1, task_queue[1], task_queue_length);
	system_os_task(user_task_prio_2_handler, USER_TASK_PRIO_2, task_queue[2], task_queue_length);
}

void dispatch_init2(void)
{
	int io, pin;
	unsigned int cmd_port, uart_port, mailbox_port;

	if(config_get_int("trigger.status.io", &io, -1, -1) &&
			config_get_int("trigger.status.pin", &pin, -1, -1))
	{
		trigger_alert.io = io;
		trigger_alert.pin = pin;
	}

	if(config_get_int("trigger.assoc.io", &io, -1, -1) &&
			config_get_int("trigger.assoc.pin", &pin, -1, -1))
	{
		assoc_alert.io = io;
		assoc_alert.pin = pin;
	}

	if(!config_get_uint("cmd.port", &cmd_port, -1, -1))
		cmd_port = 24;

	if(!config_get_uint("bridge.port", &uart_port, -1, -1))
		uart_port = 0;

	if(!config_get_uint("mailbox.port", &mailbox_port, -1, -1))
		mailbox_port = 26;

	wifi_set_event_handler_cb(wlan_event_handler);

	lwip_if_socket_create(&command_socket, &command_socket_receive_buffer, &command_socket_send_buffer, cmd_port,
			true, config_flags_match(flag_udp_term_empty), socket_command_callback_data_received);

	if(uart_port > 0)
	{
		lwip_if_socket_create(&uart_socket, &uart_socket_receive_buffer, &uart_socket_send_buffer, uart_port,
			true, config_flags_match(flag_udp_term_empty), socket_uart_callback_data_received);

		uart_bridge_active = true;
	}

	ota_init(mailbox_port);

	os_timer_setfn(&slow_timer, slow_timer_callback, (void *)0);
	os_timer_arm(&slow_timer, 100, 0);

	os_timer_setfn(&fast_timer, fast_timer_callback, (void *)0);
	os_timer_arm(&fast_timer, 10, 0);

	dispatch_post_task(2, task_init_displays, 0);
}
