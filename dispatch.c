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
#include "wlan.h"
#include "io_aux.h"
#include "io_mcp.h"
#include "io_pcf.h"

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
	fast_timer_rate_ms = 10,
	slow_timer_rate_ms = 100,
	task_queue_length = 10,
	command_input_state_timeout = 10,
};

typedef struct
{
	int expected;
	int timeout;
	int parts;
} command_input_state_t;

static command_input_state_t command_input_state;

assert_size(command_input_state, 12);

static uint32_t previous_transaction_id[2];

static os_event_t task_queue[3][task_queue_length];

string_new(static attr_flash_align, command_socket_receive_buffer, sizeof(packet_header_t) + 64 + SPI_FLASH_SEC_SIZE);
string_new(static attr_flash_align, command_socket_send_buffer,    sizeof(packet_header_t) + 64 + SPI_FLASH_SEC_SIZE);
static lwip_if_socket_t command_socket;

string_new(static, uart_socket_receive_buffer, 128);
string_new(static, uart_socket_send_buffer, 128);
static lwip_if_socket_t uart_socket;

bool uart_bridge_active = false;

static os_timer_t fast_timer;
static os_timer_t slow_timer;

trigger_t trigger_alert = { -1, -1 };
static trigger_t assoc_alert = { -1, -1 };

unsigned int broadcast_groups;

static void background_task_bridge_uart(void)
{
	unsigned int byte;

	if(uart_empty())
		return;

	if(config_flags_match(flag_cmd_from_uart))
	{
		while(!uart_empty())
		{
			byte = uart_receive();

			if((byte != '\r') && (byte != '\n') && (byte != ' ') && !isprint(byte))
				continue;

			if(byte == '\r')
				byte = '\n';

			string_append_byte(&command_socket_receive_buffer, byte);

			if(byte == '\n')
			{
				dispatch_post_task(task_prio_high, task_received_command, task_received_command_uart, 0, 0);
				uart_clear_receive_queue(0);
			}
		}

		return;
	}

	if(lwip_if_send_buffer_locked(&uart_socket))
		return;

	if(lwip_if_send_buffer_unacked(&uart_socket) == 0)
	{
		string_clear(&uart_socket_send_buffer);

		while(!uart_empty() && string_space(&uart_socket_send_buffer))
			string_append_byte(&uart_socket_send_buffer, uart_receive());

		if(!string_empty(&uart_socket_send_buffer))
		{
			if(!lwip_if_send(&uart_socket))
				stat_uart_send_buffer_overflow++;
		}
	}
}

