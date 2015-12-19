#include "application.h"
#include "application-parameters.h"

#include "gpios.h"
#include "stats.h"
#include "util.h"
#include "user_main.h"
#include "config.h"
#include "uart.h"
#include "i2c.h"
#include "i2c_sensor.h"
#include "display.h"

#include <user_interface.h>
#include <c_types.h>
#include <sntp.h>

#include <stdlib.h>

typedef struct
{
	const char		*command1;
	const char		*command2;
	uint8_t			required_args;
	app_action_t	(*function)(application_parameters_t);
	const char		*description;
} application_function_table_t;

static const application_function_table_t application_function_table[];

irom app_action_t application_content(const char *src, unsigned int size, char *dst)
{
	args_t	args;
	unsigned int args_count, arg_current;
	unsigned int src_current = 0, src_left;
	bool_t ws_skipped;
	const application_function_table_t *tableptr;

	*dst = '\0';

	if(src[0] == '\0')
		return(app_action_empty);

	src_left = strlen(src);

	for(args_count = 0; (src_left > 0) && (args_count < application_num_args);)
	{
		ws_skipped = 0;

		for(arg_current = 0;
				(src_left > 0) && (arg_current < (application_length_args - 1));
				src_current++, src_left--)
		{
			if((src[src_current] <= ' ') || (src[src_current] > '~'))
			{
				if(!ws_skipped)
					continue;
				else
					break;
			}

			ws_skipped = 1;

			args[args_count][arg_current++] = src[src_current];
		}

		args[args_count][arg_current] = '\0';

		if(arg_current)
			args_count++;

		while((src_left > 0) && (src[src_current] > ' ') && (src[src_current] <= '~'))
		{
			src_left--;
			src_current++;
		}
	}

	if(args_count == 0)
		return(app_action_empty);

	for(tableptr = application_function_table; tableptr->function; tableptr++)
		if(!strcmp(args[0], tableptr->command1) ||
			!strcmp(args[0], tableptr->command2))
			break;

	if(tableptr->function)
	{
		if(args_count < (unsigned int)(tableptr->required_args + 1))
		{
			snprintf(dst, size, "insufficient arguments: %d (%d required)\n", args_count - 1, tableptr->required_args);
			return(app_action_error);
		}

		application_parameters_t ap;

		ap.cmdline			= src;
		ap.nargs			= args_count;
		ap.args				= &args;
		ap.size				= size;
		ap.dst				= dst;

		return(tableptr->function(ap));
	}

	snprintf(dst, size, "command \"%s\" unknown\n", args[0]);
	return(app_action_error);
}

irom static app_action_t application_function_config_dump(application_parameters_t ap)
{
	config_dump(ap.size, ap.dst);

	return(app_action_normal);
}

irom static app_action_t application_function_config_write(application_parameters_t ap)
{
	config_write();
	strlcpy(ap.dst, "config write OK\n", ap.size);

	return(app_action_normal);
}

irom static app_action_t application_function_help(application_parameters_t ap)
{
	const application_function_table_t *tableptr;
	unsigned int length;

	for(tableptr = application_function_table; tableptr->function; tableptr++)
	{
		length = snprintf(ap.dst, ap.size, "> %s/%s[%d]: %s\n",
				tableptr->command1, tableptr->command2,
				tableptr->required_args, tableptr->description);
		ap.dst	+= length;
		ap.size	-= length;
	}

	return(app_action_normal);
}

irom static app_action_t application_function_quit(application_parameters_t ap)
{
	return(app_action_disconnect);
}

irom static app_action_t application_function_reset(application_parameters_t ap)
{
	return(app_action_reset);
}

irom static app_action_t application_function_stats(application_parameters_t ap)
{
	stats_generate(ap.size, ap.dst);

	return(app_action_normal);
}

