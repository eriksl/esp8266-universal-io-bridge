#include "util.h"
#include "application.h"
#include "stats.h"
#include "user_main.h"
#include "config.h"
#include "uart.h"
#include "i2c.h"
#include "i2c_sensor.h"
#include "display.h"
#include "http.h"
#include "io.h"
#include "io_gpio.h"
#include "time.h"
#include "ota.h"

#include <user_interface.h>
#include <c_types.h>
#include <sntp.h>

#include <stdlib.h>

typedef struct
{
	const char		*command1;
	const char		*command2;
	app_action_t	(*function)(const string_t *, string_t *);
	const char		*description;
} application_function_table_t;

static const application_function_table_t application_function_table[];

irom app_action_t application_content(const string_t *src, string_t *dst)
{
	string_init(varname_io, "trigger.status.io");
	string_init(varname_pin, "trigger.status.pin");

	const application_function_table_t *tableptr;
	int status_io, status_pin;

	if(config_get_int(&varname_io, -1, -1, &status_io) &&
			config_get_int(&varname_pin, -1, -1, &status_pin) &&
			(status_io != -1) && (status_pin != -1))
	{
		io_trigger_pin((string_t *)0, status_io, status_pin, io_trigger_on);
	}

	if(parse_string(0, src, dst, ' ') != parse_ok)
		return(app_action_empty);

	for(tableptr = application_function_table; tableptr->function; tableptr++)
		if(string_match_cstr(dst, tableptr->command1) ||
				string_match_cstr(dst, tableptr->command2))
			break;

	if(tableptr->function)
	{
		string_clear(dst);
		return(tableptr->function(src, dst));
	}

	string_append(dst, ": command unknown\n");
	return(app_action_error);
}

irom static app_action_t application_function_config_dump(const string_t *src, string_t *dst)
{
	config_dump(dst);
	return(app_action_normal);
}

irom static app_action_t application_function_config_write(const string_t *src, string_t *dst)
{
	unsigned int size;

	if((size = config_write()) == 0)
	{
		string_append(dst, "> failed\n");
		return(app_action_error);
	}

	string_format(dst, "> config write done, space used: %u, free: %u\n", size, SPI_FLASH_SEC_SIZE - size);
	return(app_action_normal);
}

irom static app_action_t application_function_config_query_int(const string_t *src, string_t *dst)
{
	int index1, index2;
	int value;

	string_clear(dst);

	if(parse_string(1, src, dst, ' ') != parse_ok)
		return(app_action_error);

	if(parse_int(2, src, &index1, 0, ' ') != parse_ok)
		index1 = -1;
	else
		if(parse_int(3, src, &index2, 0, ' ') != parse_ok)
			index2 = -1;

	if(!config_get_int(dst, index1, index2, &value))
	{
		string_clear(dst);
		string_append(dst, "ERROR\n");
		return(app_action_error);
	}

	string_format(dst, "=%d OK\n", value);

	return(app_action_normal);
}

irom static app_action_t application_function_config_query_string(const string_t *src, string_t *dst)
{
	string_new(stack, varid, 64);
	int index1, index2;

	if(parse_string(1, src, &varid, ' ') != parse_ok)
	{
		string_clear(dst);
		string_append(dst, "missing variable name\n");
		return(app_action_error);
	}

	if(parse_int(2, src, &index1, 0, ' ') != parse_ok)
		index1 = -1;
	else
		if(parse_int(3, src, &index2, 0, ' ') != parse_ok)
			index2 = -1;

	string_clear(dst);
	string_append_string(dst, &varid);
	string_append(dst, "=");

	if(!config_get_string(&varid, index1, index2, dst))
	{
		string_clear(dst);
		string_append(dst, "ERROR\n");
		return(app_action_error);
	}

	string_append(dst, " OK\n");

	return(app_action_normal);
}

irom static app_action_t application_function_config_set(const string_t *src, string_t *dst)
{
	int index1, index2, offset;
	string_new(stack, varid, 64);

	if(parse_string(1, src, &varid, ' ') != parse_ok)
	{
		string_append(dst, "missing variable name\n");
		return(app_action_error);
	}

	if(parse_int(2, src, &index1, 0, ' ') != parse_ok)
	{
		string_append(dst, "missing index1\n");
		return(app_action_error);
	}

	if(parse_int(3, src, &index2, 0, ' ') != parse_ok)
	{
		string_append(dst, "missing index2\n");
		return(app_action_error);
	}

	if((offset = string_sep(src, 0, 4, ' ')) < 0)
	{
		string_append(dst, "missing variable value\n");
		return(app_action_error);
	}

	if(!config_set_string(&varid, index1, index2, src, offset, -1))
	{
		string_append(dst, "ERROR\n");
		return(app_action_error);
	}

	string_append(dst, "OK\n");

	return(app_action_normal);
}

irom static app_action_t application_function_config_delete(const string_t *src, string_t *dst)
{
	int index1, index2, wildcard;
	string_new(stack, varid, 64);

	if(parse_string(1, src, &varid, ' ') != parse_ok)
	{
		string_clear(dst);
		string_append(dst, "missing variable name\n");
		return(app_action_error);
	}

	if(parse_int(2, src, &index1, 0, ' ') != parse_ok)
		index1 = -1;

	if(parse_int(3, src, &index2, 0, ' ') != parse_ok)
		index2 = -1;

	if(parse_int(4, src, &wildcard, 0, ' ') != parse_ok)
		wildcard = 0;

	index1 = config_delete(&varid, index1, index2, wildcard != 0);

	string_format(dst, "%u config entries deleted\n", index1);

	return(app_action_normal);
}

irom static app_action_t application_function_help(const string_t *src, string_t *dst)
{
	const application_function_table_t *tableptr;

	for(tableptr = application_function_table; tableptr->function; tableptr++)
		string_format(dst, "> %s/%s: %s\n",
				tableptr->command1, tableptr->command2,
				tableptr->description);

	return(app_action_normal);
}