static void generic_task_handler(unsigned int task_queue_index, const struct ETSEventTag *event)
{
	task_id_t command;
	uint32_t parameter_1;
	uint16_t parameter_2;
	uint8_t parameter_3;

	parameter_1 = event->par;
	parameter_2 = (event->sig & 0xffff0000) >> 16;
	parameter_3 = (event->sig & 0x0000ff00) >> 8;
	command = (task_id_t)(event->sig & 0x000000ff) >> 0;

	stat_task_executed[task_queue_index]++;

	if(stat_task_current_queue[task_queue_index] > 0)
		stat_task_current_queue[task_queue_index]--;
	else
		log("[dispatch] task queue %u underrun\n", task_queue_index);

	switch(command)
	{
		case(task_uart_fetch_fifo):
		{
			uart_task_handler_fetch_fifo(parameter_1);
			break;
		}

		case(task_uart_fill_fifo):
		{
			uart_task_handler_fill_fifo(parameter_1);
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
			string_t cooked_src, cooked_src_oob, cooked_dst;
			bool checksum_requested = false;
			bool transaction_id_provided = false;
			uint32_t transaction_id = 0;

			if(parameter_1 == task_received_command_uart)
			{
				string_init(static, uart_prompt, "\r\n>> ");

				uart_send_string(0, &uart_prompt);
				uart_send_string(0, &command_socket_receive_buffer);
				stat_cmd_uart++;
			}
			else
			{
				if(lwip_if_received_tcp(&command_socket))
					stat_cmd_tcp++;

				if(lwip_if_received_udp(&command_socket))
					stat_cmd_udp++;
			}

			if(lwip_if_send_buffer_locked(&command_socket))
			{
				stat_cmd_send_buffer_overflow++;
				string_clear(&command_socket_receive_buffer);
				lwip_if_receive_buffer_unlock(&command_socket, lwip_if_proto_all);
				break;
			}

			string_clear(&command_socket_send_buffer);

			if(parameter_1 == task_received_command_packet)
			{
				packet_header_t *packet_header = (packet_header_t *)string_buffer_nonconst(&command_socket_receive_buffer);

				if(packet_header->version != packet_header_version)
				{
					log("[dispatch] wrong version packet received: %u\n", packet_header->version);
					goto drop;
				}

				checksum_requested = !!(packet_header->flag.md5_32_requested);

				if(packet_header->flag.md5_32_provided)
				{
					uint32_t our_checksum, their_checksum;

					their_checksum = packet_header->checksum;
					packet_header->checksum = 0;

					our_checksum = MD5_trunc_32(string_length(&command_socket_receive_buffer), (const uint8_t *)string_buffer(&command_socket_receive_buffer));

					if(our_checksum != their_checksum)
					{
						stat_cmd_checksum_error++;
						goto drop;
					}
				}

				if(packet_header->broadcast_groups != 0)
				{
					if(!(packet_header->broadcast_groups & broadcast_groups))
						goto drop;
					else
						stat_broadcast_group_received++;
				}

				if((packet_header->oob_data_offset != packet_header->length) && ((packet_header->oob_data_offset % 4) != 0))
				{
					log("[dispatch] oob data unaligned: %u %u %u %u\n", packet_header->length, packet_header->data_offset,
							packet_header->data_pad_offset, packet_header->oob_data_offset);
					goto drop;
				}

				string_set(&cooked_src,
						string_buffer_nonconst(&command_socket_receive_buffer) + packet_header->data_offset,
						string_size(&command_socket_receive_buffer) - packet_header->data_offset,
						packet_header->data_pad_offset - packet_header->data_offset);

				string_set(&cooked_src_oob,
						string_buffer_nonconst(&command_socket_receive_buffer) + packet_header->oob_data_offset,
						string_size(&command_socket_receive_buffer) - packet_header->oob_data_offset,
						packet_header->length - packet_header->oob_data_offset);

				string_set(&cooked_dst,
						string_buffer_nonconst(&command_socket_send_buffer) + sizeof(packet_header_t),
						string_size(&command_socket_send_buffer) - sizeof(packet_header_t),
						0);

				transaction_id_provided = packet_header->flag.transaction_id_provided;
				transaction_id = packet_header->transaction_id;

				if(transaction_id_provided &&
						((transaction_id == previous_transaction_id[0]) || (transaction_id == previous_transaction_id[1])))
				{
					stat_cmd_duplicate++;
					goto drop;
				}
			}
			else
			{
				int delimiter = string_find(&command_socket_receive_buffer, 0, '\0');

				if((delimiter > 0) && ((delimiter + 1) < string_length(&command_socket_receive_buffer)))
				{
					string_set(&cooked_src,
							string_buffer_nonconst(&command_socket_receive_buffer),
							delimiter - 1,
							delimiter - 1);

					do
						delimiter++;
					while((delimiter % 4) != 0);

					if(delimiter >= string_length(&command_socket_receive_buffer))
					{
						log("[dispatch] raw data oob padding invalid: %d %d\n", string_length(&command_socket_receive_buffer), delimiter);
						goto drop;
					}

					string_set(&cooked_src_oob,
							string_buffer_nonconst(&command_socket_receive_buffer) + delimiter,
							string_size(&command_socket_receive_buffer) - delimiter,
							string_length(&command_socket_receive_buffer) - delimiter);
				}
				else
				{
					string_set(&cooked_src,
							string_buffer_nonconst(&command_socket_receive_buffer),
							string_size(&command_socket_receive_buffer),
							string_length(&command_socket_receive_buffer));

					string_set(&cooked_src_oob,
							string_buffer_nonconst(&command_socket_receive_buffer),
							0,
							0);
				}

				string_set(&cooked_dst,
						string_buffer_nonconst(&command_socket_send_buffer),
						string_size(&command_socket_send_buffer),
						0);
			}

			parameters.src = &cooked_src;
			parameters.src_oob = &cooked_src_oob;
			parameters.dst = &cooked_dst;
			parameters.dst_data_pad_offset = -1;
			parameters.dst_data_oob_offset = -1;

			action = application_content(&parameters);

			string_clear(&command_socket_receive_buffer);
			lwip_if_receive_buffer_unlock(&command_socket, lwip_if_proto_all);

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

			if(parameter_1 == task_received_command_uart) // commands from uart enabled
				uart_send_string(0, parameters.dst);
			else
			{
				if(parameter_1 == task_received_command_packet)
				{
					packet_header_t *packet_header = (packet_header_t *)string_buffer_nonconst(&command_socket_send_buffer);
					unsigned int packet_length = sizeof(packet_header_t) + string_length(parameters.dst);
					string_setlength(&command_socket_send_buffer, packet_length);

					packet_header->soh = packet_header_soh;
					packet_header->version = packet_header_version;
					packet_header->id = packet_header_id;
					packet_header->length = packet_length;
					packet_header->data_offset = sizeof(packet_header_t);

					if((parameters.dst_data_pad_offset > 0) && (parameters.dst_data_oob_offset > 0))
					{
						packet_header->data_pad_offset = sizeof(packet_header_t) + parameters.dst_data_pad_offset;
						packet_header->oob_data_offset = sizeof(packet_header_t) + parameters.dst_data_oob_offset;
					}
					else
					{
						packet_header->data_pad_offset = packet_length;
						packet_header->oob_data_offset = packet_length;
					}

					packet_header->broadcast_groups = 0;
					packet_header->flags = 0;
					packet_header->spare_0 = 0;
					packet_header->spare_1 = 0;
					packet_header->checksum = 0;

					if(transaction_id_provided)
					{
						previous_transaction_id[1] = previous_transaction_id[0];
						previous_transaction_id[0] = transaction_id;

						packet_header->flag.transaction_id_provided = 1;
						packet_header->transaction_id = transaction_id;
					}

					if(checksum_requested)
					{
						packet_header->flag.md5_32_provided = 1;
						packet_header->checksum = MD5_trunc_32(packet_length, (const uint8_t *)string_buffer(&command_socket_send_buffer));
					}

					string_setlength(&command_socket_send_buffer, string_length(parameters.dst) + sizeof(packet_header_t));
				}
				else
				{
					if((parameters.dst_data_oob_offset > 0) && (parameters.dst_data_pad_offset > 0))
					{
						char *buffer = string_buffer_nonconst(&command_socket_send_buffer);
						buffer[parameters.dst_data_pad_offset - 1] = '\0';
					}

					string_setlength(&command_socket_send_buffer, string_length(parameters.dst));
				}

				lwip_if_send(&command_socket);
			}

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
					dispatch_post_task(task_prio_high, task_reset, 0, 0, 0);

			break;
drop:
			if(parameter_1 != task_received_command_uart)
			{
				string_clear(&command_socket_receive_buffer);
				lwip_if_receive_buffer_unlock(&command_socket, lwip_if_proto_all);
				string_clear(&command_socket_send_buffer);
			}

			break;
		}

		case(task_display_update):
		{
			stat_update_display++;
			display_periodic();
			break;
		}

		case(task_wlan_recovery):
		{
			wlan_start_recovery();
			break;
		}

		case(task_run_sequencer):
		{
			sequencer_run();
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
			remote_trigger_send(parameter_1);
			break;
		}

		case(task_wlan_reconnect):
		{
			if(!wlan_start())
				log("[dispatch] wlan start failed\n");
			break;
		}

		case(task_pins_changed_gpio):
		{
			io_gpio_pins_changed(parameter_1, parameter_2);
			break;
		}

		case(task_pins_changed_aux):
		{
			io_aux_pins_changed(parameter_1, parameter_2);
			break;
		}

		case(task_pins_changed_mcp):
		{
			io_mcp_pins_changed(parameter_1, parameter_2, parameter_3);
			break;
		}

		case(task_pins_changed_pcf):
		{
			io_pcf_pins_changed(parameter_1, parameter_2, parameter_3);
			break;
		}

		default:
		{
			log("[dispatch] invalid commmand in task\n");
			break;
		}
	}
}