irom static app_action_t application_function_bridge_tcp_port(application_parameters_t ap)
{
	unsigned int tcp_port;

	if(ap.nargs > 1)
	{
		tcp_port = atoi((*ap.args)[1]);

		if(tcp_port > 65535)
		{
			snprintf(ap.dst, ap.size, "bridge-tcp-port: out of range: %u\n", tcp_port);
			return(app_action_error);
		}

		config->bridge_tcp_port = (uint16_t)tcp_port;
		snprintf(ap.dst, ap.size, "bridge-tcp_port: %u, write config and restart to activate\n", config->bridge_tcp_port);
	}
	else
		snprintf(ap.dst, ap.size, "bridge-tcp_port: %u\n", config->bridge_tcp_port);

	return(app_action_normal);
}

irom static app_action_t application_function_uart_baud_rate(application_parameters_t ap)
{
	unsigned int baud_rate = atoi((*ap.args)[1]);

	if(baud_rate > 1000000)
	{
		snprintf(ap.dst, ap.size, "uart-baud: out of range: %u\n", baud_rate);
		return(1);
	}

	config->uart.baud_rate = baud_rate;

	snprintf(ap.dst, ap.size, "uart-baud: %u\n", config->uart.baud_rate);

	return(app_action_normal);
}

irom static app_action_t application_function_uart_data_bits(application_parameters_t ap)
{
	unsigned int data_bits = atoi((*ap.args)[1]);

	if((data_bits < 5) || (data_bits > 8))
	{
		snprintf(ap.dst, ap.size, "uart-data: out of range: %u\n", data_bits);
		return(1);
	}

	config->uart.data_bits = data_bits;

	snprintf(ap.dst, ap.size, "uart-data: %u\n", config->uart.data_bits);

	return(app_action_normal);
}

irom static app_action_t application_function_uart_stop_bits(application_parameters_t ap)
{
	unsigned int stop_bits = atoi((*ap.args)[1]);

	if((stop_bits < 1) || (stop_bits > 2))
	{
		snprintf(ap.dst, ap.size, "uart-stop: out of range: %u\n", stop_bits);
		return(1);
	}

	config->uart.stop_bits = stop_bits;

	snprintf(ap.dst, ap.size, "uart-stop: %u\n", config->uart.stop_bits);

	return(app_action_normal);
}

irom static app_action_t application_function_uart_parity(application_parameters_t ap)
{
	uart_parity_t parity = uart_string_to_parity((*ap.args)[1]);

	if(parity == parity_error)
	{
		snprintf(ap.dst, ap.size, "uart-parity: out of range: %s\n", (*ap.args)[1]);
		return(1);
	}

	config->uart.parity = parity;

	snprintf(ap.dst, ap.size, "uart-parity: %s\n", uart_parity_to_string(config->uart.parity));

	return(app_action_normal);
}

static unsigned int i2c_address = 0;

irom static app_action_t application_function_i2c_address(application_parameters_t ap)
{
	i2c_address = strtoul((*ap.args)[1], 0, 16);

	snprintf(ap.dst, ap.size, "i2c-address: i2c slave address set to 0x%02x\n", i2c_address);

	return(app_action_normal);
}

irom static app_action_t application_function_i2c_delay(application_parameters_t ap)
{
	config->i2c_delay = atoi((*ap.args)[1]);

	snprintf(ap.dst, ap.size, "i2c-delay: i2c delay set to %u, write config and restart to activate\n", config->i2c_delay);

	return(app_action_normal);
}

irom static app_action_t application_function_i2c_read(application_parameters_t ap)
{
	unsigned int length, current, size;
	i2c_error_t error;
	uint8_t bytes[32];

	size = atoi((*ap.args)[1]);

	if(size > sizeof(bytes))
	{
		snprintf(ap.dst, ap.size, "i2c-read: read max %u bytes\n", sizeof(bytes));
		return(app_action_error);
	}

	if((error = i2c_receive(i2c_address, size, bytes)) != i2c_error_ok)
	{
		i2c_error_format_string("i2c-read", error, ap.size, ap.dst);
		strlcat(ap.dst, "\n", ap.size);
		i2c_reset();
		return(app_action_error);
	}

	length = snprintf(ap.dst, ap.size, "i2c_read: read %u bytes from %02x:", size, i2c_address);
	ap.dst += length;
	ap.size -= length;

	for(current = 0; current < size; current++)
	{
		length = snprintf(ap.dst, ap.size, " %02x", bytes[current]);
		ap.dst += length;
		ap.size -= length;
	}

	snprintf(ap.dst, ap.size, "\n");

	return(app_action_normal);
}

