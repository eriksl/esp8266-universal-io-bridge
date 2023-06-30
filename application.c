#include "util.h"
#include "application.h"
#include "stats.h"
#include "config.h"
#include "uart.h"
#include "i2c.h"
#include "i2c_sensor.h"
#include "display.h"
#include "http.h"
#include "io.h"
#include "io_gpio.h"
#include "sys_time.h"
#include "sequencer.h"
#include "init.h"
#include "remote_trigger.h"
#include "sdk.h"
#include "spi.h"
#include "display_eastrising.h"
#include "display_spitft.h"
#include "display_ssd1306.h"
#include "display_cfa634.h"
#include "display_orbital.h"
#include "font.h"
#include "ota.h"
#include "wlan.h"
#include "io_mcp.h"

#include <stdint.h>
#include <stdbool.h>

typedef struct attr_flash_align
{
	const char		*command_short;
	const char		*command_long;
	app_action_t	(*function)(app_params_t *);
	const char		*description;
} application_function_table_t;

assert_size(application_function_table_t, 16);

roflash static const application_function_table_t application_function_table[];

void application_init(void)
{
	int io, pin;

	trigger_alert.io = -1;
	trigger_alert.pin = -1;

	if(config_get_int("trigger.status.io", &io, -1, -1) &&
			config_get_int("trigger.status.pin", &pin, -1, -1))
	{
		trigger_alert.io = io;
		trigger_alert.pin = pin;
	}
}

app_action_t application_content(app_params_t *parameters)
{
	const application_function_table_t *tableptr;

	if((trigger_alert.io >= 0) &&
			(trigger_alert.pin >= 0))
	{
		io_trigger_pin((string_t *)0, trigger_alert.io, trigger_alert.pin, io_trigger_on);
	}

	if(parse_string(0, parameters->src, parameters->dst, ' ') != parse_ok)
		return(app_action_empty);

	for(tableptr = application_function_table; tableptr->function; tableptr++)
		if(string_match_cstr(parameters->dst, tableptr->command_short) ||
				string_match_cstr(parameters->dst, tableptr->command_long))
			break;

	if(tableptr->function)
	{
		string_clear(parameters->dst);
		return(tableptr->function(parameters));
	}

	string_append(parameters->dst, ": command unknown\n");
	return(app_action_error);
}

static app_action_t application_function_config_dump(app_params_t *parameters)
{
	if(!config_dump(parameters->dst))
	{
		string_append(parameters->dst, "config-dump: failed\n");
		return(app_action_error);
	}

	return(app_action_normal);
}

static app_action_t application_function_config_query_int(app_params_t *parameters)
{
	int index1 = -1;
	int index2 = -1;
	unsigned int value;

	string_clear(parameters->dst);

	if(parse_string(1, parameters->src, parameters->dst, ' ') != parse_ok)
		return(app_action_error);

	if(parse_int(2, parameters->src, &index1, 0, ' ') != parse_ok)
		index1 = -1;
	else
		if(parse_int(3, parameters->src, &index2, 0, ' ') != parse_ok)
			index2 = -1;

	if(!config_get_uint_flashptr(string_to_cstr(parameters->dst), &value, index1, index2))
	{
		string_clear(parameters->dst);
		string_append(parameters->dst, "ERROR\n");
		return(app_action_error);
	}

	string_format(parameters->dst, "=%u OK\n", value);

	return(app_action_normal);
}

static app_action_t application_function_config_query_string(app_params_t *parameters)
{
	string_new(, varid, 64);
	int index1 = -1;
	int index2 = -1;

	if(parse_string(1, parameters->src, &varid, ' ') != parse_ok)
	{
		string_clear(parameters->dst);
		string_append(parameters->dst, "missing variable name\n");
		return(app_action_error);
	}

	if(parse_int(2, parameters->src, &index1, 0, ' ') != parse_ok)
		index1 = -1;
	else
		if(parse_int(3, parameters->src, &index2, 0, ' ') != parse_ok)
			index2 = -1;

	string_clear(parameters->dst);
	string_append_string(parameters->dst, &varid);
	string_append(parameters->dst, "=");

	if(!config_get_string_flashptr(string_to_cstr(&varid), parameters->dst, index1, index2))
	{
		string_clear(parameters->dst);
		string_append(parameters->dst, "ERROR\n");
		return(app_action_error);
	}

	string_append(parameters->dst, " OK\n");

	return(app_action_normal);
}

static app_action_t application_function_config_set(app_params_t *parameters)
{
	int index1, index2, offset;
	string_new(, name, 64);
	string_new(, value, 64);

	if(parse_string(1, parameters->src, &name, ' ') != parse_ok)
		goto usage;

	if(parse_int(2, parameters->src, &index1, 0, ' ') != parse_ok)
		goto usage;

	if(parse_int(3, parameters->src, &index2, 0, ' ') != parse_ok)
		goto usage;

	if((offset = string_sep(parameters->src, 0, 4, ' ')) < 0)
		goto usage;

	string_splice(&value, 0, parameters->src, offset, -1);

	if(!config_open_write())
	{
		string_append(parameters->dst, "config set failure (open)\n");
		return(app_action_error);
	}

	if(!config_set_string_flashptr(string_to_cstr(&name), string_to_cstr(&value), index1, index2))
	{
		config_abort_write();
		string_append(parameters->dst, "config set failure (set)\n");
		return(app_action_error);
	}

	if(!config_close_write())
	{
		string_append(parameters->dst, "config set failure (close)\n");
		return(app_action_error);
	}

	string_append(parameters->dst, "OK\n");

	return(app_action_normal);

usage:
	string_append(parameters->dst, "usage: config-set <variable name pattern> <index1> <index2> <value>\n");
	return(app_action_error);
}

static app_action_t application_function_config_delete(app_params_t *parameters)
{
	int index1, index2;
	unsigned int wildcard;
	string_new(, name, 64);

	if(parse_string(1, parameters->src, &name, ' ') != parse_ok)
	{
		string_clear(parameters->dst);
		string_append(parameters->dst, "missing variable name\n");
		return(app_action_error);
	}

	if(parse_int(2, parameters->src, &index1, 0, ' ') != parse_ok)
		index1 = -1;

	if(parse_int(3, parameters->src, &index2, 0, ' ') != parse_ok)
		index2 = -1;

	if(parse_uint(4, parameters->src, &wildcard, 0, ' ') != parse_ok)
		wildcard = 0;

	if(config_open_write())
	{
		index1 = config_delete_flashptr(string_to_cstr(&name), wildcard != 0, index1, index2);

		if(config_close_write())
		{
			string_format(parameters->dst, "%d config entries deleted\n", index1);
			return(app_action_normal);
		}
		else
			string_append(parameters->dst, "config-delete: config close failure\n");
	}
	else
		string_append(parameters->dst, "config-delete: config open failure\n");

	return(app_action_error);
}

static app_action_t application_function_help(app_params_t *parameters)
{
	const application_function_table_t *tableptr;
	int previous;
	unsigned int x;
	string_new(, topic, 32);

	if(parse_string(1, parameters->src, &topic, ' ') == parse_ok)
	{
		for(tableptr = application_function_table; tableptr->function; tableptr++)
		{
			if( 	(!tableptr->command_short || !string_match_cstr_flash(&topic, tableptr->command_short)) &&
					(!tableptr->command_long  || !string_match_cstr_flash(&topic, tableptr->command_long)))
				continue;

			if(tableptr->command_short)
			{
				string_append_cstr_flash(parameters->dst, tableptr->command_short);

				if(tableptr->command_long && (tableptr->command_long != tableptr->command_short))
					string_append(parameters->dst, "/");
			}

			if(tableptr->command_long && (tableptr->command_long != tableptr->command_short))
				string_append_cstr_flash(parameters->dst, tableptr->command_long);

			if(tableptr->description)
			{
				string_append(parameters->dst, ": ");
				string_append_cstr_flash(parameters->dst, tableptr->description);
			}

			string_append(parameters->dst, "\n");
		}

		if(string_length(parameters->dst) == 0)
			string_append(parameters->dst, "help: no match\n");
	}
	else
	{
		x = 0;

		for(tableptr = application_function_table; tableptr->function; tableptr++)
		{
			previous = string_length(parameters->dst);

			if(tableptr->command_short)
			{
				string_append_cstr_flash(parameters->dst, tableptr->command_short);

				if(tableptr->command_long && (tableptr->command_long != tableptr->command_short))
					string_append(parameters->dst, "/");
			}

			if(tableptr->command_long && (tableptr->command_long != tableptr->command_short))
				string_append_cstr_flash(parameters->dst, tableptr->command_long);

			x += string_length(parameters->dst) - previous;

			if(x < 80)
				string_append(parameters->dst, " ");
			else
			{
				string_append(parameters->dst, "\n");
				x = 0;
			}
		}

		string_append(parameters->dst, "\n");
	}

	return(app_action_normal);
}

static app_action_t application_function_identification(app_params_t *parameters)
{
	int start;
	string_new(, text, 64);

	if((start = string_sep(parameters->src, 0, 1, ' ')) > 0)
	{
		string_splice(&text, 0, parameters->src, start, -1);

		if(!config_open_write() ||
				!config_set_string("identification", string_to_cstr(&text), -1, -1) ||
				!config_close_write())
		{
			config_abort_write();
			string_append(parameters->dst, "> cannot set identification\n");
			return(app_action_error);
		}
	}

	string_clear(&text);

	if(config_get_string("identification", &text, -1, -1) && string_empty(&text))
	{
		if(!config_open_write() ||
				!config_delete("identification", false, -1, -1) ||
				!config_close_write())
		{
			config_abort_write();
			string_append(parameters->dst, "> config delete failure\n");
			return(app_action_error);
		}
	}

	string_clear(&text);

	if(!config_get_string("identification", &text, -1, -1))
		string_append(&text, "<unset>");

	string_format(parameters->dst, "identification is \"%s\"\n", string_to_cstr(&text));

	return(app_action_normal);
}