iram static void user_task_prio_0_handler(struct ETSEventTag *event)
{
	generic_task_handler(0, event);
}

iram static void user_task_prio_1_handler(struct ETSEventTag *event)
{
	generic_task_handler(1, event);
}

iram static void user_task_prio_2_handler(struct ETSEventTag *event)
{
	generic_task_handler(2, event);
}

iram bool dispatch_post_task(task_prio_t prio, task_id_t command, uint32_t parameter_1, uint16_t parameter_2, uint8_t parameter_3)
{
	int system_prio;

	switch(prio)
	{
		case(task_prio_low):
		{
			system_prio = USER_TASK_PRIO_0;
			break;
		}
		case(task_prio_medium):
		{
			system_prio = USER_TASK_PRIO_1;
			break;
		}
		case(task_prio_high):
		{
			system_prio = USER_TASK_PRIO_2;
			break;
		}
		default:
		{
			log("[dispatch] post: invalid prio: %u\n", (unsigned int)prio);
			return(false);
		}
	}

	if((command < 0) || (command >= task_size))
	{
		log("[dispatch] post: invalid command\n");
		return(false);
	}

	if(system_os_post(system_prio, (parameter_2 << 16) | (parameter_3 << 8) | command, parameter_1))
	{
		stat_task_posted[prio]++;
		stat_task_current_queue[prio]++;

		if(stat_task_current_queue[prio] > stat_task_max_queue[prio])
			stat_task_max_queue[prio] = stat_task_current_queue[prio];
	}
	else
	{
		stat_task_post_failed[prio]++;
		return(false);
	}

	return(true);
}