irom static app_action_t application_function_identification(const string_t *src, string_t *dst)
{
	int start, length;
	char last;
	string_init(varname_identification, "identification");

	if((start = string_sep(src, 0, 1, ' ')) > 0)
	{
		length = string_length(src) - 1;

		if(length > 0)
		{
			last = string_at(src, length - 1);

			if((last == '\n') || (last == '\r'))
				length--;
		}

		if(!config_set_string(&varname_identification, -1, -1, src, start, length - start))
		{
			string_append(dst, "> cannot set identification\n");
			return(app_action_error);
		}
	}

	string_clear(dst);

	if(config_get_string(&varname_identification, -1, -1, dst) && string_empty(dst))
		config_delete(&varname_identification, -1, -1, false);

	string_clear(dst);
	string_append(dst, "identification is \"");

	if(!config_get_string(&varname_identification, -1, -1, dst))
		string_append(dst, "<unset>");

	string_append(dst, "\"\n");

	return(app_action_normal);
}

irom static app_action_t application_function_quit(const string_t *src, string_t *dst)
{
	return(app_action_disconnect);
}

irom static app_action_t application_function_reset(const string_t *src, string_t *dst)
{
	return(app_action_reset);
}

irom static app_action_t application_function_stats_firmware(const string_t *src, string_t *dst)
{
	stats_firmware(dst);
	return(app_action_normal);
}

irom static app_action_t application_function_stats_time(const string_t *src, string_t *dst)
{
	stats_time(dst);
	return(app_action_normal);
}

irom static app_action_t application_function_stats_counters(const string_t *src, string_t *dst)
{
	stats_counters(dst);
	return(app_action_normal);
}

irom static app_action_t application_function_stats_i2c(const string_t *src, string_t *dst)
{
	stats_i2c(dst);
	return(app_action_normal);
}

irom static app_action_t application_function_stats_wlan(const string_t *src, string_t *dst)
{
	stats_wlan(dst);
	return(app_action_normal);
}

irom static app_action_t application_function_bridge_port(const string_t *src, string_t *dst)
{
	int port;
	string_init(varname_bridgeport, "bridge.port");

	if(parse_int(1, src, &port, 0, ' ') == parse_ok)
	{
		if((port < 0) || (port > 65535))
		{
			string_format(dst, "> invalid port %d\n", port);
			return(app_action_error);
		}

		if(port == 0)
			config_delete(&varname_bridgeport, -1, -1, false);
		else
			if(!config_set_int(&varname_bridgeport, -1, -1, port))
			{
				string_append(dst, "> cannot set config\n");
				return(app_action_error);
			}
	}

	if(!config_get_int(&varname_bridgeport, -1, -1, &port))
		port = 0;

	string_format(dst, "> port: %d\n", port);

	return(app_action_normal);
}

irom static app_action_t application_function_bridge_timeout(const string_t *src, string_t *dst)
{
	string_init(varname_bridgetimeout, "bridge.timeout");
	int timeout;

	if(parse_int(1, src, &timeout, 0, ' ') == parse_ok)
	{
		if((timeout < 0) || (timeout > 65535))
		{
			string_format(dst, "> invalid timeout: %d\n", timeout);
			return(app_action_error);
		}

		if(timeout == 90)
			config_delete(&varname_bridgetimeout, -1, -1, false);
		else
			if(!config_set_int(&varname_bridgetimeout, -1, -1, timeout))
			{
				string_append(dst, "> cannot set config\n");
				return(app_action_error);
			}
	}

	if(!config_get_int(&varname_bridgetimeout, -1, -1, &timeout))
		timeout = 90;

	string_format(dst, "> timeout: %d\n", timeout);

	return(app_action_normal);
}

irom static app_action_t application_function_command_port(const string_t *src, string_t *dst)
{
	string_init(varname_cmdport, "cmd.port");
	int port;

	if(parse_int(1, src, &port, 0, ' ') == parse_ok)
	{
		if((port < 1) || (port > 65535))
		{
			string_format(dst, "> invalid port %d\n", port);
			return(app_action_error);
		}

		if(port == 24)
			config_delete(&varname_cmdport, -1, -1, false);
		else
			if(!config_set_int(&varname_cmdport, -1, -1, port))
			{
				string_append(dst, "> cannot set config\n");
				return(app_action_error);
			}
	}

	if(!config_get_int(&varname_cmdport, -1, -1, &port))
		port = 24;

	string_format(dst, "> port: %d\n", port);

	return(app_action_normal);
}

irom static app_action_t application_function_command_timeout(const string_t *src, string_t *dst)
{
	string_init(varname_cmdtimeout, "cmd.timeout");
	int timeout;

	if(parse_int(1, src, &timeout, 0, ' ') == parse_ok)
	{
		if((timeout < 0) || (timeout > 65535))
		{
			string_format(dst, "> invalid timeout: %d\n", timeout);
			return(app_action_error);
		}

		if(timeout == 90)
			config_delete(&varname_cmdtimeout, -1, -1, false);
		else
			if(!config_set_int(&varname_cmdtimeout, -1, -1, timeout))
			{
				string_append(dst, "> cannot set config\n");
				return(app_action_error);
			}
	}

	if(!config_get_int(&varname_cmdtimeout, -1, -1, &timeout))
		timeout = 90;

	string_format(dst, "> timeout: %d\n", timeout);

	return(app_action_normal);
}

irom static app_action_t application_function_uart_baud_rate(const string_t *src, string_t *dst)
{
	string_init(varname_baudrate, "uart.baud");
	int baud_rate;

	if(parse_int(1, src, &baud_rate, 0, ' ') == parse_ok)
	{
		if((baud_rate < 150) || (baud_rate > 1000000))
		{
			string_format(dst, "> invalid baud rate: %d\n", baud_rate);
			return(app_action_error);
		}

		if(baud_rate == 9600)
			config_delete(&varname_baudrate, -1, -1, false);
		else
			if(!config_set_int(&varname_baudrate, -1, -1, baud_rate))
			{
				string_append(dst, "> cannot set config\n");
				return(app_action_error);
			}
	}

	if(!config_get_int(&varname_baudrate, -1, -1, &baud_rate))
		baud_rate = 9600;

	string_format(dst, "> baudrate: %d\n", baud_rate);

	return(app_action_normal);
}

