#include "config.h"

#include "util.h"
#include "io.h"

#include <ets_sys.h>
#include <c_types.h>
#include <spi_flash.h>

config_t config;
config_t tmpconfig;

static config_flag_t config_flag[config_flag_size] =
{
	{
		config_flag_strip_telnet,
		"st", "strip-telnet"
	},
	{
		config_flag_print_debug,
		"pd", "print-debug"
	},
	{
		config_flag_tsl_high_sens,
		"th", "tsl-high-sens"
	},
	{
		config_flag_bh_high_sens,
		"th", "bh-high-sens"
	},
	{
		config_flag_cpu_high_speed,
		"chs", "cpu-high-speed"
	},
	{
		config_flag_phy_force,
		"pf", "phy-force"
	},
	{
		config_flag_wlan_power_save,
		"wps", "wlan-power-save"
	},
	{
		config_flag_wlan_sdk_connect,
		"wsc", "wlan-sdk-connect"
	},
};

irom static attr_pure config_flag_enum_t config_flag_to_id(const string_t *flag_name)
{
	const config_flag_t *entry;
	config_flag_enum_t current;

	for(current = 0; current < config_flag_size; current++)
	{
		entry = &config_flag[current];

		if(string_match(flag_name, entry->short_name) || string_match(flag_name, entry->long_name))
			break;
	}

	return(current);
}

irom attr_pure bool_t config_get_flag(config_flag_enum_t flag)
{
	if(flag < config_flag_size)
		return(config.flags & (1 << flag));

	return(false);
}

irom bool_t config_set_flag(config_flag_enum_t flag, bool_t onoff)
{
	if(flag < config_flag_size)
	{
		if(onoff)
			config.flags |= (1 << flag);
		else
			config.flags &= ~(1 << flag);

		return(true);
	}

	return(false);
}

irom bool_t attr_pure config_get_flag_by_name(const string_t *flag_name)
{
	return(config_get_flag(config_flag_to_id(flag_name)));
}

irom bool_t config_set_flag_by_name(const string_t *flag_name, bool_t value)
{
	return(config_set_flag(config_flag_to_id(flag_name), value));
}

irom void config_flags_to_string(string_t *dst, const char *pre, const char *post, int flags)
{
	config_flag_enum_t current;

	if(pre)
		string_format(dst, "%s", pre);

	for(current = 0; current < config_flag_size; current++)
		string_format(dst, "%s%s:%s", string_length(dst) > 0 ? " " : "", config_flag[current].long_name, onoff(flags & 1 << current));

	if(post)
		string_format(dst, "%s", post);
}

irom void config_read(config_t *cfg)
{
	int io, pin;
	io_config_pin_entry_t *pin_config;

	enum
	{
		init_none,
		init_some,
		init_all
	} init;

	spi_flash_read(USER_CONFIG_SECTOR * SPI_FLASH_SEC_SIZE, (void *)cfg, sizeof(*cfg));

	if((cfg->magic == config_magic) && (cfg->major_version == config_major_version))
	{
		if(cfg->minor_version == config_minor_version)
			init = init_none;
		else
			init = init_some;
	}
	else
		init = init_all;

	switch(init)
	{
		case(init_all):
		{
			cfg->flags = 0;
			cfg->uart.baud_rate = 115200;
			cfg->uart.data_bits = 8;
			cfg->uart.parity = parity_none;
			cfg->uart.stop_bits = 1;
			cfg->flags = 0;
			config_set_flag(config_flag_print_debug, true);
		}

		case(init_some): // fall through
		{
			cfg->magic = config_magic;
			cfg->major_version = config_major_version;
			cfg->minor_version = config_minor_version;
			cfg->tcp_port.bridge = 23;
			cfg->tcp_port.command = 24;
			cfg->tcp_timeout.bridge = 0;
			cfg->tcp_timeout.command = 0;
			cfg->ntp.server = ip_addr("0.0.0.0");
			cfg->ntp.timezone = 0;
			strlcpy(cfg->display_default_msg, "%%%%", sizeof(cfg->display_default_msg));
			cfg->status_trigger_io.io = -1;
			cfg->status_trigger_io.pin = -1;
			i2c_sensor_config_init(&cfg->i2c_sensors);

			for(io = 0; io < io_id_size; io++)
			{
				for(pin = 0; pin < max_pins_per_io; pin++)
				{
					pin_config = &config.io_config[io][pin];

					pin_config->mode = io_pin_disabled;
					pin_config->llmode = io_pin_ll_disabled;
					pin_config->flags.autostart = 0;
					pin_config->flags.repeat = 0;
					pin_config->flags.pullup = 0;
					pin_config->flags.reset_on_read = 0;

					ets_memset(pin_config, 0, sizeof(*pin_config));

					// activate UART by default

					if((io == io_id_gpio) && ((pin == 1) || (pin == 3))) // shamefully hardcoded :-(
					{
						pin_config->mode = io_pin_uart;
						pin_config->llmode = io_pin_ll_uart;

						if(pin == 3)
							pin_config->flags.pullup = 1;
					}
				}
			}

			break;
		}

		case(init_none):
		{
			break;
		}
	}
}

irom void config_write(config_t *cfg)
{
	spi_flash_erase_sector(USER_CONFIG_SECTOR);
	spi_flash_write(USER_CONFIG_SECTOR * SPI_FLASH_SEC_SIZE, (void *)cfg, sizeof(*cfg));
}

irom void config_dump(string_t *dst, const config_t *cfg)
{
	string_new(static, ntp_server, 32);

	string_clear(&ntp_server);
	string_ip(&ntp_server, cfg->ntp.server);

	string_format(dst, 
			"> config magic: %04x\n"
			"> config major version: %d\n"
			"> config minor version: %d\n"
			"> wlan ssid: %s\n"
			"> wlan passwd: %s\n"
			"> bridge tcp port: %u\n"
			"> command tcp port: %u\n"
			"> bridge tcp timeout: %u\n"
			"> command tcp timeout: %u\n"
			"> ntp server: %s\n"
			"> ntp time zone: GMT%c%u\n"
			"> display default message: %s\n"
			"> status trigger gpio (-1 is disabled): %d/%d\n"
			"> flags: ", 
		cfg->magic,
		cfg->major_version,
		cfg->minor_version,
		cfg->ssid,
		cfg->passwd,
		cfg->tcp_port.bridge,
		cfg->tcp_port.command,
		cfg->tcp_timeout.bridge,
		cfg->tcp_timeout.command,
		string_to_ptr(&ntp_server),
		cfg->ntp.timezone >= 0 ? '+' : '-',
		cfg->ntp.timezone >= 0 ? cfg->ntp.timezone : 0 - cfg->ntp.timezone,
		cfg->display_default_msg,
		cfg->status_trigger_io.io,
		cfg->status_trigger_io.pin);

	config_flags_to_string(dst, 0, 0, cfg->flags);

	string_cat(dst, "\n> ntp server: ");
	string_ip(dst, cfg->ntp_server);
	string_cat(dst, "\n> uart: ");
	uart_parameters_to_string(dst, &cfg->uart);
	string_cat(dst, "\n> gpios:\n");
	io_config_dump(dst, cfg, -1, -1, false);
}