irom static app_action_t application_function_i2c_write(application_parameters_t ap)
{
	unsigned int src_current, dst_current;
	i2c_error_t error;
	uint8_t bytes[32];

	for(src_current = 1, dst_current = 0;
			(src_current < ap.nargs) && (dst_current < sizeof(bytes));
			src_current++, dst_current++)
	{
		bytes[dst_current] = (uint8_t)strtoul((*ap.args)[src_current], 0, 16);
	}

	if((error = i2c_send(i2c_address, dst_current, bytes)) != i2c_error_ok)
	{
		i2c_error_format_string("i2c-write", error, ap.size, ap.dst);
		strlcat(ap.dst, "\n", ap.size);
		i2c_reset();
		return(app_action_error);
	}

	snprintf(ap.dst, ap.size, "i2c_write: written %u bytes to %02x\n", dst_current, i2c_address);

	return(app_action_normal);
}

irom static app_action_t application_function_i2c_reset(application_parameters_t ap)
{
	i2c_error_t error;

	if((error = i2c_reset()) != i2c_error_ok)
	{
		i2c_error_format_string("i2c-reset", error, ap.size, ap.dst);
		strlcat(ap.dst, "\n", ap.size);
		return(app_action_error);
	}

	snprintf(ap.dst, ap.size, "i2c_reset: ok\n");

	return(app_action_normal);
}

irom static app_action_t application_function_i2c_sensor_read(application_parameters_t ap)
{
	i2c_sensor_t sensor;

	sensor = atoi((*ap.args)[1]);

	if(!i2c_sensor_read(sensor, true, ap.size, ap.dst))
	{
		snprintf(ap.dst, ap.size, "> invalid i2c sensor: %d\n", (int)sensor);
		return(app_action_error);
	}

	return(app_action_normal);
}

irom static app_action_t application_function_i2c_sensor_calibrate(application_parameters_t ap)
{
	i2c_sensor_t sensor;
	float factor;
	float offset;
	unsigned int length;

	sensor = atoi((*ap.args)[1]);
	factor = string_to_double((*ap.args)[2]);
	offset = string_to_double((*ap.args)[3]);

	if(!i2c_sensor_setcal(sensor, factor, offset))
	{
		snprintf(ap.dst, ap.size, "> invalid i2c sensor: %d\n", (int)sensor);
		return(app_action_error);
	}

	length = snprintf(ap.dst, ap.size, "> i2c sensor %d calibration set to factor ", (int)sensor);
	ap.dst += length;
	ap.size -= length;

	length = double_to_string(config->i2c_sensors.sensor[sensor].calibration.factor, 4, 1e10, ap.size, ap.dst);
	ap.dst += length;
	ap.size -= length;

	length = snprintf(ap.dst, ap.size, ", offset: ");
	ap.dst += length;
	ap.size -= length;

	length = double_to_string(config->i2c_sensors.sensor[sensor].calibration.offset, 4, 1e10, ap.size, ap.dst);
	ap.dst += length;
	ap.size -= length;

	length = snprintf(ap.dst, ap.size, "\n");
	ap.dst += length;
	ap.size -= length;

	return(app_action_normal);
}

irom static app_action_t application_function_i2c_sensor_dump(application_parameters_t ap)
{
	i2c_sensor_t sensor;
	unsigned int offset;
	bool_t all, verbose;
	char *orig_dst = ap.dst;

	all = false;
	verbose = false;

	if(ap.nargs > 1)
	{
		switch(atoi((*ap.args)[1]))
		{
			case(2):
				verbose = true;
			case(1):
				all = true;
			default:
				(void)0;
		}
	}

	for(sensor = 0; sensor < i2c_sensor_size; sensor++)
	{
		if(all || i2c_sensor_detected(sensor))
		{
			offset = i2c_sensor_read(sensor, verbose, ap.size, ap.dst);
			ap.dst	+= offset;
			ap.size	-= offset;
		}
	}

	if(ap.dst == orig_dst)
		snprintf(ap.dst, ap.size, "%s", "> no sensors detected\n");

	return(app_action_normal);
}

