#include "application.h"
#include "application-wlan.h"

#include "stats.h"
#include "util.h"
#include "user_main.h"
#include "config.h"
#include "uart.h"

typedef struct
{
	const char	*command1;
	const char	*command2;
	uint8_t		required_args;
	uint8_t		(*function)(application_parameters_t);
	const char	*description;
} application_function_table_t;

static uint8_t application_function_config_dump(application_parameters_t ap);
static uint8_t application_function_config_write(application_parameters_t ap);
static uint8_t application_function_help(application_parameters_t ap);
static uint8_t application_function_print_debug(application_parameters_t ap);
static uint8_t application_function_quit(application_parameters_t ap);
static uint8_t application_function_reset(application_parameters_t ap);
static uint8_t application_function_stats(application_parameters_t ap);
static uint8_t application_function_strip_telnet(application_parameters_t ap);
static uint8_t application_function_uart_baud_rate(application_parameters_t ap);
static uint8_t application_function_uart_data_bits(application_parameters_t ap);
static uint8_t application_function_uart_stop_bits(application_parameters_t ap);
static uint8_t application_function_uart_parity(application_parameters_t ap);

static const application_function_table_t application_function_table[] =
{
	{
		"cd", "config-dump",
		0,
		application_function_config_dump,
		"dump config contents",
	},
	{
		"cw", "config-write",
		0,
		application_function_config_write,
		"write config to non-volatile storage",
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
		"pd", "print-debug",
		0,
		application_function_print_debug,
		"set system (wlan) output on uart at startup, on/off [0/1]",
	},
	{
		"r", "reset",
		0,
		application_function_reset,
		"reset",
	},
	{
		"s", "stats",
		0,
		application_function_stats,
		"statistics",
	},
	{
		"st", "strip-telnet",
		0,
		application_function_strip_telnet,
		"strip telnet do/dont [0/1]",
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
		"wd", "wlan-dump",
		0,
		application_function_wlan_dump,
		"dump wlan info",
	},
	{
		"", "",
		0,
		(void *)0,
		"",
	},
};

ICACHE_FLASH_ATTR void application_init(void)
{
}

ICACHE_FLASH_ATTR void application_periodic(void)
{
	stat_application_periodic++;
}

ICACHE_FLASH_ATTR uint8_t application_content(const char *src, uint16_t size, char *dst)
{
	static const char *error_fmt_unknown = "Command \"%s\" unknown\n";
	static const char *error_fmt_args = "Insufficient arguments: %d (%d required)\n";

	args_t	args;
	uint8_t args_count, arg_current;
	uint8_t src_current = 0;
	uint16_t src_left;
	uint8_t ws_skipped;
	const application_function_table_t *tableptr;

	*dst = '\0';

	if(src[0] == '\0')
		return(1);

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
		return(1);

	for(tableptr = application_function_table; tableptr->function; tableptr++)
		if(!strcmp(args[0], tableptr->command1) ||
			!strcmp(args[0], tableptr->command2))
			break;

	if(tableptr->function)
	{
		if(args_count < (tableptr->required_args + 1))
		{
			snprintf(dst, size, error_fmt_args, args_count - 1, tableptr->required_args);
			return(1);
		}

		application_parameters_t ap;

		ap.cmdline			= src;
		ap.nargs			= args_count;
		ap.args				= &args;
		ap.size				= size;
		ap.dst				= dst;

		return(tableptr->function(ap));
	}

	snprintf(dst, size, error_fmt_unknown, args[0]);
	return(1);
}

ICACHE_FLASH_ATTR static uint8_t application_function_config_dump(application_parameters_t ap)
{
	config_dump(ap.size, ap.dst);

	return(1);
}

ICACHE_FLASH_ATTR static uint8_t application_function_config_write(application_parameters_t ap)
{
	config_write();
	strlcpy(ap.dst, "config write OK\n", ap.size);

	return(1);
}