irom static app_action_t application_function_uart_data_bits(const string_t *src, string_t *dst)
{
	int data_bits;
	string_init(varname_uartbits, "uart.bits");

	if(parse_int(1, src, &data_bits, 0, ' ') == parse_ok)
	{
		if((data_bits < 5) || (data_bits > 8))
		{
			string_format(dst, "> invalid data bits: %d\n", data_bits);
			return(app_action_error);
		}

		if(data_bits == 8)
			config_delete(&varname_uartbits, -1, -1, false);
		else
			if(!config_set_int(&varname_uartbits, -1, -1, data_bits))
			{
				string_append(dst, "> cannot set config\n");
				return(app_action_error);
			}
	}

	if(!config_get_int(&varname_uartbits, -1, -1, &data_bits))
		data_bits = 8;

	string_format(dst, "data bits: %d\n", data_bits);

	return(app_action_normal);
}

irom static app_action_t application_function_uart_stop_bits(const string_t *src, string_t *dst)
{
	int stop_bits;
	string_init(varname_stopbits, "uart.stop");

	if(parse_int(1, src, &stop_bits, 0, ' ') == parse_ok)
	{
		if((stop_bits < 1) || (stop_bits > 2))
		{
			string_format(dst, "> stop bits out of range: %d\n", stop_bits);
			return(app_action_error);
		}

		if(stop_bits == 1)
			config_delete(&varname_stopbits, -1, -1, false);
		else
			if(!config_set_int(&varname_stopbits, -1, -1, stop_bits))
			{
				string_append(dst, "> cannot set config\n");
				return(app_action_error);
			}
	}

	if(!config_get_int(&varname_stopbits, -1, -1, &stop_bits))
		stop_bits = 1;

	string_format(dst, "> stop bits: %d\n", stop_bits);

	return(app_action_normal);
}

irom static app_action_t application_function_uart_parity(const string_t *src, string_t *dst)
{
	uart_parity_t parity;
	int parity_int;
	string_init(varname_parity, "uart.parity");

	if(parse_string(1, src, dst, ' ') == parse_ok)
	{
		parity = uart_string_to_parity(dst);

		if((parity < parity_none) || (parity >= parity_error))
		{
			string_append(dst, ": invalid parity\n");
			return(app_action_error);
		}

		if(parity == parity_none)
			config_delete(&varname_parity, -1, -1, false);
		else
		{
			parity_int = (int)parity;

			if(!config_set_int(&varname_parity, -1, -1, parity_int))
			{
				string_append(dst, "> cannot set config\n");
				return(app_action_error);
			}
		}
	}

	if(config_get_int(&varname_parity, -1, -1, &parity_int))
		parity = (uart_parity_t)parity_int;
	else
		parity = parity_none;

	string_clear(dst);
	string_append(dst, "parity: ");
	uart_parity_to_string(dst, parity);
	string_append(dst, "\n");

	return(app_action_normal);
}

static int i2c_address = 0;
static int i2c_bus = 0;

irom static app_action_t application_function_i2c_address(const string_t *src, string_t *dst)
{
	int intin;

	if(parse_int(1, src, &intin, 16, ' ') == parse_ok)
	{
		if((intin < 2) || (intin > 127))
		{
			string_format(dst, "i2c-address: invalid address 0x%02x\n", intin);
			return(app_action_error);
		}

		i2c_address = intin;
	}

	string_format(dst, "i2c-address: address: 0x%02x\n", i2c_address);

	return(app_action_normal);
}

irom static app_action_t application_function_i2c_bus(const string_t *src, string_t *dst)
{
	int intin;
	i2c_error_t error;

	if(parse_int(1, src, &intin, 0, ' ') == parse_ok)
	{
		if((error = i2c_select_bus(intin)) != i2c_error_ok)
		{
			i2c_error_format_string(dst, error);
			return(app_action_error);
		}

		i2c_bus = intin;
	}

	string_format(dst, "i2c-bus: bus: %d\n", i2c_bus);

	return(app_action_normal);
}

irom static void i2c_timing_report(string_t *dst, uint32_t from_us, uint32_t to_us, int length, int length_setup, double clock_offset)
{
	double spent_us, speed, clocks;

	spent_us = to_us - from_us;
	clocks = ((length + length_setup) * 9) + clock_offset;

	speed = 1000000 / (spent_us / clocks);

	string_format(dst, "> transferred %u bytes in ", length);
	string_double(dst, clocks, 1, 1000);
	string_append(dst, " scl clocks\n");
	string_append(dst, "> time spent: ");
	string_double(dst, spent_us, 1, 1000000000);
	string_append(dst, " microseconds, makes ");
	string_double(dst, speed / 1000, 3, 10000);
	string_append(dst, " kHz i2c bus\n");
}

irom static app_action_t application_function_i2c_read(const string_t *src, string_t *dst)
{
	int size, current;
	i2c_error_t error;
	uint8_t bytes[32];
	uint32_t from, to;

	if(parse_int(1, src, &size, 0, ' ') != parse_ok)
	{
		string_append(dst, "i2c-read: missing byte count\n");
		return(app_action_error);
	}

	if(size > (int)sizeof(bytes))
	{
		string_format(dst, "i2c-read: read max %d bytes\n", sizeof(bytes));
		return(app_action_error);
	}

	i2c_select_bus(i2c_bus);

	from = system_get_time();

	if((error = i2c_receive(i2c_address, size, bytes)) != i2c_error_ok)
	{
		string_append(dst, "i2c_read");
		i2c_error_format_string(dst, error);
		string_append(dst, "\n");
		return(app_action_error);
	}

	to = system_get_time();

	string_format(dst, "> i2c_read: read %d bytes from %02x:", size, i2c_address);

	for(current = 0; current < size; current++)
		string_format(dst, " %02x", bytes[current]);

	string_append(dst, "\n");

	i2c_timing_report(dst, from, to, size, 1, 3.8);

	return(app_action_normal);
}

irom static app_action_t application_function_i2c_write(const string_t *src, string_t *dst)
{
	i2c_error_t error;
	static uint8_t bytes[32];
	int size, out;
	uint32_t from, to;

	for(size = 0; size < (int)sizeof(bytes); size++)
	{
		if(parse_int(size + 1, src, &out, 16, ' ') != parse_ok)
			break;

		bytes[size] = (uint8_t)(out & 0xff);
	}

	i2c_select_bus(i2c_bus);

	from = system_get_time();

	if((error = i2c_send(i2c_address, size, bytes)) != i2c_error_ok)
	{
		string_append(dst, "i2c_write");
		i2c_error_format_string(dst, error);
		string_append(dst, "\n");
		return(app_action_error);
	}

	to = system_get_time();

	string_format(dst, "i2c_write: written %d bytes to %02x\n", size, i2c_address);

	i2c_timing_report(dst, from, to, size, 1, 3.8);

	return(app_action_normal);
}

