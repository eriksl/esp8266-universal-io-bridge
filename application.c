#include "application.h"
#include "application-parameters.h"

#include "wlan.h"
#include "gpios.h"
#include "config.h"

#include "stats.h"
#include "util.h"
#include "user_main.h"
#include "config.h"
#include "uart.h"

#include <os_type.h>
#include <ets_sys.h>

typedef struct
{
	const char		*command1;
	const char		*command2;
	uint8_t			required_args;
	app_action_t	(*function)(application_parameters_t);
	const char		*description;
} application_function_table_t;

static app_action_t application_function_config_dump(application_parameters_t ap);
static app_action_t application_function_config_write(application_parameters_t ap);
static app_action_t application_function_help(application_parameters_t ap);
static app_action_t application_function_print_debug(application_parameters_t ap);
static app_action_t application_function_quit(application_parameters_t ap);
static app_action_t application_function_reset(application_parameters_t ap);
static app_action_t application_function_stats(application_parameters_t ap);
static app_action_t application_function_strip_telnet(application_parameters_t ap);
static app_action_t application_function_uart_baud_rate(application_parameters_t ap);
static app_action_t application_function_uart_data_bits(application_parameters_t ap);
static app_action_t application_function_uart_stop_bits(application_parameters_t ap);
static app_action_t application_function_uart_parity(application_parameters_t ap);

static ETSTimer application_periodic_timer;

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
		"?", "help",
		0,
		application_function_help,
		"help [command]",
	},
	{
		"pd", "print-debug",
		0,
		application_function_print_debug,
		"set system (wlan) output on uart at startup, on/off [0/1]",
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

void application_periodic(void)
{
	stat_application_periodic++;

	gpios_periodic();
}

static void application_periodic_timer_callback(void *arg)
{
	(void)arg;
	application_periodic();
}

ICACHE_FLASH_ATTR void application_init(config_t *config)
{
	os_timer_setfn(&application_periodic_timer, application_periodic_timer_callback, (void *)0);
	os_timer_arm(&application_periodic_timer, 100, 1);

	gpios_init();
}

ICACHE_FLASH_ATTR app_action_t application_content(const char *src, uint16_t size, char *dst)
{
	args_t	args;
	uint8_t args_count, arg_current;
	uint16_t src_current = 0, src_left;
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
		if(args_count < (tableptr->required_args + 1))
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

ICACHE_FLASH_ATTR static app_action_t application_function_config_dump(application_parameters_t ap)
{
	config_dump(ap.size, ap.dst);

	return(app_action_normal);
}

ICACHE_FLASH_ATTR static app_action_t application_function_config_write(application_parameters_t ap)
{
	config_write();
	strlcpy(ap.dst, "config write OK\n", ap.size);

	return(app_action_normal);
}

ICACHE_FLASH_ATTR static app_action_t application_function_help(application_parameters_t ap)
{
	const application_function_table_t *tableptr;
	uint16_t length;

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

ICACHE_FLASH_ATTR static app_action_t application_function_quit(application_parameters_t ap)
{
	return(app_action_disconnect);
}

ICACHE_FLASH_ATTR static app_action_t application_function_reset(application_parameters_t ap)
{
	return(app_action_reset);
}

ICACHE_FLASH_ATTR static app_action_t application_function_stats(application_parameters_t ap)
{
	stats_generate(ap.size, ap.dst);

	return(app_action_normal);
}

ICACHE_FLASH_ATTR static app_action_t application_function_strip_telnet(application_parameters_t ap)
{
	if(ap.nargs > 1)
		config.strip_telnet = !!atoi((*ap.args)[1]);

	snprintf(ap.dst, ap.size, "strip-telnet: %u\n", config.strip_telnet);

	return(app_action_normal);
}

ICACHE_FLASH_ATTR static app_action_t application_function_print_debug(application_parameters_t ap)
{
	if(ap.nargs > 1)
		config.print_debug = !!atoi((*ap.args)[1]);

	snprintf(ap.dst, ap.size, "print-debug: %u\n", config.print_debug);

	return(app_action_normal);
}

ICACHE_FLASH_ATTR static app_action_t application_function_uart_baud_rate(application_parameters_t ap)
{
	uint32_t baud_rate = atoi((*ap.args)[1]);

	if((baud_rate < 0) || (baud_rate > 1000000))
	{
		snprintf(ap.dst, ap.size, "uart-baud: out of range: %u\n", baud_rate);
		return(1);
	}

	config.uart.baud_rate = baud_rate;

	snprintf(ap.dst, ap.size, "uart-baud: %u\n", config.uart.baud_rate);

	return(app_action_normal);
}

ICACHE_FLASH_ATTR static app_action_t application_function_uart_data_bits(application_parameters_t ap)
{
	int data_bits = atoi((*ap.args)[1]);

	if((data_bits < 5) || (data_bits > 8))
	{
		snprintf(ap.dst, ap.size, "uart-data: out of range: %u\n", data_bits);
		return(1);
	}

	config.uart.data_bits = data_bits;

	snprintf(ap.dst, ap.size, "uart-data: %u\n", config.uart.data_bits);

	return(app_action_normal);
}

ICACHE_FLASH_ATTR static app_action_t application_function_uart_stop_bits(application_parameters_t ap)
{
	int stop_bits = atoi((*ap.args)[1]);

	if((stop_bits < 1) || (stop_bits > 2))
	{
		snprintf(ap.dst, ap.size, "uart-stop: out of range: %u\n", stop_bits);
		return(1);
	}

	config.uart.stop_bits = stop_bits;

	snprintf(ap.dst, ap.size, "uart-stop: %u\n", config.uart.stop_bits);

	return(app_action_normal);
}

ICACHE_FLASH_ATTR static app_action_t application_function_uart_parity(application_parameters_t ap)
{
	uart_parity_t parity = uart_string_to_parity((*ap.args)[1]);

	if(parity == parity_error)
	{
		snprintf(ap.dst, ap.size, "uart-parity: out of range: %s\n", (*ap.args)[1]);
		return(1);
	}

	config.uart.parity = parity;

	snprintf(ap.dst, ap.size, "uart-parity: %s\n", uart_parity_to_string(config.uart.parity));

	return(app_action_normal);
}