static app_action_t application_function_quit(app_params_t *parameters)
{
	return(app_action_disconnect);
}

static app_action_t application_function_reset(app_params_t *parameters)
{
	return(app_action_reset);
}

static app_action_t application_function_stats_firmware(app_params_t *parameters)
{
	stats_firmware(parameters->dst);
	return(app_action_normal);
}

static app_action_t application_function_stats_flash(app_params_t *parameters)
{
	stats_flash(parameters->dst);
	return(app_action_normal);
}

static app_action_t application_function_stats_time(app_params_t *parameters)
{
	stats_time(parameters->dst);
	return(app_action_normal);
}

static app_action_t application_function_stats_counters(app_params_t *parameters)
{
	stats_counters(parameters->dst);
	return(app_action_normal);
}

static app_action_t application_function_stats_lwip(app_params_t *parameters)
{
	stats_lwip(parameters->dst);
	return(app_action_normal);
}

static app_action_t application_function_stats_i2c(app_params_t *parameters)
{
	stats_i2c(parameters->dst);
	return(app_action_normal);
}

static app_action_t application_function_stats_uart(app_params_t *parameters)
{
	stats_uart(parameters->dst);
	return(app_action_normal);
}

static app_action_t application_function_bridge_port(app_params_t *parameters)
{
	unsigned int port;

	if(parse_uint(1, parameters->src, &port, 0, ' ') == parse_ok)
	{
		if(port > 65535)
		{
			string_format(parameters->dst, "> invalid port %u\n", port);
			return(app_action_error);
		}

		if(port == 0)
		{
			if(!config_open_write() ||
					!config_delete("bridge.port", -1, -1, false) ||
					!config_close_write())
			{
				config_abort_write();
				string_append(parameters->dst, "> cannot delete config (default values)\n");
				return(app_action_error);
			}
		}
		else
			if(!config_open_write() ||
					!config_set_int("bridge.port", port, -1, -1) ||
					!config_close_write())
			{
				config_abort_write();
				string_append(parameters->dst, "> cannot set config\n");
				return(app_action_error);
			}
	}

	if(!config_get_uint("bridge.port", &port, -1, -1))
		port = 0;

	string_format(parameters->dst, "> port: %u\n", port);

	return(app_action_normal);
}

static app_action_t application_function_command_port(app_params_t *parameters)
{
	unsigned int port;

	if(parse_uint(1, parameters->src, &port, 0, ' ') == parse_ok)
	{
		if(port > 65535)
		{
			string_format(parameters->dst, "> invalid port %u\n", port);
			return(app_action_error);
		}

		if(port == 24)
		{
			if(!config_open_write() ||
					!config_delete("cmd.port", false, -1, -1) ||
					!config_close_write())
			{
				config_abort_write();
				string_append(parameters->dst, "> cannot delete config (default values)\n");
				return(app_action_error);
			}
		}
		else
			if(!config_open_write() ||
					!config_set_int("cmd.port", port, -1, -1) ||
					!config_close_write())
			{
				config_abort_write();
				string_append(parameters->dst, "> cannot set config\n");
				return(app_action_error);
			}
	}

	if(!config_get_uint("cmd.port", &port, -1, -1))
		port = 24;

	string_format(parameters->dst, "> port: %u\n", port);

	return(app_action_normal);
}

static app_action_t application_function_sequencer_clear(app_params_t *parameters)
{
	bool result;

	result = sequencer_clear();

	string_format(parameters->dst, "> sequencer-clear: %s\n", result ? "ok" : "failed");

	return(app_action_normal);
}

static app_action_t application_function_sequencer_remove(app_params_t *parameters)
{
	unsigned int current;

	if(parse_uint(1, parameters->src, &current, 0, ' ') != parse_ok)
	{
		string_append(parameters->dst, "> usage: sequencer-remove index\n");
		return(app_action_error);
	}

	if(!sequencer_remove_entry(current))
	{
		string_append(parameters->dst, "sequencer-remove: failed\n");
		return(app_action_error);
	}

	string_append(parameters->dst, "sequencer-remove: ok\n");

	return(app_action_normal);
}

static app_action_t application_function_sequencer_add(app_params_t *parameters)
{
	static unsigned int start = 0;
	int io, pin, start_in;
	unsigned int duration, value;
	bool active;

	if((parse_int(1, parameters->src, &start_in, 0, ' ') != parse_ok) ||
			(parse_int(2, parameters->src, &io, 0, ' ') != parse_ok) ||
			(parse_int(3, parameters->src, &pin, 0, ' ') != parse_ok) ||
			(parse_uint(4, parameters->src, &value, 0, ' ') != parse_ok) ||
			(parse_uint(5, parameters->src, &duration, 0, ' ') != parse_ok))
	{
		string_append(parameters->dst, "> usage: sequencer-set index io pin value duration_ms\n");
		return(app_action_error);
	}

	if(start_in >= 0)
		start = start_in;

	if(!sequencer_set_entry(start, io, pin, value, duration))
	{
		string_append(parameters->dst, "> sequencer-set: error setting entry (set)\n");
		return(app_action_error);
	}

	if(!sequencer_get_entry(start, &active, &io, &pin, &value, &duration))
	{
		string_append(parameters->dst, "> sequencer-set: error setting entry (get)\n");
		return(app_action_error);
	}

	string_format(parameters->dst, "> sequencer-set: %u: %d/%d %u %u ms %s\n",
			start, io, pin, value, duration, onoff(active));

	start++;

	return(app_action_normal);
}

static app_action_t application_function_sequencer_list(app_params_t *parameters)
{
	static unsigned int start = 0;
	unsigned int index, value, duration;
	int io, pin;
	bool active;

	if(parse_uint(1, parameters->src, &index, 0, ' ') == parse_ok)
		start = index;

	string_append(parameters->dst, "> index io pin value duration_ms\n");

	for(index = 0; index < 20; index++, start++)
	{
		if(!sequencer_get_entry(start, &active, &io, &pin, &value, &duration))
			break;

		string_format(parameters->dst, "> %5u %2d %3d %5u       %5u %s\n", start, io, pin, value, duration, onoff(active));
	}

	return(app_action_normal);
}

static app_action_t application_function_sequencer_start(app_params_t *parameters)
{
	unsigned int start, repeats;

	if((parse_uint(1, parameters->src, &start, 0, ' ') != parse_ok) || (parse_uint(2, parameters->src, &repeats, 0, ' ') != parse_ok))
	{
		string_append(parameters->dst, "> usage: sequencer-play start_entry repeats\n");
		return(app_action_error);
	}

	sequencer_start(start, repeats);

	string_format(parameters->dst, "> sequencer started: %u,%u ok\n", start, repeats);

	return(app_action_normal);
}

static app_action_t application_function_sequencer_stop(app_params_t *parameters)
{
	sequencer_stop();

	string_append(parameters->dst, "> sequencer stopped\n");

	return(app_action_normal);
}

static app_action_t application_function_stats_sequencer(app_params_t *parameters)
{
	bool running, active;
	int io, pin;
	unsigned int start, flash_size, flash_size_entries, flash_offset_flash0, flash_offset_flash1, flash_offset_mapped;
	unsigned int current, value, duration;

	sequencer_get_status(&running, &start, &flash_size, &flash_size_entries, &flash_offset_flash0, &flash_offset_flash1, &flash_offset_mapped);

	string_format(parameters->dst, "> sequencer\n>\n"
			"> running: %s\n"
			"> total flash size available: %u\n"
			"> total entries in flash available: %u\n"
			"> flash offset for ota image #0: 0x%06x\n"
			"> flash offset for ota image #1: 0x%06x\n"
			"> flash offset mapped into address space: 0x%08x\n",
		yesno(running),
		flash_size,
		flash_size_entries,
		flash_offset_flash0,
		flash_offset_flash1,
		flash_offset_mapped);

	if(running)
	{
		string_format(parameters->dst, "> starting from entry: %d\n"
				"> repeats left: %d\n"
				"> remaining duration from current entry: %u\n",
			sequencer_get_start(),
			sequencer_get_repeats() - 1,
			(unsigned int)(sequencer_get_current_end_time() - (time_get_us() / 1000)));

		current = sequencer_get_current();

		if(sequencer_get_entry(current, &active, &io, &pin, &value, &duration))
		{
			string_append(parameters->dst, "> now playing:\n");
			string_append(parameters->dst, "> index io pin value duration_ms\n");
			string_format(parameters->dst, "> %5u %2d %3d %5u       %5u %s\n", current, io, pin, value, duration, onoff(active));
		}
	}

	return(app_action_normal);
}

static app_action_t application_function_uart_baud_rate(app_params_t *parameters)
{
	unsigned int uart, baud_rate;

	if((parse_uint(1, parameters->src, &uart, 0, ' ') != parse_ok) || (uart > 1))
	{
		string_append(parameters->dst, "> usage uart-baudrate <uart [0|1]> <baud rate>\n");
		return(app_action_error);
	}

	if(parse_uint(2, parameters->src, &baud_rate, 0, ' ') == parse_ok)
	{
		if(baud_rate == 115200)
		{
			if(!config_open_write() ||
					!config_delete("uart.baud.%u", false, uart, -1) ||
					!config_close_write())
			{
				config_abort_write();
				string_append(parameters->dst, "> cannot delete config (default values)\n");
				return(app_action_error);
			}
		}
		else
			if(!config_open_write() ||
					!config_set_int("uart.baud.%u", baud_rate, uart, -1) ||
					!config_close_write())
			{
				config_abort_write();
				string_append(parameters->dst, "> cannot set config\n");
				return(app_action_error);
			}

		uart_baudrate(uart, baud_rate);
	}

	if(!config_get_uint("uart.baud.%u", &baud_rate, uart, -1))
		baud_rate = 115200;

	string_format(parameters->dst, "> baudrate[%u]: %u\n", uart, baud_rate);

	return(app_action_normal);
}