irom static app_action_t application_function_i2c_write_read(const string_t *src, string_t *dst)
{
	i2c_error_t error;
	uint8_t sendbytes[1];
	uint8_t receivebytes[32];
	int size, current, out;
	uint32_t from, to;

	if(parse_int(1, src, &out, 16, ' ') != parse_ok)
	{
		string_append(dst, "usage: i2wr <send byte> <amount to read>\n");
		return(app_action_error);
	}

	i2c_select_bus(i2c_bus);

	sendbytes[0] = (uint8_t)(out & 0xff);

	if(parse_int(2, src, &size, 0, ' ') != parse_ok)
	{
		string_append(dst, "usage: i2wr <send byte> <amount to read>\n");
		return(app_action_error);
	}

	if((size < 0) || (size >= (int)sizeof(receivebytes)))
	{
		string_format(dst, "i2wr: max read %d bytes\n", sizeof(receivebytes));
		return(app_action_error);
	}

	from = system_get_time();

	if((error = i2c_send1_receive_repeated_start(i2c_address, sendbytes[0], size, receivebytes)) != i2c_error_ok)
	{
		string_append(dst, "i2wr");
		i2c_error_format_string(dst, error);
		string_append(dst, "\n");
		return(app_action_error);
	}

	to = system_get_time();

	string_format(dst, "> i2wr: read %d bytes from %02x:", size, i2c_address);

	for(current = 0; current < size; current++)
		string_format(dst, " %02x", receivebytes[current]);

	string_append(dst, "\n");

	i2c_timing_report(dst, from, to, size, 3, 6.6);

	return(app_action_normal);
}

irom static app_action_t application_function_i2c_sensor_init(const string_t *src, string_t *dst)
{
	int intin, bus;
	i2c_error_t error;
	i2c_sensor_t sensor;

	if((parse_int(1, src, &intin, 0, ' ')) != parse_ok)
	{
		string_format(dst, "> invalid i2c sensor: %u\n", intin);
		return(app_action_error);
	}

	sensor = (i2c_sensor_t)intin;

	if((parse_int(2, src, &bus, 0, ' ')) != parse_ok)
		bus = 0;

	if(bus >= i2c_busses)
	{
		string_format(dst, "> invalid i2c sensor: %u/%u\n", bus, intin);
		return(app_action_error);
	}

	if((error = i2c_sensor_init(bus, sensor)) != i2c_error_ok)
	{
		string_format(dst, "sensor init %d:%d", bus, sensor);
		i2c_error_format_string(dst, error);
		string_append(dst, "\n");
		return(app_action_error);
	}

	string_format(dst, "init sensor %u/%u ok\n", bus, sensor);

	return(app_action_normal);
}

irom static app_action_t application_function_i2c_sensor_read(const string_t *src, string_t *dst)
{
	int intin, bus;
	i2c_sensor_t sensor;

	if((parse_int(1, src, &intin, 0, ' ')) != parse_ok)
	{
		string_format(dst, "> invalid i2c sensor: %u\n", intin);
		return(app_action_error);
	}

	sensor = (i2c_sensor_t)intin;

	if((parse_int(2, src, &bus, 0, ' ')) != parse_ok)
		bus = 0;

	if(bus >= i2c_busses)
	{
		string_format(dst, "> invalid i2c sensor: %u/%u\n", bus, intin);
		return(app_action_error);
	}

	if(!i2c_sensor_read(dst, bus, sensor, true, false))
	{
		string_clear(dst);
		string_format(dst, "> invalid i2c sensor: %u/%u\n", bus, (int)sensor);
		return(app_action_error);
	}

	string_append(dst, "\n");

	return(app_action_normal);
}

irom static app_action_t application_function_i2c_sensor_calibrate(const string_t *src, string_t *dst)
{
	unsigned int intin, bus;
	i2c_sensor_t sensor;
	double factor, offset;
	int int_factor, int_offset;
	string_init(varname_i2s, "i2s.%u.%u.");
	string_init(varname_i2s_factor, "i2s.%u.%u.factor");
	string_init(varname_i2s_offset, "i2s.%u.%u.offset");

	if(parse_int(1, src, &bus, 0, ' ') != parse_ok)
	{
		string_append(dst, "> missing i2c bus\n");
		return(app_action_error);
	}

	if(parse_int(2, src, &intin, 0, ' ') != parse_ok)
	{
		string_append(dst, "> missing i2c sensor\n");
		return(app_action_error);
	}

	if(bus >= i2c_busses)
	{
		string_format(dst, "> invalid i2c bus: %u\n", bus);
		return(app_action_error);
	}

	if(intin >= i2c_sensor_size)
	{
		string_format(dst, "> invalid i2c sensor: %u/%u\n", bus, intin);
		return(app_action_error);
	}

	sensor = (i2c_sensor_t)intin;

	if((parse_float(3, src, &factor, ' ') == parse_ok) && (parse_float(4, src, &offset, ' ') == parse_ok))
	{
		int_factor = (int)(factor * 1000.0);
		int_offset = (int)(offset * 1000.0);

		config_delete(&varname_i2s, bus, sensor, true);

		if((int_factor != 1000) && !config_set_int(&varname_i2s_factor, bus, sensor, int_factor))
		{
			string_append(dst, "> cannot set factor\n");
			return(app_action_error);
		}

		if((int_offset != 0) && !config_set_int(&varname_i2s_offset, bus, sensor, int_offset))
		{
			string_append(dst, "> cannot set offset\n");
			return(app_action_error);
		}
	}

	if(!config_get_int(&varname_i2s_factor, bus, sensor, &int_factor))
		int_factor = 1000;

	if(!config_get_int(&varname_i2s_offset, bus, sensor, &int_offset))
		int_offset = 0;

	string_format(dst, "> i2c sensor %u/%u calibration set to factor ", bus, (int)sensor);
	string_double(dst, int_factor / 1000.0, 4, 1e10);
	string_append(dst, ", offset: ");
	string_double(dst, int_offset / 1000.0, 4, 1e10);
	string_append(dst, "\n");

	return(app_action_normal);
}

