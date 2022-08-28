#include "dispatch.h"

#include "util.h"
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
#include "font.h"

#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>

typedef enum attr_packed
{
	ts_copy,
	ts_dodont,
	ts_data,
} telnet_strip_state_t;

assert_size(telnet_strip_state_t, 1);

enum
{
	task_queue_length = 10,
	command_input_state_timeout = 10,
};

typedef struct
{
	unsigned int expected;
	unsigned int timeout;
	unsigned int fragments;
} command_input_state_t;

static command_input_state_t command_input_state;

assert_size(command_input_state, 12);

static os_event_t task_queue[3][task_queue_length];

string_new(static attr_flash_align, command_socket_receive_buffer, sizeof(packet_header_t) + ota_data_offset + SPI_FLASH_SEC_SIZE + 2);
string_new(static attr_flash_align, command_socket_send_buffer,    sizeof(packet_header_t) + ota_data_offset + SPI_FLASH_SEC_SIZE + 2);
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

unsigned int broadcast_groups;

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
				dispatch_post_task(1, task_received_command, task_received_command_uart);
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
		log("task queue %u underrun\n", prio);

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
			app_params_t parameters;
			app_action_t action;
			string_t cooked_src, cooked_dst;
			bool checksum_requested;

			if(argument == task_received_command_uart)
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

			if(argument == task_received_command_packet)
			{
				packet_header_t *packet_header = (packet_header_t *)string_buffer_nonconst(&command_socket_receive_buffer);

				checksum_requested = !!(packet_header->flags & packet_header_flags_md5_32_requested);

				if(packet_header->flags & packet_header_flags_md5_32_provided)
				{
					uint32_t our_checksum, their_checksum;

					their_checksum = packet_header->checksum;
					packet_header->checksum = packet_header_checksum_dummy;

					our_checksum = MD5_trunc_32(string_length(&command_socket_receive_buffer), (const uint8_t *)string_buffer(&command_socket_receive_buffer));

					if(our_checksum != their_checksum)
					{
						stat_dispatch_command_input_checksum_error++;
						string_clear(&command_socket_receive_buffer);
						lwip_if_receive_buffer_unlock(&command_socket);
						return;
					}
				}

				if(packet_header->flags & packet_header_flags_use_bc_group)
				{
					unsigned int packet_broadcast_groups = (packet_header->flags >> packet_header_flag_bc_group_shift) & packet_header_flag_bc_group_bits;

					if(!(packet_broadcast_groups & broadcast_groups))
					{
						string_clear(&command_socket_receive_buffer);
						lwip_if_receive_buffer_unlock(&command_socket);
						break;
					}
					else
						stat_broadcast_group_received++;
				}

				string_set(&cooked_src,
						string_buffer_nonconst(&command_socket_receive_buffer) + sizeof(packet_header_t),
						string_size(&command_socket_receive_buffer) - sizeof(packet_header_t),
						string_length(&command_socket_receive_buffer) - sizeof(packet_header_t));
				parameters.src = &cooked_src;

				string_set(&cooked_dst,
						string_buffer_nonconst(&command_socket_send_buffer) + sizeof(packet_header_t),
						string_size(&command_socket_send_buffer) - sizeof(packet_header_t),
						0);

				parameters.dst = &cooked_dst;
			}
			else
			{
				parameters.src = &command_socket_receive_buffer;
				parameters.dst = &command_socket_send_buffer;
			}

			action = application_content(&parameters);

			string_clear(&command_socket_receive_buffer);
			lwip_if_receive_buffer_unlock(&command_socket);

			if(action == app_action_empty)
			{
				string_clear(parameters.dst);
				string_append(parameters.dst, "> empty command\n");
			}

			if(action == app_action_disconnect)
			{
				string_clear(parameters.dst);
				string_append(parameters.dst, "> disconnect\n");
			}

			if(action == app_action_reset)
			{
				string_clear(parameters.dst);
				string_append(parameters.dst, "> reset\n");
			}

			if(argument == task_received_command_uart) // commands from uart enabled
				uart_send_string(0, parameters.dst);

			if(argument == task_received_command_packet)
			{
				packet_header_t *packet_header = (packet_header_t *)string_buffer_nonconst(&command_socket_send_buffer);
				unsigned int packet_length = sizeof(packet_header_t) + string_length(parameters.dst);

				string_setlength(&command_socket_send_buffer, packet_length);
				packet_header->id = packet_header_id;
				packet_header->length = packet_length;
				packet_header->checksum = packet_header_checksum_dummy;
				packet_header->flags = 0;

				if(checksum_requested)
				{
					packet_header->flags |= packet_header_flags_md5_32_provided;
					packet_header->checksum = MD5_trunc_32(packet_length, (const uint8_t *)string_buffer(&command_socket_send_buffer));
				}
			}

			if(!lwip_if_send(&command_socket))
				log("dispatch: lwip send failed\n");

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

iram static void fast_timer_run(unsigned int period)
{
	stat_fast_timer++;
	io_periodic_fast(period);
}

iram static void fast_timer_callback(void *arg)
{
	// timer runs every 10 ms = 100 Hz

	fast_timer_run(100);
	os_timer_arm(&fast_timer, 10, 0);
}