static app_action_t application_function_uart_data_bits(app_params_t *parameters)
{
	unsigned int uart, data_bits;

	if((parse_uint(1, parameters->src, &uart, 0, ' ') != parse_ok) || (uart > 1))
	{
		string_append(parameters->dst, "> usage uart-data-bits <uart [0|1]> <data bits>\n");
		return(app_action_error);
	}

	if(parse_uint(2, parameters->src, &data_bits, 0, ' ') == parse_ok)
	{
		if((data_bits < 5) || (data_bits > 8))
		{
			string_format(parameters->dst, "> invalid data bits: %u\n", data_bits);
			return(app_action_error);
		}

		if(data_bits == 8)
		{
			if(!config_open_write() ||
					!config_delete("uart.data.%u", false, uart, -1) ||
					!config_close_write())
			{
				config_abort_write();
				string_append(parameters->dst, "> cannot set config\n");
				return(app_action_error);
			}

		}
		else
			if(!config_open_write() ||
					!config_set_int("uart.data.%u", data_bits, uart, -1) ||
					!config_close_write())
			{
				config_abort_write();
				string_append(parameters->dst, "> cannot set config\n");
				return(app_action_error);
			}

		uart_data_bits(uart, data_bits);
	}

	if(!config_get_uint("uart.data.%u", &data_bits, uart, -1))
		data_bits = 8;

	string_format(parameters->dst, "data bits[%u]: %u\n", uart, data_bits);

	return(app_action_normal);
}

static app_action_t application_function_uart_stop_bits(app_params_t *parameters)
{
	unsigned int uart, stop_bits;

	if((parse_uint(1, parameters->src, &uart, 0, ' ') != parse_ok) || (uart > 1))
	{
		string_append(parameters->dst, "> usage uart-stop-bits <uart [1|2]> <stop bits>\n");
		return(app_action_error);
	}

	if(parse_uint(2, parameters->src, &stop_bits, 0, ' ') == parse_ok)
	{
		if((stop_bits < 1) || (stop_bits > 2))
		{
			string_format(parameters->dst, "> stop bits out of range: %u\n", stop_bits);
			return(app_action_error);
		}

		if(stop_bits == 1)
		{
			if(!config_open_write() ||
					!config_delete("uart.stop.%u", false, uart, -1) ||
					!config_close_write())
			{
				config_abort_write();
				string_append(parameters->dst, "> cannot delete config (default values)\n");
				return(app_action_error);
			}
		}
		else
			if(!config_open_write() ||
					!config_set_int("uart.stop.%u", stop_bits, uart, -1) ||
					!config_close_write())
			{
				config_abort_write();
				string_append(parameters->dst, "> cannot set config\n");
				return(app_action_error);
			}

		uart_stop_bits(uart, stop_bits);
	}

	if(!config_get_uint("uart.stop.%u", &stop_bits, uart, -1))
		stop_bits = 1;

	string_format(parameters->dst, "> stop bits[%u]: %u\n", uart, stop_bits);

	return(app_action_normal);
}

static app_action_t application_function_uart_parity(app_params_t *parameters)
{
	uart_parity_t parity;
	unsigned int uart, parity_int;

	if((parse_uint(1, parameters->src, &uart, 0, ' ') != parse_ok) || (uart > 1))
	{
		string_append(parameters->dst, "> usage uart-parity <uart [0|1]> <parity>\n");
		return(app_action_error);
	}

	if(parse_string(2, parameters->src, parameters->dst, ' ') == parse_ok)
	{
		parity = uart_string_to_parity(parameters->dst);

		if(parity >= parity_error)
		{
			string_append(parameters->dst, ": invalid parity\n");
			return(app_action_error);
		}

		if(parity == parity_none)
		{
			if(!config_open_write() ||
					!config_delete("uart.parity.%u", false, uart, -1) ||
					!config_close_write())
			{
				config_abort_write();
				string_append(parameters->dst, "> cannot set config\n");
				return(app_action_error);
			}

		}
		else
		{
			parity_int = (int)parity;

			if(!config_open_write() ||
					!config_set_int("uart.parity.%u", parity_int, uart, -1) ||
					!config_close_write())
			{
				config_abort_write();
				string_append(parameters->dst, "> cannot set config\n");
				return(app_action_error);
			}
		}

		uart_parity(uart, parity);
	}

	if(config_get_uint("uart.parity.%u", &parity_int, uart, -1))
		parity = (uart_parity_t)parity_int;
	else
		parity = parity_none;

	string_clear(parameters->dst);
	string_format(parameters->dst, "parity[%u]: ", uart);
	uart_parity_to_string(parameters->dst, parity);
	string_append(parameters->dst, "\n");

	return(app_action_normal);
}

static app_action_t application_function_uart_loopback(app_params_t *parameters)
{
	unsigned int uart, mode;

	if((parse_uint(1, parameters->src, &uart, 0, ' ') != parse_ok) || (uart > 1))
	{
		string_append(parameters->dst, "> usage uart-loopback <uart [0|1]> [0|1]\n");
		return(app_action_error);
	}

	if(parse_uint(2, parameters->src, &mode, 0, ' ') == parse_ok)
	{
		if(mode > 1)
		{
			string_append(parameters->dst, "> usage uart-loopback <uart [0|1]> [0|1]\n");
			return(app_action_error);
		}

		uart_loopback(uart, mode ? true : false);

		string_format(parameters->dst, "> uart loopback %s for uart %u\n", onoff(mode), uart);
	}
	else
		string_append(parameters->dst, "> usage uart-loopback <uart [0|1]> [0|1]\n");

	return(app_action_normal);
}

static app_action_t application_function_uart_write(app_params_t *parameters)
{
	unsigned int uart;
	int start, length;
	char last;

	if((parse_uint(1, parameters->src, &uart, 0, ' ') != parse_ok) || ((start = string_sep(parameters->src, 0, 2, ' ')) <= 0))
	{
		string_append(parameters->dst, "> usage: uart-write <uart id> <text>\n");
		return(app_action_error);
	}

	if(uart > 1)
	{
		string_append(parameters->dst, "> uart-write: uart should be 0 or 1\n");
		return(app_action_error);
	}

	length = string_length(parameters->src) - start;

	if(length > 0)
	{
		last = string_at(parameters->src, length - 1);

		if((last == '\n') || (last == '\r'))
			length--;
	}

	for(; length > 0; length--, start++)
		uart_send(uart, string_at(parameters->src, start));

	uart_flush(uart);

	string_append(parameters->dst, "> uart-write ok\n");

	return(app_action_normal);
}

static app_action_t application_function_uart_read(app_params_t *parameters)
{
	string_append(parameters->dst, "> uart receive: ");

	while(!uart_empty())
		string_format(parameters->dst, "%c", (int)uart_receive());

	string_append(parameters->dst, "\n");

	return(app_action_normal);
}

static app_action_t application_function_uart_write_hex(app_params_t *parameters)
{
	unsigned int uart, current, byte;

	if(parse_uint(1, parameters->src, &uart, 0, ' ') != parse_ok)
	{
		string_append(parameters->dst, "> usage: uart-write-hex <uart id> <hex byte> [<hex byte>]\n");
		return(app_action_error);
	}

	if(uart > 1)
	{
		string_append(parameters->dst, "> uart-write-hex: uart should be 0 or 1\n");
		return(app_action_error);
	}

	for(current = 2; current < 128; current++)
	{
		if(parse_uint(current, parameters->src, &byte, 16, ' ') != parse_ok)
			break;

		uart_send(uart, byte);
	}

	uart_flush(uart);

	string_append(parameters->dst, "> uart-write-hex ok\n");

	return(app_action_normal);
}

static app_action_t application_function_uart_read_hex(app_params_t *parameters)
{
	string_append(parameters->dst, "> uart receive hex: ");

	while(!uart_empty())
		string_format(parameters->dst, "%02x ", uart_receive());

	string_append(parameters->dst, "\n");

	return(app_action_normal);
}

static int i2c_address = 0;
static int i2c_bus = 0;

static app_action_t application_function_i2c_address(app_params_t *parameters)
{
	unsigned int intin;

	if(parse_uint(1, parameters->src, &intin, 16, ' ') == parse_ok)
	{
		if((intin < 2) || (intin > 127))
		{
			string_format(parameters->dst, "i2c-address: invalid address 0x%02x\n", intin);
			return(app_action_error);
		}

		i2c_address = intin;
	}

	string_format(parameters->dst, "i2c-address: address: 0x%02x\n", (unsigned int)i2c_address);

	return(app_action_normal);
}

static app_action_t application_function_i2c_bus(app_params_t *parameters)
{
	unsigned int intin;
	i2c_error_t error;

	if(parse_uint(1, parameters->src, &intin, 0, ' ') == parse_ok)
	{
		if((error = i2c_select_bus(intin)) != i2c_error_ok)
		{
			string_append(parameters->dst, "i2c-bus");
			i2c_error_format_string(parameters->dst, error);
			string_append(parameters->dst, "\n");
			return(app_action_error);
		}

		i2c_bus = intin;
	}

	string_format(parameters->dst, "i2c-bus: bus: %d\n", i2c_bus);

	return(app_action_normal);
}

static void i2c_timing_report(string_t *dst, uint32_t from_us, uint32_t to_us, int length, int extra_clocks)
{
	unsigned int clocks, spent_us, speed;

	length++;								// address + r/w
	extra_clocks += 2;						// start and stop condition
	spent_us = to_us - from_us - 8;
	clocks = (length * 9) + extra_clocks;	// 9 is 8 data bits + ack

	speed = 1000000000ULL / ((spent_us * 1000) / clocks);

	string_format(dst, "> transferred %d bytes in %u scl clocks\n", length, clocks);
	string_format(dst, "> time spent: %u microseconds, makes %u Hz i2c bus", spent_us, speed);
}