irom static app_action_t application_function_i2c_sensor_dump(const string_t *src, string_t *dst)
{
	i2c_sensor_t sensor;
	int option, bus;
	bool_t all, verbose;
	int original_length = string_length(dst);

	all = false;
	verbose = false;

	if(parse_int(1, src, &option, 0, ' ') == parse_ok)
	{
		switch(option)
		{
			case(2):
				all = true;
			case(1):
				verbose = true;
			default:
				(void)0;
		}
	}

	for(bus = 0; bus < i2c_busses; bus++)
		for(sensor = 0; sensor < i2c_sensor_size; sensor++)
		{
			if(all || i2c_sensor_detected(bus, sensor))
			{
				i2c_sensor_read(dst, bus, sensor, verbose, false);
				string_append(dst, "\n");
			}
		}

	if(string_length(dst) == original_length)
		string_append(dst, "> no sensors detected\n");

	return(app_action_normal);
}

irom static app_action_t set_unset_flag(const string_t *src, string_t *dst, bool_t add)
{
	if(parse_string(1, src, dst, ' ') == parse_ok)
	{
		if(!config_flags_change(dst, add))
		{
			string_append(dst, ": unknown flag\n");
			return(app_action_error);
		}
	}

	string_clear(dst);
	string_append(dst, "flags:");
	config_flags_to_string(dst);
	string_append(dst, "\n");

	return(app_action_normal);
}

irom static app_action_t application_function_flag_set(const string_t *src, string_t *dst)
{
	return(set_unset_flag(src, dst, true));
}

irom static app_action_t application_function_flag_unset(const string_t *src, string_t *dst)
{
	return(set_unset_flag(src, dst, false));
}

irom static app_action_t application_function_time_set(const string_t *src, string_t *dst)
{
	unsigned int Y, M, D, h, m, s;
	const char *source;

	if((parse_int(1, src, &h, 0, ' ') == parse_ok) && (parse_int(2, src, &m, 0, ' ') == parse_ok))
	{
		if(parse_int(3, src, &s, 0, ' ') != parse_ok)
			s = 0;

		if((h > 10512000) && (s == 0)) // unix timestamp supplied
		{
			h += m * 60 * 60;
			time_set_stamp(h); // timestamp,timezone_offset
		}
		else
			time_set_hms(h, m, s);
	}

	source = time_get(&h, &m, &s, &Y, &M, &D);

	string_format(dst, "%s: %04u/%02u/%02u %02u:%02u:%02u\n", source, Y, M, D, h, m, s);

	return(app_action_normal);
}

irom static void wlan_scan_done_callback(void *arg, STATUS status)
{
	struct bss_info *bss;

	static const char *status_msg[] =
	{
		"OK",
		"FAIL",
		"PENDING",
		"BUSY",
		"CANCEL"
	};

	static const char *auth_mode_msg[] =
	{
		"OTHER",
		"WEP",
		"WPA PSK",
		"WPA2 PSK",
		"WPA PSK + WPA2 PSK"
	};

	log("wlan scan result: %s\n", status <= CANCEL ? status_msg[status] : "<invalid>");
	log("> %-16s  %-4s  %-4s  %-18s  %-6s  %s\n", "SSID", "CHAN", "RSSI", "AUTH", "OFFSET", "BSSID");

	for(bss = arg; bss; bss = bss->next.stqe_next)
		log("> %-16s  %4u  %4d  %-18s  %6d  %02x:%02x:%02x:%02x:%02x:%02x\n",
				bss->ssid,
				bss->channel,
				bss->rssi,
				bss->authmode < AUTH_MAX ? auth_mode_msg[bss->authmode] : "<invalid auth>",
				bss->freq_offset,
				bss->bssid[0], bss->bssid[1], bss->bssid[2], bss->bssid[3], bss->bssid[4], bss->bssid[5]);
}

irom static app_action_t application_function_wlan_ap_configure(const string_t *src, string_t *dst)
{
	int channel;
	string_new(stack, ssid, 64);
	string_new(stack, passwd, 64);
	string_init(varname_wlan_ap_ssid, "wlan.ap.ssid");
	string_init(varname_wlan_ap_passwd, "wlan.ap.passwd");
	string_init(varname_wlan_ap_channel, "wlan.ap.channel");

	if((parse_string(1, src, &ssid, ' ') == parse_ok) && (parse_string(2, src, &passwd, ' ') == parse_ok) &&
			(parse_int(3, src, &channel, 0, ' ') == parse_ok))
	{
		if((channel < 1) || (channel > 13))
		{
			string_format(dst, "> channel %d out of range (1-13)\n", channel);
			return(app_action_error);
		}

		if(string_length(&passwd) < 8)
		{
			string_format(dst, "> passwd \"%s\" too short (length must be >= 8)\n",
					string_to_cstr(&passwd));
			return(app_action_error);
		}

		if(!config_set_string(&varname_wlan_ap_ssid, -1, -1, &ssid, -1, -1))
		{
			string_append(dst, "> cannot set config\n");
			return(app_action_error);
		}

		if(!config_set_string(&varname_wlan_ap_passwd, -1, -1, &passwd, -1, -1))
		{
			string_append(dst, "> cannot set config\n");
			return(app_action_error);
		}

		if(!config_set_int(&varname_wlan_ap_channel, -1, -1, channel))
		{
			string_append(dst, "> cannot set config\n");
			return(app_action_error);
		}
	}

	string_clear(&ssid);
	string_clear(&passwd);

	if(!config_get_string(&varname_wlan_ap_ssid, -1, -1, &ssid))
	{
		string_clear(&ssid);
		string_append(&ssid, "<empty>");
	}

	if(!config_get_string(&varname_wlan_ap_passwd, -1, -1, &passwd))
	{
		string_clear(&passwd);
		string_append(&passwd, "<empty>");
	}

	if(!config_get_int(&varname_wlan_ap_channel, -1, -1, &channel))
		channel = 0;

	string_format(dst, "> ssid: \"%s\", passwd: \"%s\", channel: %d\n",
			string_to_cstr(&ssid), string_to_cstr(&passwd), channel);

	return(app_action_normal);
}