static void slow_timer_callback(void *arg)
{
	// run background task every ~100 ms = ~10 Hz

	stat_slow_timer++;

	if(command_input_state.timeout > 0)
	{
		command_input_state.timeout--;

		if(command_input_state.timeout == 0)
		{
			stat_dispatch_command_input_timeout++;
			command_input_state.expected = 0;
			command_input_state.fragments = 0;
			string_clear(&command_socket_receive_buffer);
			lwip_if_receive_buffer_unlock(&command_socket);
		}
	}

	if(config_flags_match(flag_wlan_power_save))
		fast_timer_run(10);

	dispatch_post_task(1, task_update_time, 0);

	if(uart_bridge_active || config_flags_match(flag_cmd_from_uart))
		dispatch_post_task(0, task_uart_bridge, 0);

	if(display_detected())
		dispatch_post_task(2, task_display_update, 0);

	dispatch_post_task(2, task_periodic_i2c_sensors, 0);

	// fallback to config-ap-mode when not connected or no ip within 30 seconds

	if(!stat_flags.wlan_recovery_mode_active && (stat_slow_timer == 300) && (wifi_station_get_connect_status() != STATION_GOT_IP))
		dispatch_post_task(1, task_fallback_wlan, 0);

	io_periodic_slow(10);

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

			multicast_init_groups();
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

static void socket_command_callback_data_received(lwip_if_socket_t *socket, unsigned int length, bool broadcast, bool multicast)
{
	command_input_state.fragments++;

	if(broadcast)
		stat_broadcast_received++;

	if(string_full(&command_socket_receive_buffer))
	{
		log("dispatch: overrun, %d %d %u\n", string_length(&command_socket_receive_buffer), string_size(&command_socket_receive_buffer), length);
		command_input_state.expected = 0;
		command_input_state.timeout = 0;
		command_input_state.fragments = 0;
		string_clear(&command_socket_receive_buffer);
		lwip_if_receive_buffer_unlock(&command_socket);
		return;
	}

	if((command_input_state.fragments == 1) && (length >= sizeof(packet_header_t)))
	{
		const packet_header_t *packet_header = (const packet_header_t *)string_buffer(&command_socket_receive_buffer);

		if(packet_header->id == packet_header_id)
		{
			unsigned int packet_length = packet_header->length;

			if(packet_length < (sizeof(packet_header_t) + ota_data_offset + SPI_FLASH_SEC_SIZE + 2))
			{
				command_input_state.expected = packet_length;
				command_input_state.timeout = command_input_state_timeout;
			}
		}
	}

	if((command_input_state.expected > 0) && (command_input_state.timeout > 0))
	{
		if(string_length(&command_socket_receive_buffer) >= (int)command_input_state.expected)
		{
			command_input_state.expected = 0;
			command_input_state.timeout = 0;
			command_input_state.fragments = 0;
			dispatch_post_task(1, task_received_command, task_received_command_packet);
		}
		else
			lwip_if_receive_buffer_unlock(&command_socket);

		return;
	}

	if(string_trim_nl(&command_socket_receive_buffer))
	{
		command_input_state.expected = 0;
		command_input_state.timeout = 0;
		command_input_state.fragments = 0;
		dispatch_post_task(1, task_received_command, task_received_command_text);
	}
	else
		lwip_if_receive_buffer_unlock(&command_socket);
}

static void socket_uart_callback_data_received(lwip_if_socket_t *socket, unsigned int received, bool broadcast, bool multicast)
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
	flash_buffer_release(fsb_free, "init");

	system_os_task(user_task_prio_0_handler, USER_TASK_PRIO_0, task_queue[0], task_queue_length);
	system_os_task(user_task_prio_1_handler, USER_TASK_PRIO_1, task_queue[1], task_queue_length);
	system_os_task(user_task_prio_2_handler, USER_TASK_PRIO_2, task_queue[2], task_queue_length);
}

void dispatch_init2(void)
{
	int io, pin;
	unsigned int cmd_port, uart_port;

	command_input_state.expected = 0;
	command_input_state.timeout = 0;
	command_input_state.fragments = 0;

	if(!config_get_uint("broadcast-groups", &broadcast_groups, -1, -1))
		broadcast_groups = 0x00;

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

	wifi_set_event_handler_cb(wlan_event_handler);

	lwip_if_socket_create(&command_socket, &command_socket_receive_buffer, &command_socket_send_buffer, cmd_port,
			true, socket_command_callback_data_received);

	if(uart_port > 0)
	{
		lwip_if_socket_create(&uart_socket, &uart_socket_receive_buffer, &uart_socket_send_buffer, uart_port,
			true, socket_uart_callback_data_received);

		uart_bridge_active = true;
	}

	font_init();

	os_timer_setfn(&slow_timer, slow_timer_callback, (void *)0);
	os_timer_arm(&slow_timer, 100, 0);

	if(!config_flags_match(flag_wlan_power_save))
	{
		os_timer_setfn(&fast_timer, fast_timer_callback, (void *)0);
		os_timer_arm(&fast_timer, 10, 0);
	}

	dispatch_post_task(2, task_init_displays, 0);
}