iram static void fast_timer_run(unsigned int rate_ms)
{
	stat_fast_timer++;
	io_periodic_fast(rate_ms);
}

iram static void fast_timer_callback(void *arg)
{
	fast_timer_run(fast_timer_rate_ms);
	os_timer_arm(&fast_timer, fast_timer_rate_ms, 0);
}

static void slow_timer_callback(void *arg)
{
	stat_slow_timer++;

	if(config_flags_match(flag_wlan_power_save))
		fast_timer_run(slow_timer_rate_ms);

	time_periodic();

	if(command_input_state.timeout > 0)
	{
		command_input_state.timeout--;

		if(command_input_state.timeout == 0)
		{
			log("[dispatch] tcp input timeout\n");
			stat_cmd_timeout++;
			command_input_state.expected = 0;
			command_input_state.timeout = 0;
			command_input_state.parts = 0;
			string_clear(&command_socket_receive_buffer);
			lwip_if_receive_buffer_unlock(&command_socket, lwip_if_proto_all);
		}
	}

	if(uart_bridge_active || config_flags_match(flag_cmd_from_uart))
		dispatch_post_task(task_prio_medium, task_uart_bridge, 0, 0, 0);

	if(display_detected())
		dispatch_post_task(task_prio_medium, task_display_update, 0, 0, 0);

	dispatch_post_task(task_prio_medium, task_periodic_i2c_sensors, 0, 0, 0);

	io_periodic_slow(slow_timer_rate_ms);

	if(stack_stack_painted <= 0)
		stack_paint_stack();

	if((stat_slow_timer % (1000 / slow_timer_rate_ms)) == 0)
		wlan_periodic();

	os_timer_arm(&slow_timer, slow_timer_rate_ms, 0);
}