static app_action_t application_function_i2c_read(app_params_t *parameters)
{
	unsigned int size, current;
	i2c_error_t error;
	uint8_t bytes[32];
	uint32_t from, to;

	if(parse_uint(1, parameters->src, &size, 0, ' ') != parse_ok)
	{
		string_append(parameters->dst, "i2c-read: missing byte count\n");
		return(app_action_error);
	}

	if(size > (int)sizeof(bytes))
	{
		string_format(parameters->dst, "i2c-read: read max %u bytes\n", sizeof(bytes));
		return(app_action_error);
	}

	i2c_select_bus(i2c_bus);

	from = system_get_time();

	if((error = i2c_receive(i2c_address, size, bytes)) != i2c_error_ok)
	{
		string_append(parameters->dst, "i2c_read");
		i2c_error_format_string(parameters->dst, error);
		string_append(parameters->dst, "\n");
		return(app_action_error);
	}

	to = system_get_time();

	string_format(parameters->dst, "> i2c_read: read %u bytes from %02x:", size, (unsigned int)i2c_address);

	for(current = 0; current < size; current++)
		string_format(parameters->dst, " %02x", bytes[current]);

	string_append(parameters->dst, "\n");

	i2c_timing_report(parameters->dst, from, to, size, 0);

	return(app_action_normal);
}

static app_action_t application_function_i2c_write(app_params_t *parameters)
{
	i2c_error_t error;
	static uint8_t bytes[32];
	unsigned int out, size;
	uint32_t from, to;

	for(size = 0; size < (int)sizeof(bytes); size++)
	{
		if(parse_uint(size + 1, parameters->src, &out, 16, ' ') != parse_ok)
			break;

		bytes[size] = (uint8_t)(out & 0xff);
	}

	i2c_select_bus(i2c_bus);

	from = system_get_time();

	if((error = i2c_send(i2c_address, size, bytes)) != i2c_error_ok)
	{
		string_append(parameters->dst, "i2c_write");
		i2c_error_format_string(parameters->dst, error);
		string_append(parameters->dst, "\n");
		return(app_action_error);
	}

	to = system_get_time();

	string_format(parameters->dst, "i2c_write: written %u bytes to %02x\n", size, (unsigned int)i2c_address);

	i2c_timing_report(parameters->dst, from, to, size, 0);

	return(app_action_normal);
}

static app_action_t application_function_i2c_write_read(app_params_t *parameters)
{
	i2c_error_t error;
	uint8_t sendbytes[1];
	uint8_t receivebytes[32];
	unsigned int size, current, out;
	uint32_t from, to;

	if(parse_uint(1, parameters->src, &out, 16, ' ') != parse_ok)
	{
		string_append(parameters->dst, "usage: i2wr <send byte> <amount to read>\n");
		return(app_action_error);
	}

	i2c_select_bus(i2c_bus);

	sendbytes[0] = (uint8_t)(out & 0xff);

	if(parse_uint(2, parameters->src, &size, 0, ' ') != parse_ok)
	{
		string_append(parameters->dst, "usage: i2wr <send byte> <amount to read>\n");
		return(app_action_error);
	}

	if(size >= (int)sizeof(receivebytes))
	{
		string_format(parameters->dst, "i2wr: max read %u bytes\n", sizeof(receivebytes));
		return(app_action_error);
	}

	from = system_get_time();

	if((error = i2c_send1_receive(i2c_address, sendbytes[0], size, receivebytes)) != i2c_error_ok)
	{
		string_append(parameters->dst, "i2wr");
		i2c_error_format_string(parameters->dst, error);
		string_append(parameters->dst, "\n");
		return(app_action_error);
	}

	to = system_get_time();

	string_format(parameters->dst, "> i2wr: read %u bytes from %02x:", size, (unsigned int)i2c_address);

	for(current = 0; current < size; current++)
		string_format(parameters->dst, " %02x", receivebytes[current]);

	string_append(parameters->dst, "\n");

	i2c_timing_report(parameters->dst, from, to, size, 19);

	return(app_action_normal);
}

static app_action_t application_function_i2c_speed(app_params_t *parameters)
{
	unsigned int speed_delay;

	if(parse_uint(1, parameters->src, &speed_delay, 0, ' ') == parse_ok)
	{
		if(speed_delay > 65535)
		{
			string_format(parameters->dst, "> invalid i2c speed delay (0-65535, 1000 is normal): %u\n", speed_delay);
			return(app_action_error);
		}

		if(speed_delay == 1000)
		{
			if(!config_open_write() ||
					!config_delete("i2c.speed_delay", false, -1, -1) ||
					!config_close_write())
			{
				config_abort_write();
				string_append(parameters->dst, "> cannot delete config (default values)\n");
				return(app_action_error);
			}

		}
		else
			if(!config_open_write() ||
					!config_set_int("i2c.speed_delay", speed_delay, -1, -1) ||
					!config_close_write())
			{
				config_abort_write();
				string_append(parameters->dst, "> cannot set config\n");
				return(app_action_error);
			}
	}

	if(!config_get_uint("i2c.speed_delay", &speed_delay, -1, -1))
		speed_delay = 1000;

	i2c_speed_delay(speed_delay);

	string_format(parameters->dst, "> i2c speed delay: %u\n", speed_delay);

	return(app_action_normal);
}

static app_action_t application_function_i2c_sensor_read(app_params_t *parameters)
{
	unsigned int intin, bus;
	i2c_sensor_t sensor;

	if((parse_uint(1, parameters->src, &intin, 0, ' ')) != parse_ok)
	{
		string_format(parameters->dst, "> invalid i2c sensor: %u\n", intin);
		return(app_action_error);
	}

	sensor = (i2c_sensor_t)intin;

	if((parse_uint(2, parameters->src, &bus, 0, ' ')) != parse_ok)
		bus = 0;

	if(bus >= i2c_busses)
	{
		string_format(parameters->dst, "> invalid i2c sensor: %u/%u\n", bus, intin);
		return(app_action_error);
	}

	if(!i2c_sensor_read(parameters->dst, bus, sensor, true, false))
	{
		string_clear(parameters->dst);
		string_format(parameters->dst, "> invalid i2c sensor: %u/%u\n", bus, sensor);
		return(app_action_error);
	}

	string_append(parameters->dst, "\n");

	return(app_action_normal);
}

static app_action_t application_function_i2c_sensor_calibrate(app_params_t *parameters)
{
	unsigned int intin, bus;
	i2c_sensor_t sensor;
	double factor, offset;
	int int_factor, int_offset;

	if(parse_uint(1, parameters->src, &bus, 0, ' ') != parse_ok)
	{
		string_append(parameters->dst, "> missing i2c bus\n");
		return(app_action_error);
	}

	if(parse_uint(2, parameters->src, &intin, 0, ' ') != parse_ok)
	{
		string_append(parameters->dst, "> missing i2c sensor\n");
		return(app_action_error);
	}

	if(bus >= i2c_busses)
	{
		string_format(parameters->dst, "> invalid i2c bus: %u\n", bus);
		return(app_action_error);
	}

	if(intin >= i2c_sensor_size)
	{
		string_format(parameters->dst, "> invalid i2c sensor: %u/%u\n", bus, intin);
		return(app_action_error);
	}

	sensor = (i2c_sensor_t)intin;

	if((parse_float(3, parameters->src, &factor, ' ') == parse_ok) && (parse_float(4, parameters->src, &offset, ' ') == parse_ok))
	{
		int_factor = (int)(factor * 1000.0);
		int_offset = (int)(offset * 1000.0);

		if(!config_open_write())
		{
			config_abort_write();
			string_append(parameters->dst, "cannot open config for writing\n");
			return(app_action_error);
		}

		config_delete("i2s.%u.%u.", true, bus, sensor);

		if((int_factor != 1000) && !config_set_int("i2s.%u.%u.factor", int_factor, bus, sensor))
		{
			config_abort_write();
			string_append(parameters->dst, "> cannot set factor\n");
			return(app_action_error);
		}

		if((int_offset != 0) && !config_set_int("i2s.%u.%u.offset", int_offset, bus, sensor))
		{
			config_abort_write();
			string_append(parameters->dst, "> cannot set offset\n");
			return(app_action_error);
		}

		if(!config_close_write())
		{
			string_append(parameters->dst, "> cannot write config\n");
			return(app_action_error);
		}
	}

	if(!config_get_int("i2s.%u.%u.factor", &int_factor, bus, sensor))
		int_factor = 1000;

	if(!config_get_int("i2s.%u.%u.offset", &int_offset, bus, sensor))
		int_offset = 0;

	string_format(parameters->dst, "> i2c sensor %u/%u calibration set to factor %f, offset: %f\n", bus, sensor, int_factor / 1000.0, int_offset / 1000.0);

	return(app_action_normal);
}

static app_action_t application_function_i2c_sensor_dump(app_params_t *parameters)
{
	unsigned int option;
	bool verbose;
	int original_length;

	original_length = string_length(parameters->dst);
	verbose = false;

	if((parse_uint(1, parameters->src, &option, 0, ' ') == parse_ok) && option)
		verbose = true;

	i2c_sensor_dump(verbose, parameters->dst);

	if(string_length(parameters->dst) == original_length)
		string_append(parameters->dst, "> no sensors detected\n");

	return(app_action_normal);
}

static app_action_t set_unset_flag(string_t *src, string_t *dst, bool add)
{
	if(parse_string(1, src, dst, ' ') == parse_ok)
	{
		if(!config_flag_change_from_string(dst, add))
		{
			string_append(dst, ": unknown flag\n");
			return(app_action_error);
		}
	}

	string_clear(dst);
	string_append(dst, "> flags:\n>\n");
	config_flags_to_string(true, ">    ", dst);

	return(app_action_normal);
}