irom static app_action_t set_unset_flag(application_parameters_t ap, bool_t value)
{
	unsigned int length;

	if(ap.nargs < 2)
	{
		length = snprintf(ap.dst, ap.size, "%s", "flags: ");
		ap.dst += length;
		ap.size -= length;

		length = config_flags_to_string(ap.size, ap.dst, config->flags);
		ap.dst += length;
		ap.size -= length;

		strlcpy(ap.dst, "\n", ap.size);

		return(app_action_normal);
	}

	if(!config_set_flag_by_name((*ap.args)[1], value))
	{
		snprintf(ap.dst, ap.size, "> unknown flag %s\n", (*ap.args)[1]);
		return(app_action_error);
	}

	snprintf(ap.dst, ap.size, "> flag %s %s\n", (*ap.args)[1], onoff(value));

	return(app_action_normal);
}

irom static app_action_t application_function_set(application_parameters_t ap)
{
	return(set_unset_flag(ap, true));
}

irom static app_action_t application_function_unset(application_parameters_t ap)
{
	return(set_unset_flag(ap, false));
}

irom static app_action_t application_function_rtc_set(application_parameters_t ap)
{
	rt_hours = atoi((*ap.args)[1]);
	rt_mins = atoi((*ap.args)[2]);
	rt_secs = 0;

	snprintf(ap.dst, ap.size, "rtc set to %02u:%02u\n", rt_hours, rt_mins);

	return(app_action_normal);
}

irom static app_action_t application_function_display_brightness(application_parameters_t ap)
{
	unsigned int id;
	unsigned int value;
	static const char *usage = "display-brightness: usage: display_id <brightess>=0,1,2,3,4\n";

	id = atoi((*ap.args)[1]);

	if(ap.nargs > 2)
	{
		value = atoi((*ap.args)[2]);

		if(!display_set_brightness(id, value))
		{
			snprintf(ap.dst, ap.size, "%s", usage);
			return(app_action_error);
		}
	}

	if(!display_get_brightness(id, &value))
	{
		snprintf(ap.dst, ap.size, "%s", usage);
		return(app_action_error);
	}

	snprintf(ap.dst, ap.size, "display %u brightness: %u\n", id, value);

	return(app_action_normal);
}

irom static app_action_t application_function_display_dump(application_parameters_t ap)
{
	unsigned int verbose;

	if(ap.nargs > 1)
		verbose = atoi((*ap.args)[1]);
	else
		verbose = 0;

	display_dump(ap.size, ap.dst, verbose);

	return(app_action_normal);
}

irom static app_action_t application_function_display_default_message(application_parameters_t ap)
{
	const char *text;
	unsigned int current;

	text = ap.cmdline;

	for(current = 1; current > 0; text++)
	{
		if(*text == '\0')
			break;

		if(*text == ' ')
			current--;
	}

	snprintf(config->display_default_msg, sizeof(config->display_default_msg), "%s", text);
	snprintf(ap.dst, ap.size, "set default display message to \"%s\", write config and restart to activate\n",
			config->display_default_msg);

	return(app_action_normal);
}

irom static app_action_t application_function_display_set(application_parameters_t ap)
{
	unsigned int id;
	unsigned int slot;
	unsigned int current;
	unsigned int timeout;
	const char *text;

	id = atoi((*ap.args)[1]);
	slot = atoi((*ap.args)[2]);
	timeout = atoi((*ap.args)[3]);

	text = ap.cmdline;

	for(current = 4; current > 0; text++)
	{
		if(*text == '\0')
			break;

		if(*text == ' ')
			current--;
	}

	display_setslot(id, slot, timeout, text, ap.size, ap.dst);

	return(app_action_normal);
}

irom static const char *wlan_scan_status_to_string(STATUS status)
{
	static const char *status_msg[] =
	{
		"OK",
		"FAIL",
		"PENDING",
		"BUSY",
		"CANCEL"
	};

	if(status <= CANCEL)
		return(status_msg[status]);

	return("ERROR");
}