irom static app_action_t application_function_wlan_client_configure(const string_t *src, string_t *dst)
{
	string_new(stack, ssid, 64);
	string_new(stack, passwd, 64);
	string_init(varname_wlan_client_ssid, "wlan.client.ssid");
	string_init(varname_wlan_client_passwd, "wlan.client.passwd");

	if((parse_string(1, src, &ssid, ' ') == parse_ok) && (parse_string(2, src, &passwd, ' ') == parse_ok))
	{
		if(string_length(&passwd) < 8)
		{
			string_format(dst, "> passwd \"%s\" too short (length must be >= 8)\n", string_to_cstr(&passwd));
			return(app_action_error);
		}

		if(!config_set_string(&varname_wlan_client_ssid, -1, -1, &ssid, -1, -1))
		{
			string_append(dst, "> cannot set config\n");
			return(app_action_error);
		}

		if(!config_set_string(&varname_wlan_client_passwd, -1, -1, &passwd, -1, -1))
		{
			string_append(dst, "> cannot set config\n");
			return(app_action_error);
		}
	}

	string_clear(&ssid);
	string_clear(&passwd);

	if(!config_get_string(&varname_wlan_client_ssid, -1, -1, &ssid))
	{
		string_clear(&ssid);
		string_append(&ssid, "<empty>");
	}

	if(!config_get_string(&varname_wlan_client_passwd, -1, -1, &passwd))
	{
		string_clear(&passwd);
		string_append(&passwd, "<empty>");
	}

	string_format(dst, "> ssid: \"%s\", passwd: \"%s\"\n",
			string_to_cstr(&ssid), string_to_cstr(&passwd));

	return(app_action_normal);
}

irom static app_action_t application_function_wlan_mode(const string_t *src, string_t *dst)
{
	unsigned int int_mode;
	config_wlan_mode_t mode;
	string_init(varname_wlan_mode, "wlan.mode");

	if(parse_string(1, src, dst, ' ') == parse_ok)
	{
		if(string_match_cstr(dst, "client"))
		{
			string_clear(dst);

			if(!config_set_int(&varname_wlan_mode, -1, -1, config_wlan_mode_client))
			{
				string_append(dst, "> cannot set config\n");
				return(app_action_error);
			}

			if(!wlan_init())
			{
				string_append(dst, "> cannot init\n");
				return(app_action_error);
			}

			return(app_action_disconnect);
		}

		if(string_match_cstr(dst, "ap"))
		{
			string_clear(dst);

			if(!config_set_int(&varname_wlan_mode, -1, -1, config_wlan_mode_ap))
			{
				string_append(dst, "> cannot set config\n");
				return(app_action_error);
			}

			if(!wlan_init())
			{
				string_append(dst, "> cannot init\n");
				return(app_action_error);
			}

			return(app_action_disconnect);
		}

		string_append(dst, ": invalid wlan mode\n");
		return(app_action_error);
	}

	string_clear(dst);
	string_append(dst, "> current mode: ");

	if(config_get_int(&varname_wlan_mode, -1, -1, &int_mode))
	{
		mode = (config_wlan_mode_t)int_mode;

		switch(mode)
		{
			case(config_wlan_mode_client):
			{
				string_append(dst, "client mode");
				break;
			}

			case(config_wlan_mode_ap):
			{
				string_append(dst, "ap mode");
				break;
			}

			default:
			{
				string_append(dst, "unknown mode");
				break;
			}
		}
	}
	else
		string_append(dst, "mode unset");

	string_append(dst, "\n");

	return(app_action_normal);
}

irom static app_action_t application_function_log_display(const string_t *src, string_t *dst)
{
	if(string_length(&logbuffer) == 0)
		string_append(dst, "<log empty>\n");
	else
	{
		string_clear(dst);
		string_append_string(dst, &logbuffer);
	}

	return(app_action_normal);
}

irom static app_action_t application_function_log_clear(const string_t *src, string_t *dst)
{
	app_action_t rv;

	rv = application_function_log_display(src, dst);

	string_clear(&logbuffer);

	return(rv);
}

irom static app_action_t application_function_wlan_scan(const string_t *src, string_t *dst)
{
	if(config_uses_logbuffer())
	{
		string_append(dst, "wlan-scan: output buffer is in use\n");
		return(app_action_error);
	}

	wifi_station_scan(0, wlan_scan_done_callback);
	string_append(dst, "wlan scan started, use log-display to retrieve the results\n");

	return(app_action_normal);
}

irom static app_action_t application_function_ntp_dump(const string_t *src, string_t *dst)
{
	ip_addr_t addr;
	int timezone;

	timezone = sntp_get_timezone();
	addr = sntp_getserver(0);

	string_append(dst, "> server: ");
	string_ip(dst, addr);

	string_format(dst, "\n> time zone: GMT%c%d\n> ntp time: %s",
			timezone < 0 ? '-' : '+',
			timezone < 0 ? 0 - timezone : timezone,
			sntp_get_real_time(sntp_get_current_timestamp()));

	return(app_action_normal);
}

irom static app_action_t application_function_ntp_set(const string_t *src, string_t *dst)
{
	int					timezone, ix;
	ip_addr_to_bytes_t	a2b;

	string_new(stack, ip, 32);
	string_init(varname_ntp_server, "ntp.server.%u");
	string_init(varname_ntp_tz, "ntp.tz");

	if((parse_string(1, src, &ip, ' ') == parse_ok) && (parse_int(2, src, &timezone, 0, ' ') == parse_ok))
	{
		a2b.ip_addr = ip_addr(string_to_cstr(&ip));

		if((a2b.byte[0] == 0) && (a2b.byte[1] == 0) && (a2b.byte[2] == 0) && (a2b.byte[3] == 0))
			for(ix = 0; ix < 4; ix++)
				config_delete(&varname_ntp_server, ix, -1, false);
		else
			for(ix = 0; ix < 4; ix++)
				if(!config_set_int(&varname_ntp_server, ix, -1, a2b.byte[ix]))
				{
					string_clear(dst);
					string_append(dst, "cannot set config\n");
					return(app_action_error);
				}

		if(timezone == 0)
			config_delete(&varname_ntp_tz, -1, -1, false);
		else
			if(!config_set_int(&varname_ntp_tz, -1, -1, timezone))
			{
				string_clear(dst);
				string_append(dst, "cannot set config\n");
				return(app_action_error);
			}

		time_ntp_init();
	}

	return(application_function_ntp_dump(src, dst));
}