static app_action_t application_function_flag_set(app_params_t *parameters)
{
	return(set_unset_flag(parameters->src, parameters->dst, true));
}

static app_action_t application_function_flag_unset(app_params_t *parameters)
{
	return(set_unset_flag(parameters->src, parameters->dst, false));
}

static app_action_t application_function_time_set(app_params_t *parameters)
{
	unsigned int Y, M, D, h, m, s;
	const char *source;

	if((parse_uint(1, parameters->src, &h, 0, ' ') == parse_ok) && (parse_uint(2, parameters->src, &m, 0, ' ') == parse_ok))
	{
		if(parse_uint(3, parameters->src, &s, 0, ' ') != parse_ok)
			s = 0;

		time_set_hms(h, m, s);
	}

	source = time_get(&h, &m, &s, &Y, &M, &D);

	string_format(parameters->dst, "%s: %04u/%02u/%02u %02u:%02u:%02u\n", source, Y, M, D, h, m, s);

	return(app_action_normal);
}

static app_action_t application_function_time_stamp_set(app_params_t *parameters)
{
	unsigned int stamp;
	unsigned int Y, M, D, h, m, s;
	const char *source;

	if(parse_uint(1, parameters->src, &stamp, 0, ' ') == parse_ok)
		time_set_stamp(stamp);

	source = time_get(&h, &m, &s, &Y, &M, &D);

	string_format(parameters->dst, "%s: %04u/%02u/%02u %02u:%02u:%02u\n", source, Y, M, D, h, m, s);

	return(app_action_normal);
}

static app_action_t application_function_time_zone_set(app_params_t *parameters)
{
	int tz;

	if(parse_int(1, parameters->src, &tz, 0, ' ') == parse_ok)
	{
		string_clear(parameters->src);

		if(!time_set_timezone(tz, parameters->src))
		{
			string_append(parameters->dst, "time-zone-set: error: ");
			string_append_string(parameters->dst, parameters->src);
			return(app_action_error);
		}
	}

	string_format(parameters->dst, "time-zone-set: timezone: %d\n", time_get_timezone());

	return(app_action_normal);
}

static app_action_t application_function_log_display(app_params_t *parameters)
{
	if(string_length(&logbuffer) == 0)
		string_append(parameters->dst, "<log empty>\n");
	else
	{
		string_clear(parameters->dst);
		string_append_string(parameters->dst, &logbuffer);
	}

	return(app_action_normal);
}

static app_action_t application_function_log_clear(app_params_t *parameters)
{
	app_action_t rv;

	rv = application_function_log_display(parameters);

	logbuffer_clear();

	return(rv);
}

static app_action_t application_function_log_write(app_params_t *parameters)
{
	string_new(, text, 64);
	unsigned int start;

	if((start = string_sep(parameters->src, 0, 1, ' ')) > 0)
	{
		string_splice(&text, 0, parameters->src, start, -1);
		log("%s\n", string_to_cstr(&text));
	}

	string_append(parameters->dst, "log write ok\n");

	return(app_action_normal);
}

static app_action_t application_function_sntp_set(app_params_t *parameters)
{
	string_new(, ipstring, 32);
	ip_addr_t ipaddr;

	if(parse_string(1, parameters->src, &ipstring, ' ') == parse_ok)
	{
		ipaddr = ip_addr(string_to_cstr(&ipstring));

		if(!time_sntp_set_server(parameters->dst, ipaddr))
			return(app_action_error);

		time_sntp_start();

		ipaddr.addr = 0;
	}

	if(!time_sntp_get_server(&ipaddr))
	{
		string_append(parameters->dst, "> server address unavailable\n");
		return(app_action_error);
	}

	string_clear(&ipstring);
	string_ip(&ipstring, ipaddr);

	string_format(parameters->dst, "sntp-set: server: %s\n", string_to_cstr(&ipstring));

	return(app_action_normal);
}

static app_action_t application_function_multicast_group_set(app_params_t *parameters)
{
	unsigned int entry, entries;
	string_new(, ip, 32);

	if((parse_uint(1, parameters->src, &entry, 0, ' ') == parse_ok) && (parse_string(2, parameters->src, &ip, ' ') == parse_ok))
	{
		if(entry > 7)
		{
			string_append(parameters->dst, "entry must be between 0 and 7\n");
			return(app_action_error);
		}

		if(!config_open_write())
		{
			string_append(parameters->dst, "cannot set config (open)\n");
			return(app_action_error);
		}

		config_delete("multicast-group.%u", true, entry, -1);

		if(!string_match_cstr(&ip, "0.0.0.0"))
		{
			if(!config_set_string("multicast-group.%u", string_to_cstr(&ip), entry, -1))
			{
				config_abort_write();
				string_append(parameters->dst, "cannot set config (set multicast group entry)\n");
				return(app_action_error);
			}
		}

		if(!config_close_write())
		{
			string_append(parameters->dst, "cannot set config (close)\n");
			return(app_action_error);
		}

		wlan_multicast_init_groups();
	}

	for(entry = 0, entries = 0; entry < 8; entry++)
	{
		string_clear(&ip);

		if(config_get_string("multicast-group.%u", &ip, entry, -1))
		{
			entries++;
			string_format(parameters->dst, "> %u: %s\n", entry, string_to_cstr(&ip));
		}
	}

	string_format(parameters->dst, "%u entries\n", entries);

	return(app_action_normal);
}

static app_action_t application_function_broadcast_group_set(app_params_t *parameters)
{
	unsigned int group, old_groups, new_groups, enable;

	if((parse_uint(1, parameters->src, &group, 0, ' ') == parse_ok) && (parse_uint(2, parameters->src, &enable, 0, ' ') == parse_ok))
	{
		if(group > 7)
		{
			string_append(parameters->dst, "group must be between 0 and 15\n");
			return(app_action_error);
		}

		if(!config_get_uint("broadcast-groups", &old_groups, -1, -1))
			old_groups = 0x00;

		if(enable)
			new_groups = old_groups | (1 << group);
		else
			new_groups = old_groups & ~(1 << group);

		if(old_groups != new_groups)
		{
			if(!config_open_write())
			{
				string_append(parameters->dst, "cannot set config (open)\n");
				return(app_action_error);
			}

			config_delete("broadcast-groups", false, -1, -1);

			if(new_groups != 0)
			{
				if(!config_set_uint("broadcast-groups", new_groups, -1, -1))
				{
					config_abort_write();
					string_append(parameters->dst, "cannot set config (set broadcast group entry)\n");
					return(app_action_error);
				}
			}

			if(!config_close_write())
			{
				string_append(parameters->dst, "cannot set config (close)\n");
				return(app_action_error);
			}
		}
	}

	if(!config_get_uint("broadcast-groups", &old_groups, -1, -1))
		old_groups = 0x00;

	string_format(parameters->dst, "broadcast groups:");

	for(group = 0; group < 8; group++)
		if(old_groups & (1 << group))
			string_format(parameters->dst, " %u", group);

	string_append(parameters->dst, "\n");

	return(app_action_normal);
}

static app_action_t application_function_gpio_status_set(app_params_t *parameters)
{
	int trigger_io, trigger_pin;

	if((parse_int(1, parameters->src, &trigger_io, 0, ' ') == parse_ok) && (parse_int(2, parameters->src, &trigger_pin, 0, ' ') == parse_ok))
	{
		if((trigger_io < -1) || (trigger_io > io_id_size))
		{
			string_format(parameters->dst, "status trigger io %d/%d invalid\n", trigger_io, trigger_pin);
			return(app_action_error);
		}

		if(!config_open_write())
		{
			string_append(parameters->dst, "> cannot set config (open)\n");
			return(app_action_error);
		}

		if((trigger_io < 0) || (trigger_pin < 0))
		{
			if(!config_delete("trigger.status.io", false, -1, -1) ||
					!config_delete("trigger.status.pin", false, -1, -1))
			{
				config_abort_write();
				string_append(parameters->dst, "> cannot delete config (default values)\n");
				return(app_action_error);
			}
		}
		else
			if(!config_set_int("trigger.status.io", trigger_io, -1, -1) ||
					!config_set_int("trigger.status.pin", trigger_pin, -1, -1))
			{
				config_abort_write();
				string_append(parameters->dst, "> cannot set config\n");
				return(app_action_error);
			}

		if(!config_close_write())
		{
			string_append(parameters->dst, "> cannot set config (close)\n");
			return(app_action_error);
		}
	}

	if(!config_get_int("trigger.status.io", &trigger_io, -1, -1))
		trigger_io = -1;

	if(!config_get_int("trigger.status.pin", &trigger_pin, -1, -1))
		trigger_pin = -1;

	string_format(parameters->dst, "status trigger at io %d/%d (-1 is disabled)\n",
			trigger_io, trigger_pin);

	return(app_action_normal);
}

static app_action_t application_function_gpio_pcint_set(app_params_t *parameters)
{
	int trigger_io, trigger_pin;

	if((parse_int(1, parameters->src, &trigger_io, 0, ' ') == parse_ok) && (parse_int(2, parameters->src, &trigger_pin, 0, ' ') == parse_ok))
	{
		if((trigger_io < -1) || (trigger_io > io_id_size))
		{
			string_format(parameters->dst, "pcint trigger io %d/%d invalid\n", trigger_io, trigger_pin);
			return(app_action_error);
		}

		if(!config_open_write())
		{
			string_append(parameters->dst, "> cannot set config (open)\n");
			return(app_action_error);
		}

		if((trigger_io < 0) || (trigger_pin < 0))
		{
			if(!config_delete("trigger.pcint.io", false, -1, -1) ||
					!config_delete("trigger.pcint.pin", false, -1, -1))
			{
				config_abort_write();
				string_append(parameters->dst, "> cannot delete config (default values)\n");
				return(app_action_error);
			}
		}
		else
			if(!config_set_int("trigger.pcint.io", trigger_io, -1, -1) ||
					!config_set_int("trigger.pcint.pin", trigger_pin, -1, -1))
			{
				config_abort_write();
				string_append(parameters->dst, "> cannot set config\n");
				return(app_action_error);
			}

		if(!config_close_write())
		{
			string_append(parameters->dst, "> cannot set config (close)\n");
			return(app_action_error);
		}
	}

	if(!config_get_int("trigger.pcint.io", &trigger_io, -1, -1))
		trigger_io = -1;

	if(!config_get_int("trigger.pcint.pin", &trigger_pin, -1, -1))
		trigger_pin = -1;

	string_format(parameters->dst, "pcint trigger at io %d/%d (-1 is disabled)\n",
			trigger_io, trigger_pin);

	return(app_action_normal);
}