ICACHE_FLASH_ATTR static uint8_t application_function_help(application_parameters_t ap)
{
	const application_function_table_t *tableptr;
	uint8_t offset;

	for(tableptr = application_function_table; tableptr->function; tableptr++)
	{
		offset = snprintf(ap.dst, ap.size, "> %s/%s[%d]: %s\n",
				tableptr->command1, tableptr->command2,
				tableptr->required_args, tableptr->description);
		ap.dst	+= offset;
		ap.size	-= offset;
	}

	return(1);
}

ICACHE_FLASH_ATTR static uint8_t application_function_quit(application_parameters_t ap)
{
	return(0);
}

ICACHE_FLASH_ATTR static uint8_t application_function_reset(application_parameters_t ap)
{
	reset();

	return(1);
}

ICACHE_FLASH_ATTR static uint8_t application_function_stats(application_parameters_t ap)
{
	stats_generate(ap.size, ap.dst);

	return(1);
}

ICACHE_FLASH_ATTR static uint8_t application_function_strip_telnet(application_parameters_t ap)
{
	if(ap.nargs > 1)
		config.strip_telnet = !!atoi((*ap.args)[1]);

	snprintf(ap.dst, ap.size, "strip-telnet: %u\n", config.strip_telnet);

	return(1);
}

ICACHE_FLASH_ATTR static uint8_t application_function_print_debug(application_parameters_t ap)
{
	if(ap.nargs > 1)
		config.print_debug = !!atoi((*ap.args)[1]);

	snprintf(ap.dst, ap.size, "print-debug: %u\n", config.print_debug);

	return(1);
}

ICACHE_FLASH_ATTR static uint8_t application_function_uart_baud_rate(application_parameters_t ap)
{
	int baud_rate = atoi((*ap.args)[1]);

	if((baud_rate < 0) || (baud_rate > 1000000))
	{
		snprintf(ap.dst, ap.size, "uart-baud: out of range: %u\n", baud_rate);
		return(1);
	}

	config.uart.baud_rate = baud_rate;

	snprintf(ap.dst, ap.size, "uart-baud: %u (%s)\n", config.uart.baud_rate, uart_parameters_to_string(&config.uart));

	return(1);
}

ICACHE_FLASH_ATTR static uint8_t application_function_uart_data_bits(application_parameters_t ap)
{
	int data_bits = atoi((*ap.args)[1]);

	if((data_bits < 5) || (data_bits > 8))
	{
		snprintf(ap.dst, ap.size, "uart-data: out of range: %u\n", data_bits);
		return(1);
	}

	config.uart.data_bits = data_bits;

	snprintf(ap.dst, ap.size, "uart-data: %u (%s)\n", config.uart.data_bits, uart_parameters_to_string(&config.uart));

	return(1);
}

ICACHE_FLASH_ATTR static uint8_t application_function_uart_stop_bits(application_parameters_t ap)
{
	int stop_bits = atoi((*ap.args)[1]);

	if((stop_bits < 1) || (stop_bits > 2))
	{
		snprintf(ap.dst, ap.size, "uart-stop: out of range: %u\n", stop_bits);
		return(1);
	}

	config.uart.stop_bits = stop_bits;

	snprintf(ap.dst, ap.size, "uart-stop: %u (%s)\n", config.uart.stop_bits, uart_parameters_to_string(&config.uart));

	return(1);
}

ICACHE_FLASH_ATTR static uint8_t application_function_uart_parity(application_parameters_t ap)
{
	uint8_t parity = uart_string_to_parity((*ap.args)[1]);

	if(parity == parity_error)
	{
		snprintf(ap.dst, ap.size, "uart-parity: out of range: %s\n", (*ap.args)[1]);
		return(1);
	}

	config.uart.parity = parity;

	snprintf(ap.dst, ap.size, "uart-parity: %s (%s)\n", uart_parity_to_string(config.uart.parity), uart_parameters_to_string(&config.uart));

	return(1);
}