irom static app_action_t application_function_gpio_status_set(const string_t *src, string_t *dst)
{
	int trigger_io, trigger_pin;
	string_init(varname_trig_stat_io, "trigger.status.io");
	string_init(varname_trig_stat_pin, "trigger.status.pin");

	if((parse_int(1, src, &trigger_io, 0, ' ') == parse_ok) && (parse_int(2, src, &trigger_pin, 0, ' ') == parse_ok))
	{
		if((trigger_io < -1) || (trigger_io > io_id_size))
		{
			string_format(dst, "status trigger io %d/%d invalid\n", trigger_io, trigger_pin);
			return(app_action_error);
		}

		if((trigger_io < 0) || (trigger_pin < 0))
		{
			config_delete(&varname_trig_stat_io, -1, -1, false);
			config_delete(&varname_trig_stat_pin, -1, -1, false);
		}
		else
			if(!config_set_int(&varname_trig_stat_io, -1, -1, trigger_io) ||
					!config_set_int(&varname_trig_stat_pin, -1, -1, trigger_pin))
			{
				string_append(dst, "> cannot set config\n");
				return(app_action_error);
			}
	}

	if(!config_get_int(&varname_trig_stat_io, -1, -1, &trigger_io))
		trigger_io = -1;

	if(!config_get_int(&varname_trig_stat_pin, -1, -1, &trigger_pin))
		trigger_pin = -1;

	string_format(dst, "status trigger at io %d/%d (-1 is disabled)\n",
			trigger_io, trigger_pin);

	return(app_action_normal);
}

irom static app_action_t application_function_gpio_assoc_set(const string_t *src, string_t *dst)
{
	int trigger_io, trigger_pin;
	string_init(varname_trig_assoc_io, "trigger.assoc.io");
	string_init(varname_trig_assoc_pin, "trigger.assoc.pin");

	if((parse_int(1, src, &trigger_io, 0, ' ') == parse_ok) && (parse_int(2, src, &trigger_pin, 0, ' ') == parse_ok))
	{
		if((trigger_io < -1) || (trigger_io > io_id_size))
		{
			string_format(dst, "association trigger io %d/%d invalid\n", trigger_io, trigger_pin);
			return(app_action_error);
		}

		if((trigger_io < 0) || (trigger_pin < 0))
		{
			config_delete(&varname_trig_assoc_io, -1, -1, false);
			config_delete(&varname_trig_assoc_pin, -1, -1, false);
		}
		else
			if(!config_set_int(&varname_trig_assoc_io, -1, -1, trigger_io) ||
					!config_set_int(&varname_trig_assoc_pin, -1, -1, trigger_pin))
			{
				string_append(dst, "> cannot set config\n");
				return(app_action_error);
			}
	}

	if(!config_get_int(&varname_trig_assoc_io, -1, -1, &trigger_io))
		trigger_io = -1;

	if(!config_get_int(&varname_trig_assoc_pin, -1, -1, &trigger_pin))
		trigger_pin = -1;

	string_format(dst, "wlan association trigger at io %d/%d (-1 is disabled)\n",
			trigger_io, trigger_pin);

	return(app_action_normal);
}

irom static app_action_t application_function_peek(const string_t *src, string_t *dst)
{
	uint32_t address;

	if(parse_int(1, src, &address, 16, ' ') != parse_ok)
	{
		string_append(dst, "> use peek <address>");
		return(app_action_error);
	}

	address &= ~0x03; // ensure proper alignment

	string_format(dst, "> peek (0x%x) = 0x%x\n", address, *(uint32_t *)address);

	return(app_action_normal);
}

irom static app_action_t application_function_poke(const string_t *src, string_t *dst)
{
	uint32_t address;
	uint32_t value;

	if((parse_int(1, src, &address, 16, ' ') != parse_ok) || (parse_int(2, src, &value, 16, ' ') != parse_ok))
	{
		string_append(dst, "> use poke <address> <value>");
		return(app_action_error);
	}

	address &= ~0x03; // ensure proper alignment

	*(uint32_t *)address = value;

	string_format(dst, "> poke (0x%x,0x%x) = 0x%x\n", address, value, *(uint32_t *)address);

	return(app_action_normal);
}