static app_action_t application_function_gpio_assoc_set(app_params_t *parameters)
{
	int trigger_io, trigger_pin;

	if((parse_int(1, parameters->src, &trigger_io, 0, ' ') == parse_ok) && (parse_int(2, parameters->src, &trigger_pin, 0, ' ') == parse_ok))
	{
		if(!config_open_write())
		{
			string_append(parameters->dst, "> cannot set config (open)\n");
			return(app_action_error);
		}

		if((trigger_io < -1) || (trigger_io > io_id_size))
		{
			string_format(parameters->dst, "association trigger io %d/%d invalid\n", trigger_io, trigger_pin);
			return(app_action_error);
		}

		if((trigger_io < 0) || (trigger_pin < 0))
		{
			if(!config_delete("trigger.assoc.io", false, -1, -1) ||
					!config_delete("trigger.assoc.pin", false, -1, -1))
			{
				config_abort_write();
				string_append(parameters->dst, "> cannot delete config (default values)\n");
				return(app_action_error);
			}
		}
		else
			if(!config_set_int("trigger.assoc.io", trigger_io, -1, -1) ||
					!config_set_int("trigger.assoc.pin", trigger_pin, -1, -1))
			{
				config_abort_write();
				string_append(parameters->dst, "> cannot set config\n");
				return(app_action_error);
			}

		if(!config_close_write())
		{
			string_append(parameters->dst, "> cannot set config (close)\n");
			return(app_action_error);
		}
	}

	if(!config_get_int("trigger.assoc.io", &trigger_io, -1, -1))
		trigger_io = -1;

	if(!config_get_int("trigger.assoc.pin", &trigger_pin, -1, -1))
		trigger_pin = -1;

	string_format(parameters->dst, "wlan association trigger at io %d/%d (-1 is disabled)\n",
			trigger_io, trigger_pin);

	return(app_action_normal);
}

static app_action_t application_function_pwm_width(app_params_t *parameters)
{
	unsigned int width;

	if(parse_uint(1, parameters->src, &width, 0, ' ') == parse_ok)
	{
		if(!io_gpio_pwm1_width_set(width, /*load*/false, /*save*/true))
		{
			string_format(parameters->dst, "pwm-width: invalid width: %u\n", width);
			return(app_action_error);
		}
	}

	string_format(parameters->dst, "pwm-width: %u\n", io_gpio_pwm1_width_get());

	return(app_action_normal);
}
static app_action_t application_function_peek(app_params_t *parameters)
{
	unsigned int address;

	if(parse_uint(1, parameters->src, &address, 16, ' ') != parse_ok)
	{
		string_append(parameters->dst, "> use peek <address>");
		return(app_action_error);
	}

	address &= ~0x03; // ensure proper alignment

	string_format(parameters->dst, "> peek (0x%x) = 0x%x\n", address, *(unsigned int *)address);

	return(app_action_normal);
}

static app_action_t application_function_poke(app_params_t *parameters)
{
	unsigned int address;
	unsigned int value;

	if((parse_uint(1, parameters->src, &address, 16, ' ') != parse_ok) || (parse_uint(2, parameters->src, &value, 16, ' ') != parse_ok))
	{
		string_append(parameters->dst, "> use poke <address> <value>");
		return(app_action_error);
	}

	address &= ~0x03; // ensure proper alignment

	*(uint32_t *)address = value;

	string_format(parameters->dst, "> poke (0x%x,0x%x) = 0x%x\n", address, value, *(unsigned int *)address);

	return(app_action_normal);
}

static app_action_t application_function_crypto_bench(app_params_t *parameters)
{
	unsigned int rounds, round;
	uint8_t *buffer1 = (uint8_t *)string_buffer_nonconst(parameters->src);
	uint8_t *buffer2 = (uint8_t *)string_buffer_nonconst(parameters->dst);
	uint64_t start, stop;
	unsigned int time_memcmp, time_ets_memcmp, time_memory_compare;
	unsigned int time_memcpy = 0, time_ets_memcpy = 0;
	unsigned int time_memcpy_dummy1a = 0, time_memcpy_dummy4a = 0;
	unsigned int time_memcpy_dummy1b = 0, time_memcpy_dummy4b = 0;
	int result_memcmp = 0, result_ets_memcmp = 0, result_memory_compare = 0;
	unsigned int sector;
	string_new(, digest_text, 64);
	unsigned int crc = 0;

	union
	{
		uint8_t byte[4];
		uint32_t word;
	} byte_ordering;

	assert_size(byte_ordering, 4);

	if((parse_uint(1, parameters->src, &sector, 0, ' ') != parse_ok))
	{
		string_append(parameters->dst, "> use crypto-bench <flash sector #> <rounds>\n");
		return(app_action_error);
	}

	if((parse_uint(2, parameters->src, &rounds, 0, ' ') != parse_ok))
	{
		string_append(parameters->dst, "> use crypto-bench <flash sector #> <rounds>\n");
		return(app_action_error);
	}

	spi_flash_read(sector * SPI_FLASH_SEC_SIZE, buffer1, SPI_FLASH_SEC_SIZE);

	start = time_get_us();
	for(round = 0; round < rounds; round++)
		ets_memcpy(buffer2, buffer1, SPI_FLASH_SEC_SIZE);
	stop = time_get_us();
	time_ets_memcpy = stop - start;

	start = time_get_us();
	for(round = 0; round < rounds; round++)
		memcpy(buffer2, buffer1, SPI_FLASH_SEC_SIZE);
	stop = time_get_us();
	time_memcpy = stop - start;

	start = time_get_us();
	for(round = 0; round < rounds; round++)
	{
		const uint32_t *a = (const unsigned int *)(const void *)buffer1;
		uint32_t *b = (unsigned int *)(void *)buffer2;
		for(unsigned int x = 0; x < (SPI_FLASH_SEC_SIZE / 4); x++)
			b[x] = a[x];
	}
	stop = time_get_us();
	time_memcpy_dummy4a = stop - start;

	start = time_get_us();
	for(round = 0; round < rounds; round++)
	{
		const uint32_t *a = (const unsigned int *)(const void *)buffer1;
		uint32_t *b = (unsigned int *)(void *)buffer2;
		for(unsigned int x = 0; x < (SPI_FLASH_SEC_SIZE / 4); x++)
			*b++ = *a++;
	}
	stop = time_get_us();
	time_memcpy_dummy4b = stop - start;

	start = time_get_us();
	for(round = 0; round < rounds; round++)
	{
		const uint8_t *a = (const uint8_t *)buffer1;
		uint8_t *b = (uint8_t *)buffer2;
		for(unsigned int x = 0; x < SPI_FLASH_SEC_SIZE; x++)
			b[x] = a[x];
	}
	stop = time_get_us();
	time_memcpy_dummy1a = stop - start;

	start = time_get_us();
	for(round = 0; round < rounds; round++)
	{
		const uint8_t *a = (const uint8_t *)buffer1;
		uint8_t *b = (uint8_t *)buffer2;
		for(unsigned int x = 0; x < SPI_FLASH_SEC_SIZE; x++)
			*b++ = *a++;
	}
	stop = time_get_us();
	time_memcpy_dummy1b = stop - start;

	start = time_get_us();
	for(round = 0; round < rounds; round++)
		result_ets_memcmp = ets_memcmp(buffer1, buffer2, SPI_FLASH_SEC_SIZE);
	stop = time_get_us();
	time_ets_memcmp = stop - start;

	start = time_get_us();
	for(round = 0; round < rounds; round++)
		result_memcmp = memcmp(buffer1, buffer2, SPI_FLASH_SEC_SIZE);
	stop = time_get_us();
	time_memcmp = stop - start;

	start = time_get_us();
	for(round = 0; round < rounds; round++)
		result_memory_compare = memory_compare(SPI_FLASH_SEC_SIZE, buffer1, buffer2);
	stop = time_get_us();
	time_memory_compare = stop - start;

	string_clear(parameters->dst);

	byte_ordering.word = 0x01234567;
	string_format(parameters->dst, "> byte ordering: %02x:%02x:%02x:%02x\n", byte_ordering.byte[0], byte_ordering.byte[1], byte_ordering.byte[2], byte_ordering.byte[3]);
	string_format(parameters->dst, "> cryptograpic benchmark, sector %u, %u rounds\n", sector, rounds);
	string_append(parameters->dst, "> \n");
	string_append(parameters->dst, "> memory copy\n");
	string_format(parameters->dst, ">    %6u %-16s\n", time_ets_memcpy, "ets_memcpy");
	string_format(parameters->dst, ">    %6u %-16s\n", time_memcpy, "memcpy");
	string_format(parameters->dst, ">    %6u %-16s\n", time_memcpy_dummy4a, "dummy4a");
	string_format(parameters->dst, ">    %6u %-16s\n", time_memcpy_dummy4b, "dummy4b");
	string_format(parameters->dst, ">    %6u %-16s\n", time_memcpy_dummy1a, "dummy1a");
	string_format(parameters->dst, ">    %6u %-16s\n", time_memcpy_dummy1b, "dummy1b");
	string_append(parameters->dst, "> \n");
	string_append(parameters->dst, "> memory compare, different pointers\n");
	string_format(parameters->dst, ">    %6u %-16s = %d\n", time_ets_memcmp, "ets_memcmp", result_ets_memcmp);
	string_format(parameters->dst, ">    %6u %-16s = %d\n", time_memcmp, "memcmp", result_memcmp);
	string_format(parameters->dst, ">    %6u %-16s = %d\n", time_memory_compare, "memory_compare", result_memory_compare);
	string_append(parameters->dst, "> \n");
	string_append(parameters->dst, "> memory compare, same pointers\n");

	start = time_get_us();
	for(round = 0; round < rounds; round++)
		result_ets_memcmp = ets_memcmp(buffer1, buffer1, SPI_FLASH_SEC_SIZE);
	stop = time_get_us();
	string_format(parameters->dst, ">    %6u %-16s = %d\n", (unsigned int)(stop - start), "ets_memcmp", result_ets_memcmp);

	start = time_get_us();
	for(round = 0; round < rounds; round++)
		result_memcmp = memcmp(buffer1, buffer1, SPI_FLASH_SEC_SIZE);
	stop = time_get_us();
	string_format(parameters->dst, ">    %6u %-16s = %d\n", (unsigned int)(stop - start), "memcmp", result_memcmp);

	start = time_get_us();
	for(round = 0; round < rounds; round++)
		result_memory_compare = memory_compare(SPI_FLASH_SEC_SIZE, buffer1, buffer1);
	stop = time_get_us();
	string_format(parameters->dst, ">    %6u %-16s = %d\n", (unsigned int)(stop - start), "memory_compare", result_memory_compare);

	start = time_get_us();
	for(round = 0; round < rounds; round++)
	{
		string_clear(&digest_text);
		MD5_text(buffer1, SPI_FLASH_SEC_SIZE, &digest_text);
	}

	stop = time_get_us();
	string_append(parameters->dst, ">\n");
	string_append(parameters->dst, "> hashing functions\n");
	string_format(parameters->dst, ">    %6u %-16s = %s (%d) [trunc32 = 0x%04x]\n",
			(unsigned int)(stop - start), "md5", string_to_cstr(&digest_text), string_length(&digest_text), MD5_trunc_32(SPI_FLASH_SEC_SIZE, buffer1));

	start = time_get_us();
	for(round = 0; round < rounds; round++)
	{
		string_clear(&digest_text);
		SHA1_text(buffer1, SPI_FLASH_SEC_SIZE, &digest_text);
	}
	stop = time_get_us();
	string_format(parameters->dst, ">    %6u %-16s = %s (%d)\n", (unsigned int)(stop - start), "sha1", string_to_cstr(&digest_text), string_length(&digest_text));

	start = time_get_us();
	for(round = 0; round < rounds; round++)
		crc = crc16(SPI_FLASH_SEC_SIZE, buffer1);
	stop = time_get_us();
	string_format(parameters->dst, ">    %6u %-16s = %04x (%d)\n", (unsigned int)(stop - start), "crc16", crc, 2);

	return(app_action_normal);
}