irom static const char *wlan_scan_authmode_to_string(AUTH_MODE auth_mode)
{
	static const char *auth_mode_msg[] =
	{
		"OTHER",
		"WEP",
		"WPA PSK",
		"WPA2 PSK",
		"WPA PSK + WPA2 PSK"
	};

	if(auth_mode < AUTH_MAX)
		return(auth_mode_msg[auth_mode]);

	return("ERROR");
}

static char wlan_scan_result[2048] = "";

irom static void wlan_scan_done_callback(void *arg, STATUS status)
{
	struct bss_info *bss;
	unsigned int length;
	char *dst;
	unsigned int size;
	bool_t first;

	dst = wlan_scan_result;
	size = sizeof(wlan_scan_result);

	length = snprintf(dst, size, "wlan scan result: %s\n", wlan_scan_status_to_string(status));
	dst += length;
	size -= length;

	first = true;

	for(bss = arg; bss; bss = bss->next.stqe_next)
	{
		if(first)
		{
			length = snprintf(dst, size, "> %-16s  %-4s  %-4s  %-18s  %-6s  %-6s  %s\n", "SSID", "CHAN", "RSSI", "AUTH", "HIDDEN", "OFFSET", "BSSID");
			dst += length;
			size -= length;
			first = false;
		}

		length = snprintf(dst, size, "> %-16s  %4u  %4d  %-18s  %-6s  %6d  %02x:%02x:%02x:%02x:%02x:%02x\n",
				bss->ssid,
				bss->channel,
				bss->rssi,
				wlan_scan_authmode_to_string(bss->authmode),
				yesno(bss->is_hidden),
				bss->freq_offset,
				bss->bssid[0], bss->bssid[1], bss->bssid[2], bss->bssid[3], bss->bssid[4], bss->bssid[5]);

		dst += length;
		size -= length;
	}
}

irom static app_action_t application_function_wlan_list(application_parameters_t ap)
{
	if(!*wlan_scan_result)
	{
		snprintf(ap.dst, ap.size, "wlan scan: no results (yet)\n");
		return(app_action_normal);
	}

	snprintf(ap.dst, ap.size, "%s", wlan_scan_result);

	return(app_action_normal);
}

irom static app_action_t application_function_wlan_scan(application_parameters_t ap)
{
	*wlan_scan_result = '\0';

	wifi_station_scan(0, wlan_scan_done_callback);

	snprintf(ap.dst, ap.size, "wlan scan started, use wlan-list to retrieve the results\n");

	return(app_action_normal);
}

irom static app_action_t application_function_ntp_dump(application_parameters_t ap)
{
	ip_addr_t addr;
	unsigned int length;
	int timezone;

	timezone = sntp_get_timezone();
	addr = sntp_getserver(0);
	length = snprintf(ap.dst, ap.size, "> server: ");
	ap.dst += length;
	ap.size -= length;
	length = ip_addr_to_string(ap.size, ap.dst, addr);
	ap.dst += length;
	ap.size -= length;
	snprintf(ap.dst, ap.size, "\n> time zone: GMT%c%u\n> ntp time: %s",
			timezone < 0 ? '-' : '+',
			timezone < 0 ? 0 - timezone : timezone,
			sntp_get_real_time(sntp_get_current_timestamp()));

	return(app_action_normal);
}

irom static app_action_t application_function_ntp_set(application_parameters_t ap)
{
	unsigned int length;

	config->ntp_server = string_to_ip_addr((*ap.args)[1]);
	config->ntp_timezone = atoi((*ap.args)[2]);

	length = snprintf(ap.dst, ap.size, "ntp server set, write config and restart to activate\n");
	ap.dst += length;
	ap.size -= length;

	return(app_action_normal);
}