static void socket_command_callback_data_received(lwip_if_socket_t *socket, const lwip_if_callback_context_t *context)
{
	const packet_header_t *packet_header;

	if(context->tcp)
		command_input_state.parts += context->parts;
	else
		command_input_state.parts++;

	if(context->overflow > 0)
	{
		log("[dispatch] socket buffer overflow: %d ", context->overflow);
		log("[%d %d %d: %d]", context->original_length, context->length, context->buffer_size, context->parts);
		log("[%d %d %d]\n", command_input_state.expected, command_input_state.timeout, command_input_state.parts);
		stat_cmd_receive_buffer_overflow++;
		goto drop_and_reset;
	}

	if(context->tcp)
	{
		if(command_input_state.parts > 3)
		{
			stat_cmd_tcp_too_many_segments++;
			goto drop_and_reset;
		}

		if(command_input_state.timeout > 0) // expecting more tcp segments ...
		{
			if(command_input_state.expected == 0) // ... raw data ...
			{
				if(context->length > SPI_FLASH_SEC_SIZE)
				{
					command_input_state.timeout = 0;
					command_input_state.parts = 0;
					lwip_if_receive_buffer_lock(socket, lwip_if_proto_all);
					dispatch_post_task(task_prio_high, task_received_command, task_received_command_text, 0, 0);
				}
				else // ... no eol -> unlock and continue receiving
					lwip_if_receive_buffer_unlock(socket, lwip_if_proto_tcp);
			}
			else // ... packet data ...
			{
				if(context->length == command_input_state.expected) // ... packet complete -> push
				{
					command_input_state.expected = 0;
					command_input_state.timeout = 0;
					command_input_state.parts = 0;
					lwip_if_receive_buffer_lock(socket, lwip_if_proto_all);
					dispatch_post_task(task_prio_high, task_received_command, task_received_command_packet, 0, 0);
				}
				else
				{
					if(context->length < command_input_state.expected) // ... packet incomplete -> unlock and continue receiving
						lwip_if_receive_buffer_unlock(socket, lwip_if_proto_tcp);
					else // ... more data than packet -> overrun -> drop
					{
						stat_cmd_invalid_packet_length++;
						goto drop_and_reset;
					}
				}
			}

			return;
		}

		packet_header = (const packet_header_t *)context->buffer;

		if(command_input_state.parts == 1) // first segment ...
		{
			if((context->length >= (int)sizeof(packet_header_t)) && // ... contains packet header ...
				(packet_header->soh == packet_header_soh) &&
				(packet_header->id == packet_header_id))
			{
				if((command_input_state.expected != 0) || (command_input_state.timeout != 0))
				{
					log("[dispatch] already waiting for another session: %d %d, drop\n", command_input_state.expected, command_input_state.timeout);
					goto drop_and_reset;
				}

				if(context->length == packet_header->length) // ... packet already complete -> push
				{
					command_input_state.timeout = 0;
					command_input_state.expected = 0;
					command_input_state.parts = 0;
					lwip_if_receive_buffer_lock(socket, lwip_if_proto_all);
					dispatch_post_task(task_prio_high, task_received_command, task_received_command_packet, 0, 0);
				}
				else
				{
					if(context->length < packet_header->length) // ... packet incomplete -> unlock and continue
					{
						command_input_state.expected = packet_header->length;
						command_input_state.timeout = command_input_state_timeout;
						lwip_if_receive_buffer_unlock(socket, lwip_if_proto_tcp);
					}
					else // ... payload bigger than length field -> drop
					{
						stat_cmd_invalid_packet_length++;
						goto drop_and_reset;
					}
				}
			}
			else // ... contains raw data ...
			{
				if(context->length != 1460 /* tcp mss */)
				{
					string_trim_nl(context->buffer_string);
					command_input_state.timeout = 0;
					command_input_state.parts = 0;
					lwip_if_receive_buffer_lock(socket, lwip_if_proto_all);
					dispatch_post_task(task_prio_high, task_received_command, task_received_command_text, 0, 0);
				}
				else // ... no eol -> unlock and continue receiving
				{
					command_input_state.timeout = command_input_state_timeout;
					command_input_state.expected = 0;
					lwip_if_receive_buffer_unlock(socket, lwip_if_proto_tcp);
				}
			}

			return;
		}

		log("[dispatch] tcp invalid state\n");
		log("parts: %d\n", command_input_state.parts);
		log("length: %d\n", context->length);
		log("packet size: %u\n", sizeof(packet_header_t));
		log("packet soh: %d\n", packet_header->soh);
		log("packet id: %04x\n", packet_header->id);
		log("expected: %d\n", command_input_state.expected);
		log("timeout: %d\n", command_input_state.timeout);

		goto drop_and_reset;
	}

	if(context->udp)
	{
		if(command_input_state.parts > 1)
		{
			log("[dispatch] packet fragmented\n");
			goto drop_and_reset;
		}

		if(command_input_state.expected > 0)
		{
			log("[dispatch] drop udp while expecting packet data\n");
			goto drop_and_reset;
		}

		if(command_input_state.timeout > 0)
		{
			log("[dispatch] drop udp while expecting tcp raw data\n");
			goto drop_and_reset;
		}

		packet_header = (const packet_header_t *)context->buffer;

		// packet contains new packet header ...

		if((command_input_state.parts == 1) && (context->length >= (int)sizeof(packet_header_t)) &&
				(packet_header->soh == packet_header_soh) && (packet_header->id == packet_header_id))
		{
			if(context->length == packet_header->length) // ... packet complete -> push
			{
				command_input_state.timeout = 0;
				command_input_state.expected = 0;
				command_input_state.parts = 0;
				lwip_if_receive_buffer_lock(socket, lwip_if_proto_all);
				dispatch_post_task(task_prio_high, task_received_command, task_received_command_packet, 0, 0);
			}
			else
			{
				if(context->length < packet_header->length) // ... packet incomplete -> error, drop
				{
					stat_cmd_udp_packet_incomplete++;
					goto drop_and_reset;
				}
				else // ... payload bigger than length field -> error, drop
				{
					stat_cmd_invalid_packet_length++;
					goto drop_and_reset;
				}
			}
		}
		else // packet contains raw data -> push
		{
			command_input_state.expected = 0;
			command_input_state.timeout = 0;
			command_input_state.parts = 0;
			lwip_if_receive_buffer_lock(socket, lwip_if_proto_all);
			dispatch_post_task(task_prio_high, task_received_command, task_received_command_text, 0, 0);
		}

		return;
	}

	log("[dispatch] invalid state\n");
	goto drop_and_reset;

drop_and_reset:
	command_input_state.expected = 0;
	command_input_state.timeout = 0;
	command_input_state.parts = 0;

	string_clear(context->buffer_string);
	lwip_if_receive_buffer_unlock(socket, lwip_if_proto_all);
}