roflash static const char help_description_help[] =					"help [command]";
roflash static const char help_description_quit[] =					"quit";
roflash static const char help_description_reset[] =				"reset";
roflash static const char help_description_identification[] =		"hostname/identification/location/comment";
roflash static const char help_description_statistics[] =			"generic info and statistics";
roflash static const char help_description_stats_flash[] =			"statistics about flash use";
roflash static const char help_description_stats_counters[] =		"statistics from counters";
roflash static const char help_description_stats_lwip[] =			"statistics from lwip";
roflash static const char help_description_stats_i2c[] =			"statistics from i2c subsystem";
roflash static const char help_description_stats_sequencer[] =		"statistics from the sequencer";
roflash static const char help_description_stats_time[] =			"statistics from the time subsystem";
roflash static const char help_description_stats_uart[] =			"statistics from the uarts";
roflash static const char help_description_bridge_port[] =			"set uart bridge tcp/udp port (default 23)";
roflash static const char help_description_command_port[] =			"set command tcp/udp port (default 24)";
roflash static const char help_description_dump_config[] =			"dump config contents (as stored in flash)";
roflash static const char help_description_display_brightness[] =	"set or show display brightness";
roflash static const char help_description_display_dump[] =			"shows display and contents";
roflash static const char help_description_display_flip[] =			"set the time between flipping of the slots";
roflash static const char help_description_display_set[] =			"put content on display <slot> <timeout> <tag> <text>";
roflash static const char help_description_display_picture_load[] =	"load picture [<entry> (0|1)]";
roflash static const char help_description_display_freeze[] =		"freeze display <timeout_ms>";
roflash static const char help_description_set_flag[] =				"set a flag";
roflash static const char help_description_unset_flag[] =			"unset a flag";
roflash static const char help_description_gpio_assoc[] =			"set gpio to trigger on wlan association";
roflash static const char help_description_gpio_status[] =			"set gpio to trigger on status change";
roflash static const char help_description_gpio_pcint[] =			"set gpio to trigger on pin change interrupt";
roflash static const char help_description_i2c_address[] =			"set i2c slave address";
roflash static const char help_description_i2c_bus[] =				"set i2c mux bus number (0-8)";
roflash static const char help_description_i2c_read[] =				"read data from i2c slave";
roflash static const char help_description_i2c_speed[] =			"set i2c bus speed, 1000 (default) is 100 kHz, 0 is unconstrained";
roflash static const char help_description_i2c_write[] =			"write data to i2c slave";
roflash static const char help_description_i2c_write_read[] =		"write data to i2c slave and read back data";
roflash static const char help_description_io_mode[] =				"config i/o pin";
roflash static const char help_description_io_read[] =				"read from i/o pin";
roflash static const char help_description_io_trigger[] = 			"trigger i/o pin";
roflash static const char help_description_trigger_remote[] = 		"remote trigger: <index> <ip>";
roflash static const char help_description_io_write[] =				"write to i/o pin";
roflash static const char help_description_io_multiple[] =			"write to multiple pins from one I/O";
roflash static const char help_description_io_set_flag[] =			"set i/o pin flag";
roflash static const char help_description_pwm1_width[] =			"set pwm1 width";
roflash static const char help_description_io_clear_flag[] =		"clear i/o pin flag";
roflash static const char help_description_i2c_sensor_read[] =		"read from i2c sensor";
roflash static const char help_description_i2c_sensor_calibrate[] =	"calibrate i2c sensor, use sensor factor offset";
roflash static const char help_description_i2c_sensor_dump[] =		"dump all i2c sensors";
roflash static const char help_description_log_display[] =			"display log";
roflash static const char help_description_log_clear[] =			"display and clear the log";
roflash static const char help_description_log_write[] =			"write to the log";
roflash static const char help_description_multicast_group_set[] =	"set multicast group, entry 0-7, ip address";
roflash static const char help_description_broadcast_group_set[] =	"set broadcast group, entry 0-15, 0=disable 1=enable";
roflash static const char help_description_sntp_set[] =				"set sntp <ip addr>";
roflash static const char help_description_time_set[] =				"set time base using <h> <m> [<s>]";
roflash static const char help_description_time_stamp_set[] =		"set time base using unix epoch timestamp";
roflash static const char help_description_time_zone_set[] =		"set time zone <+/- hours>";
roflash static const char help_description_sequencer_add[] =		"add sequencer entry";
roflash static const char help_description_sequencer_clear[] =		"clear sequencer";
roflash static const char help_description_sequencer_list[] =		"list sequencer entries";
roflash static const char help_description_sequencer_remove[] =		"remove sequencer entry";
roflash static const char help_description_sequencer_start[] =		"start sequencer";
roflash static const char help_description_sequencer_stop[] =		"stop sequencer";
roflash static const char help_description_uart_baud[] =			"set uart baud rate [1-1000000]";
roflash static const char help_description_uart_data[] =			"set uart data bits [5/6/7/8]";
roflash static const char help_description_uart_stop[] =			"set uart stop bits [1/2]";
roflash static const char help_description_uart_parity[] =			"set uart parity [none/even/odd]";
roflash static const char help_description_uart_loopback[] =		"set uart loopback mode [0/1]";
roflash static const char help_description_uart_write[] =			"write text to uart";
roflash static const char help_description_uart_read[] =			"read text from uart";
roflash static const char help_description_uart_write_hex[] =		"write hex bytes to uart";
roflash static const char help_description_uart_read_hex[] =		"read hex bytes from uart";
roflash static const char help_description_config_query_string[] =	"query config string";
roflash static const char help_description_config_query_int[] =		"query config int";
roflash static const char help_description_config_set[] =			"set config entry";
roflash static const char help_description_config_delete[] =		"delete config entry";

