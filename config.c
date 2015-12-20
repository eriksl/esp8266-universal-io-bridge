#include "config.h"

#include "util.h"
#include "gpios.h"

#include <ets_sys.h>
#include <c_types.h>
#include <spi_flash.h>

config_t *config = 0;
config_t *tmpconfig = 0;

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
		config_flag_disable_wlan_bootstrap,
		"db", "disable-wlan-bootstrap"
	},
	{
		config_flag_cpu_high_speed,
		"chs", "cpu-high-speed"
	},
	{
		config_flag_cpu_high_speed,
		"pfg", "phy-force-802.11g"
	},
	{
		config_flag_wlan_power_save,
		"wps", "wlan-power-save"
	},
};

irom bool_t config_init(void)
{
	if(!(config = (config_t *)malloc(sizeof(config_t))))
		return(false);

	if(!(tmpconfig = (config_t *)malloc(sizeof(config_t))))
		return(false);

	return(true);
}

irom attr_pure const char *config_flag_to_string(config_flag_enum_t id)
{
	if(id > config_flag_size)
		return("unknown");

	return(config_flag[id].long_name);
}

irom attr_pure config_flag_enum_t config_flag_to_id(const char *flag_name)
{
	const config_flag_t *entry;
	config_flag_enum_t current;

	for(current = 0; current < config_flag_size; current++)
	{
		entry = &config_flag[current];

		if(!strcmp(flag_name, entry->short_name) || !strcmp(flag_name, entry->long_name))
			break;
	}

	return(current);
}

irom attr_pure bool_t config_get_flag(config_flag_enum_t flag)
{
	if(flag < config_flag_size)
		return(config->flags & (1 << flag));

	return(false);
}

irom bool_t config_set_flag(config_flag_enum_t flag, bool_t onoff)
{
	if(flag < config_flag_size)
	{
		if(onoff)
			config->flags |= (1 << flag);
		else
			config->flags &= ~(1 << flag);

		return(true);
	}

	return(false);
}

irom bool_t attr_pure config_get_flag_by_name(const char *flag_name)
{
	return(config_get_flag(config_flag_to_id(flag_name)));
}

irom bool_t config_set_flag_by_name(const char *flag_name, bool_t value)
{
	return(config_set_flag(config_flag_to_id(flag_name), value));
}

irom unsigned int config_flags_to_string(unsigned int size, char *dst, unsigned int flags)
{
	config_flag_enum_t current;
	unsigned int length, total;

	if(size > 0)
		*dst = '\0';
	else
		return(0);

	total = 0;

	for(current = 0; current < config_flag_size; current++)
	{
		if((current != 0) && (size > 1))
		{
			*dst++ = ' ';
			size--;
			total++;
		}

		length = snprintf(dst, size, "%s:%s", config_flag[current].long_name, onoff(flags & 1 << current));
		dst += length;
		size -= length;
		total += length;
	}

	return(total);
}

iram void config_read_alt(config_t *cfg)
{
	enum
	{
		init_none,
		init_some,
		init_all
	} init;

	spi_flash_read(0x7a * SPI_FLASH_SEC_SIZE, (void *)cfg, sizeof(*cfg));

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
			cfg->ssid[0] = '\0';
			cfg->passwd[0] = '\0';
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
			cfg->bridge_tcp_port = 23;
			cfg->ntp_server = string_to_ip_addr("0.0.0.0");
			cfg->ntp_timezone = 0;
			cfg->i2c_delay = 5;
			strcpy(cfg->display_default_msg, "%%%%");
			cfg->stat_trigger_gpio = -1;
			gpios_config_init(&cfg->gpios);
			i2c_sensor_config_init(&cfg->i2c_sensors);

			break;
		}

		case(init_none):
		{
			break;
		}
	}
}

irom void config_read(void)
{
	return(config_read_alt(config));
}

iram void config_write_alt(config_t *cfg)
{
	ETS_UART_INTR_DISABLE();
	spi_flash_erase_sector(0x7a);
	spi_flash_write(0x7a * SPI_FLASH_SEC_SIZE, (void *)cfg, sizeof(*cfg));
	ETS_UART_INTR_ENABLE();
}

irom void config_write(void)
{
	config_write_alt(config);
}

irom void config_dump(unsigned int size, char *dst)
{
	unsigned int length;

	config_read_alt(tmpconfig);

	length = snprintf(dst, size,
			"> config magic: %04x\n"
			"> config major version: %d\n"
			"> config minor version: %d\n"
			"> wlan ssid: %s\n"
			"> wlan passwd: %s\n"
			"> bridge tcp port: %u\n"
			"> ntp time zone: GMT%c%u\n"
			"> i2c delay: %u\n"
			"> display default message: %s\n"
			"> status trigger gpio (-1 is disabled): %d\n"
			"> flags: ",
			tmpconfig->magic,
			tmpconfig->major_version,
			tmpconfig->minor_version,
			tmpconfig->ssid,
			tmpconfig->passwd,
			tmpconfig->bridge_tcp_port,
			tmpconfig->ntp_timezone >= 0 ? '+' : '-',
			tmpconfig->ntp_timezone >= 0 ? tmpconfig->ntp_timezone : 0 - tmpconfig->ntp_timezone,
			tmpconfig->i2c_delay,
			tmpconfig->display_default_msg,
			tmpconfig->stat_trigger_gpio);

	size -= length;
	dst += length;

	length = config_flags_to_string(size, dst, config->flags);
	size -= length;
	dst += length;

	length = snprintf(dst, size, "%s", "\n> ntp server: ");
	size -= length;
	dst += length;

	length = ip_addr_to_string(size, dst, tmpconfig->ntp_server);
	size -= length;
	dst += length;

	length = snprintf(dst, size, "%s", "\n> uart: ");
	size -= length;
	dst += length;

	length = uart_parameters_to_string(&tmpconfig->uart, size, dst);
	size -= length;
	dst += length;

	length = snprintf(dst, size, "%s", "\n> gpios:\n");
	size -= length;
	dst += length;

	gpios_dump_string(&tmpconfig->gpios, size, dst);
}