static void socket_uart_callback_data_received(lwip_if_socket_t *socket, const lwip_if_callback_context_t *context)
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
	lwip_if_receive_buffer_unlock(socket, lwip_if_proto_all);
	uart_flush(0);
}

void dispatch_init1(void)
{
	flash_buffer_release(fsb_free, "init");

	previous_transaction_id[0] = 0;
	previous_transaction_id[1] = 0;

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
	command_input_state.parts = 0;

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

	lwip_if_socket_create(&command_socket, "command", &command_socket_receive_buffer, &command_socket_send_buffer, cmd_port,
			true, socket_command_callback_data_received);

	if(uart_port > 0)
	{
		lwip_if_socket_create(&uart_socket, "uart", &uart_socket_receive_buffer, &uart_socket_send_buffer, uart_port,
			true, socket_uart_callback_data_received);

		uart_bridge_active = true;
	}

	font_init();

	os_timer_setfn(&slow_timer, slow_timer_callback, (void *)0);
	os_timer_arm(&slow_timer, slow_timer_rate_ms, 0);

	if(!config_flags_match(flag_wlan_power_save))
	{
		os_timer_setfn(&fast_timer, fast_timer_callback, (void *)0);
		os_timer_arm(&fast_timer, fast_timer_rate_ms, 0);
	}

	dispatch_post_task(task_prio_medium, task_init_displays, 0, 0, 0);
}