roflash static const application_function_table_t application_function_table[] =
{
	{
		"?", "help",
		application_function_help,
		help_description_help,
	},
	{
		"q", "quit",
		application_function_quit,
		help_description_quit,
	},
	{
		"r", "reset",
		application_function_reset,
		help_description_reset,
	},
	{
		"id", "identification",
		application_function_identification,
		help_description_identification,
	},
	{
		"s", "stats",
		application_function_stats_firmware,
		help_description_statistics,
	},
	{
		"sf", "stats-flash",
		application_function_stats_flash,
		help_description_stats_flash,
	},
	{
		"sc", "stats-counters",
		application_function_stats_counters,
		help_description_stats_counters,
	},
	{
		"sl", "stats-lwip",
		application_function_stats_lwip,
		help_description_stats_lwip,
	},
	{
		"si", "stats-i2c",
		application_function_stats_i2c,
		help_description_stats_i2c,
	},
	{
		"ss", "stats-sequencer",
		application_function_stats_sequencer,
		help_description_stats_sequencer,
	},
	{
		"st", "stats-time",
		application_function_stats_time,
		help_description_stats_time,
	},
	{
		"sw", "stats-wlan",
		application_function_stats_wlan,
		help_description_stats_wlan,
	},
	{
		"su", "stats-uart",
		application_function_stats_uart,
		help_description_stats_uart,
	},
	{
		"bp", "bridge-port",
		application_function_bridge_port,
		help_description_bridge_port,
	},
	{
		"cp", "command-port",
		application_function_command_port,
		help_description_command_port,
	},
	{
		"cd", "config-dump",
		application_function_config_dump,
		help_description_dump_config,
	},
	{
		"db", "display-brightness",
		application_function_display_brightness,
		help_description_display_brightness,
	},
	{
		"dd", "display-dump",
		application_function_display_dump,
		help_description_display_dump,
	},
	{
		"dfs", "display-font-select",
		application_function_display_font_select,
		help_description_display_font_select,
	},
	{
		"dft", "display-flip-timeout",
		application_function_display_flip_timeout,
		help_description_display_flip,
	},
	{
		"ds", "display-set",
		application_function_display_set,
		help_description_display_set,
	},
	{
		"dpl", "display-picture-load",
		application_function_display_picture_load,
		help_description_display_picture_load,
	},
	{
		"df", "display-freeze",
		application_function_display_freeze,
		help_description_display_freeze,
	},
	{
		"fs", "flag-set",
		application_function_flag_set,
		help_description_set_flag,
	},
	{
		"fu", "flag-unset",
		application_function_flag_unset,
		help_description_unset_flag,
	},
	{
		"gas", "gpio-association-set",
		application_function_gpio_assoc_set,
		help_description_gpio_assoc,
	},
	{
		"gss", "gpio-status-set",
		application_function_gpio_status_set,
		help_description_gpio_status,
	},
	{
		"gps", "gpio-pcint-set",
		application_function_gpio_pcint_set,
		help_description_gpio_pcint,
	},
	{
		"i2a", "i2c-address",
		application_function_i2c_address,
		help_description_i2c_address,
	},
	{
		"i2b", "i2c-bus",
		application_function_i2c_bus,
		help_description_i2c_bus,
	},
	{
		"i2r", "i2c-read",
		application_function_i2c_read,
		help_description_i2c_read,
	},
	{
		"i2s", "i2c-speed",
		application_function_i2c_speed,
		help_description_i2c_speed,
	},
	{
		"i2w", "i2c-write",
		application_function_i2c_write,
		help_description_i2c_write,
	},
	{
		"i2wr", "i2c-write-read",
		application_function_i2c_write_read,
		help_description_i2c_write_read,
	},
	{
		"spc", "spi-configure",
		application_function_spi_configure,
		help_description_spi_configure,
	},
	{
		"sps", "spi-start",
		application_function_spi_start,
		help_description_spi_start,
	},
	{
		"spw", "spi-write",
		application_function_spi_write,
		help_description_spi_write,
	},
	{
		"spt", "spi-transmit",
		application_function_spi_transmit,
		help_description_spi_transmit,
	},
	{
		"spr", "spi-receive",
		application_function_spi_receive,
		help_description_spi_receive,
	},
	{
		"spf", "spi-finish",
		application_function_spi_finish,
		help_description_spi_finish,
	},
	{
		"im", "io-mode",
		application_function_io_mode,
		help_description_io_mode,
	},
	{
		"ir", "io-read",
		application_function_io_read,
		help_description_io_read,
	},
	{
		"it", "io-trigger",
		application_function_io_trigger,
		help_description_io_trigger,
	},
	{
		"itr", "io-trigger-remote",
		application_function_trigger_remote,
		help_description_trigger_remote,
	},
	{
		"iw", "io-write",
		application_function_io_write,
		help_description_io_write,
	},
	{
		"ism", "io-set-mask",
		application_function_io_set_mask,
		help_description_io_multiple,
	},
	{
		"isf", "io-set-flag",
		application_function_io_set_flag,
		help_description_io_set_flag,
	},
	{
		"pw", "pwm-width",
		application_function_pwm_width,
		help_description_pwm1_width,
	},
	{
		"icf", "io-clear-flag",
		application_function_io_clear_flag,
		help_description_io_clear_flag,
	},
	{
		"isr", "i2c-sensor-read",
		application_function_i2c_sensor_read,
		help_description_i2c_sensor_read,
	},
	{
		"isc", "i2c-sensor-calibrate",
		application_function_i2c_sensor_calibrate,
		help_description_i2c_sensor_calibrate,
	},
	{
		"isd", "i2c-sensor-dump",
		application_function_i2c_sensor_dump,
		help_description_i2c_sensor_dump,
	},
	{
		"l", "log-display",
		application_function_log_display,
		help_description_log_display,
	},
	{
		"lc", "log-clear",
		application_function_log_clear,
		help_description_log_clear,
	},
	{
		"lw", "log-write",
		application_function_log_write,
		help_description_log_write,
	},
	{
		"mgs", "multicast-group-set",
		application_function_multicast_group_set,
		help_description_multicast_group_set,
	},
	{
		"bgs", "broadcast-group-set",
		application_function_broadcast_group_set,
		help_description_broadcast_group_set,
	},
	{
		"sns", "sntp-set",
		application_function_sntp_set,
		help_description_sntp_set,
	},
	{
		"ts", "time-set",
		application_function_time_set,
		help_description_time_set,
	},
	{
		"tss", "time-stamp-set",
		application_function_time_stamp_set,
		help_description_time_stamp_set,
	},
	{
		"tzs", "time-zone-set",
		application_function_time_zone_set,
		help_description_time_zone_set,
	},
	{
		"sea", "sequencer-add",
		application_function_sequencer_add,
		help_description_sequencer_add,
	},
	{
		"sec", "sequencer-clear",
		application_function_sequencer_clear,
		help_description_sequencer_clear,
	},
	{
		"sel", "sequencer-list",
		application_function_sequencer_list,
		help_description_sequencer_list,
	},
	{
		"ser", "sequencer-remove",
		application_function_sequencer_remove,
		help_description_sequencer_remove,
	},
	{
		"ses", "sequencery-start",
		application_function_sequencer_start,
		help_description_sequencer_start,
	},
	{
		"set", "sequencery-stop",
		application_function_sequencer_stop,
		help_description_sequencer_stop,
	},
	{
		"ub", "uart-baud",
		application_function_uart_baud_rate,
		help_description_uart_baud,
	},
	{
		"ud", "uart-data",
		application_function_uart_data_bits,
		help_description_uart_data,
	},
	{
		"us", "uart-stop",
		application_function_uart_stop_bits,
		help_description_uart_stop,
	},
	{
		"up", "uart-parity",
		application_function_uart_parity,
		help_description_uart_parity,
	},
	{
		"ul", "uart-loopback",
		application_function_uart_loopback,
		help_description_uart_loopback,
	},
	{
		"ur", "uart-read",
		application_function_uart_read,
		help_description_uart_read,
	},
	{
		"uw", "uart-write",
		application_function_uart_write,
		help_description_uart_write,
	},
	{
		"urh", "uart-read-hex",
		application_function_uart_read_hex,
		help_description_uart_read_hex,
	},
	{
		"uwh", "uart-write-hex",
		application_function_uart_write_hex,
		help_description_uart_write_hex,
	},
	{
		"wac", "wlan-ap-configure",
		application_function_wlan_ap_configure,
		help_description_wlan_ap_config,
	},
	{
		"wcc", "wlan-client-configure",
		application_function_wlan_client_configure,
		help_description_wlan_client_config,
	},
	{
		"wm", "wlan-mode",
		application_function_wlan_mode,
		help_description_wlan_mode,
	},
	{
		"ws", "wlan-scan",
		application_function_wlan_scan,
		help_description_wlan_scan,
	},
	{
		"cqs", "config-query-string",
		application_function_config_query_string,
		help_description_config_query_string,
	},
	{
		"cqi", "config-query-int",
		application_function_config_query_int,
		help_description_config_query_int,
	},
	{
		"cs", "config-set",
		application_function_config_set,
		help_description_config_set,
	},
	{
		"cde", "config-delete",
		application_function_config_delete,
		help_description_config_delete,
	},
	{
		"GET", "http-get",
		application_function_http_get,
		(void *)0,
	},
	{
		"de", "display-eastrising",
		application_function_display_eastrising,
		help_description_display_eastrising,
	},
	{
		"dss", "display-ssd1306",
		application_function_display_ssd1306,
		help_description_display_ssd1306,
	},
	{
		"dt", "display-spitft",
		application_function_display_spitft,
		help_description_display_spitft,
	},
	{
		"dc6", "display-cfa-634",
		application_function_display_cfa634,
		help_description_display_cfa634,
	},
	{
		"do", "display-orbital",
		application_function_display_orbital,
		help_description_display_orbital,
	},
	{
		"dp", "display-plot",
		application_function_display_plot,
		(void *)0,
	},
	{
		"ims", "io-mcp-spi",
		application_function_io_mcp_spi,
		help_description_io_mcp_spi,
	},
	{
		"flash-info", "flash-info",
		application_function_flash_info,
		(void *)0,
	},
	{
		"flash-read", "flash-read",
		application_function_flash_read,
		(void *)0,
	},
	{
		"flash-write", "flash-write",
		application_function_flash_write,
		(void *)0,
	},
	{
		"flash-checksum", "flash-checksum",
		application_function_flash_checksum,
		(void *)0,
	},
	{
		"flash-bench", "flash-bench",
		application_function_flash_bench,
		(void *)0,
	},
	{
		"flash-select", "flash-select",
		application_function_flash_select,
		(void *)0,
	},
	{
		"cb", "crypto-bench",
		application_function_crypto_bench,
		(void *)0,
	},
	{
		"pe", "peek",
		application_function_peek,
		(void *)0,
	},
	{
		"po", "poke",
		application_function_poke,
		(void *)0,
	},
	{
		(void *)0, (void *)0,
		(void *)0,
		(void *)0,
	},
};