static const application_function_table_t application_function_table[] =
{
	{
		"?", "help",
		application_function_help,
		"help [command]",
	},
	{
		"q", "quit",
		application_function_quit,
		"quit",
	},
	{
		"r", "reset",
		application_function_reset,
		"reset",
	},
	{
		"id", "identification",
		application_function_identification,
		"hostname/identification/location/comment",
	},
	{
		"s", "stats",
		application_function_stats_firmware,
		"statistics",
	},
	{
		"sc", "stats-counters",
		application_function_stats_counters,
		"stats (counters)",
	},
	{
		"si", "stats-i2c",
		application_function_stats_i2c,
		"stats (i2c)",
	},
	{
		"st", "stats-time",
		application_function_stats_time,
		"stats (time)",
	},
	{
		"sw", "stats-wlan",
		application_function_stats_wlan,
		"stats (wlan)",
	},
	{
		"bp", "bridge-port",
		application_function_bridge_port,
		"set uart bridge tcp/udp port (default 23)"
	},
	{
		"bt", "bridge-timeout",
		application_function_bridge_timeout,
		"set uart bridge tcp connection timeout (default 0)"
	},
	{
		"cp", "command-port",
		application_function_command_port,
		"set command tcp/udp port (default 24)"
	},
	{
		"ct", "command-timeout",
		application_function_command_timeout,
		"set command tcp connection timeout (default 0)"
	},
	{
		"cd", "config-dump",
		application_function_config_dump,
		"dump config contents (stored in flash)"
	},
	{
		"cw", "config-write",
		application_function_config_write,
		"write config to non-volatile storage"
	},
	{
		"db", "display-brightness",
		application_function_display_brightness,
		"set or show display brightness"
	},
	{
		"dd", "display-dump",
		application_function_display_dump,
		"shows all displays"
	},
	{
		"ddm", "display-default-message",
		application_function_display_default_message,
		"set default message",
	},
	{
		"dft", "display-flip-timeout",
		application_function_display_flip_timeout,
		"set the time between flipping of the slots",
	},
	{
		"ds", "display-set",
		application_function_display_set,
		"put content on display <slot> <timeout> <tag> <text>"
	},
	{
		"fs", "flag-set",
		application_function_flag_set,
		"set a flag",
	},
	{
		"fu", "flag-unset",
		application_function_flag_unset,
		"unset a flag",
	},
	{
		"gas", "gpio-association-set",
		application_function_gpio_assoc_set,
		"set gpio to trigger on wlan association"
	},
	{
		"gss", "gpio-status-set",
		application_function_gpio_status_set,
		"set gpio to trigger on status update"
	},
	{
		"i2a", "i2c-address",
		application_function_i2c_address,
		"set i2c slave address",
	},
	{
		"i2b", "i2c-bus",
		application_function_i2c_bus,
		"set i2c mux bus number (0-3)",
	},
	{
		"i2r", "i2c-read",
		application_function_i2c_read,
		"read data from i2c slave",
	},
	{
		"i2w", "i2c-write",
		application_function_i2c_write,
		"write data to i2c slave",
	},
	{
		"i2wr", "i2c-write-read",
		application_function_i2c_write_read,
		"write data to i2c slave and read back data",
	},
	{
		"im", "io-mode",
		application_function_io_mode,
		"config i/o pin",
	},
	{
		"ir", "io-read",
		application_function_io_read,
		"read from i/o pin",
	},
	{
		"it", "io-trigger",
		application_function_io_trigger,
		"trigger i/o pin",
	},
	{
		"iw", "io-write",
		application_function_io_write,
		"write to i/o pin",
	},
	{
		"isf", "io-set-flag",
		application_function_io_set_flag,
		"set i/o pin flag",
	},
	{
		"pp", "pwm-period",
		application_function_pwm_period,
		"set pwm period (rate = 200 ns / period)",
	},
	{
		"icf", "io-clear-flag",
		application_function_io_clear_flag,
		"clear i/o pin flag",
	},
	{
		"isr", "i2c-sensor-read",
		application_function_i2c_sensor_read,
		"read from i2c sensor",
	},
	{
		"isc", "i2c-sensor-calibrate",
		application_function_i2c_sensor_calibrate,
		"calibrate i2c sensor, use sensor factor offset",
	},
	{
		"isd", "i2c-sensor-dump",
		application_function_i2c_sensor_dump,
		"dump all i2c sensors",
	},
	{
		"l", "log-display",
		application_function_log_display,
		"display log"
	},
	{
		"lc", "log-clear",
		application_function_log_clear,
		"clear the log"
	},
	{
		"nd", "ntp-dump",
		application_function_ntp_dump,
		"dump ntp information",
	},
	{
		"ns", "ntp-set",
		application_function_ntp_set,
		"set ntp <ip addr> <timezone GMT+x>",
	},
	{
		"ts", "time-set",
		application_function_time_set,
		"set time base [h m (s)] or [unix timestamp tz_offset]",
	},
	{
		"ub", "uart-baud",
		application_function_uart_baud_rate,
		"set uart baud rate [1-1000000]",
	},
	{
		"ud", "uart-data",
		application_function_uart_data_bits,
		"set uart data bits [5/6/7/8]",
	},
	{
		"us", "uart-stop",
		application_function_uart_stop_bits,
		"set uart stop bits [1/2]",
	},
	{
		"up", "uart-parity",
		application_function_uart_parity,
		"set uart parity [none/even/odd]",
	},
	{
		"wac", "wlan-ap-configure",
		application_function_wlan_ap_configure,
		"configure access point mode wlan params, supply ssid, passwd and channel"
	},
	{
		"wcc", "wlan-client-configure",
		application_function_wlan_client_configure,
		"configure client mode wlan params, supply ssid and passwd"
	},
	{
		"wm", "wlan-mode",
		application_function_wlan_mode,
		"set wlan mode: client or ap"
	},
	{
		"ws", "wlan-scan",
		application_function_wlan_scan,
		"scan wlan, use wlan-list to retrieve the results"
	},
	{
		"cqs", "config-query-string",
		application_function_config_query_string,
		"query config string"
	},
	{
		"cqi", "config-query-int",
		application_function_config_query_int,
		"query config int"
	},
	{
		"cs", "config-set",
		application_function_config_set,
		"set config entry"
	},
	{
		"cde", "config-delete",
		application_function_config_delete,
		"delete config entry"
	},
	{
		"GET", "http-get",
		application_function_http_get,
		"get access over http"
	},
	{
		"isi", "i2c-sensor-init",
		application_function_i2c_sensor_init,
		"(re-)init i2c sensor",
	},
	{
		"or", "ota-read",
		application_function_ota_read,
		"ota-read length start chunk-size",
	},
	{
		"od", "ota-receive-data",
		application_function_ota_receive,
		"ota-receive-data",
	},
	{
		"ow", "ota-write",
		application_function_ota_write,
		"ota-write length [start]",
	},
	{
		"owd", "ota-write-dummy",
		application_function_ota_write_dummy,
		"ota-write-dummy length [start]",
	},
	{
		"os", "ota-send-data",
		application_function_ota_send,
		"ota-send chunk_length data",
	},
	{
		"of", "ota-finish",
		application_function_ota_finish,
		"ota-finish md5sum",
	},
	{
		"oc", "ota-commit",
		application_function_ota_commit,
		"ota-commit",
	},
	{
		"flash-info", "flash-info",
		application_function_flash_info,
		"flash-info",
	},
	{
		"flash-erase", "flash-erase",
		application_function_flash_erase,
		"flash-erase",
	},
	{
		"flash-send", "flash-send",
		application_function_flash_send,
		"flash-send",
	},
	{
		"flash-read", "flash-read",
		application_function_flash_read,
		"flash-read",
	},
	{
		"flash-receive", "flash-receive",
		application_function_flash_receive,
		"flash-receive",
	},
	{
		"flash-write", "flash-write",
		application_function_flash_write,
		"flash-write",
	},
	{
		"flash-verify", "flash-verify",
		application_function_flash_verify,
		"flash-verify",
	},
	{
		"flash-checksum", "flash-checksum",
		application_function_flash_checksum,
		"flash-checksum",
	},
	{
		"flash-select", "flash-select",
		application_function_flash_select,
		"flash-select",
	},
	{
		"pe", "peek",
		application_function_peek,
		"peek at a memory address",
	},
	{
		"po", "poke",
		application_function_poke,
		"poke to a memory address",
	},
	{
		"", "",
		(void *)0,
		"",
	},
};