static const application_function_table_t application_function_table[] =
{
	{
		"btp", "bridge-tcp-port",
		0,
		application_function_bridge_tcp_port,
		"set uart tcp bridge tcp port (default 25)"
	},
	{
		"cd", "config-dump",
		0,
		application_function_config_dump,
		"dump config contents"
	},
	{
		"cw", "config-write",
		0,
		application_function_config_write,
		"write config to non-volatile storage"
	},
	{
		"db", "display-brightness",
		1,
		application_function_display_brightness,
		"set or show display brightness"
	},
	{
		"dd", "display-dump",
		0,
		application_function_display_dump,
		"shows all displays"
	},
	{
		"ddm", "display-default-message",
		0,
		application_function_display_default_message,
		"set default message",
	},
	{
		"ds", "display-set",
		3,
		application_function_display_set,
		"put content on display <display id> <slot> <timeout> <text>"
	},
	{
		"gd", "gpio-dump",
		0,
		application_function_gpio_dump,
		"dump all gpio config"
	},
	{
		"gg", "gpio-get",
		1,
		application_function_gpio_get,
		"get gpio"
	},
	{
		"gm", "gpio-mode",
		0,
		application_function_gpio_mode,
		"get/set gpio mode (gpio, mode, parameters)",
	},
	{
		"gs", "gpio-set",
		1,
		application_function_gpio_set,
		"set gpio"
	},
    {
        "ia", "i2c-address",
        1,
        application_function_i2c_address,
        "set i2c slave address",
    },
    {
        "id", "i2c-delay",
        1,
        application_function_i2c_delay,
        "set i2c bit transaction delay (microseconds, default 5 ~ standard 100 kHz bus)",
    },
    {
        "ir", "i2c-read",
        1,
        application_function_i2c_read,
        "read data from i2c slave",
    },
    {
        "irst", "i2c-reset",
        0,
        application_function_i2c_reset,
        "i2c interface reset",
    },
    {
        "iw", "i2c-write",
        1,
        application_function_i2c_write,
        "write data to i2c slave",
    },
    {
        "isr", "i2c-sensor-read",
        1,
        application_function_i2c_sensor_read,
        "read from i2c sensor",
    },
    {
        "isc", "i2c-sensor-calibrate",
        3,
        application_function_i2c_sensor_calibrate,
        "calibrate i2c sensor, use sensor factor offset",
    },
    {
        "isd", "i2c-sensor-dump",
        0,
        application_function_i2c_sensor_dump,
        "dump all i2c sensors",
    },
    {
        "nd", "ntp-dump",
        0,
        application_function_ntp_dump,
        "dump ntp information",
    },
    {
        "ns", "ntp-set",
        2,
        application_function_ntp_set,
        "set ntp <ip addr> <timezone GMT+x>",
    },
	{
		"?", "help",
		0,
		application_function_help,
		"help [command]",
	},
	{
		"q", "quit",
		0,
		application_function_quit,
		"quit",
	},
	{
		"r", "reset",
		0,
		application_function_reset,
		"reset",
	},
	{
		"rs", "rtc-set",
		2,
		application_function_rtc_set,
		"set rtc [h m]",
	},
	{
		"s", "set",
		0,
		application_function_set,
		"set an option",
	},
	{
		"u", "unset",
		0,
		application_function_unset,
		"unset an option",
	},
	{
		"S", "stats",
		0,
		application_function_stats,
		"statistics",
	},
	{
		"ub", "uart-baud",
		1,
		application_function_uart_baud_rate,
		"set uart baud rate [1-1000000]",
	},
	{
		"ud", "uart-data",
		1,
		application_function_uart_data_bits,
		"set uart data bits [5/6/7/8]",
	},
	{
		"us", "uart-stop",
		1,
		application_function_uart_stop_bits,
		"set uart stop bits [1/2]",
	},
	{
		"up", "uart-parity",
		1,
		application_function_uart_parity,
		"set uart parity [none/even/odd]",
	},
	{
		"wl", "wlan-list",
		0,
		application_function_wlan_list,
		"retrieve results from wlan-scan"
	},
	{
		"ws", "wlan-scan",
		0,
		application_function_wlan_scan,
		"scan wlan, use wlan-list to retrieve the results"
	},
	{
		"", "",
		0,
		(void *)0,
		"",
	},
};
