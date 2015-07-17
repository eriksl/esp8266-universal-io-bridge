#include "config.h"

#include "util.h"
#include "gpios.h"

#include <ets_sys.h>
#include <c_types.h>
#include <spi_flash.h>

enum
{
	config_magic = 0x4afb4afc,
	config_version = 10
};

typedef struct
{
	uint32_t magic;
	uint32_t version;
	config_t config;
} eeprom_t;

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
};

config_t config;

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

irom bool_t attr_pure config_get_flag_by_name(const char *flag_name)
{
	return(config_get_flag(config_flag_to_id(flag_name)));
}

irom bool_t config_set_flag_by_name(const char *flag_name, bool_t value)
{
	return(config_set_flag(config_flag_to_id(flag_name), value));
}

irom uint16_t config_flags_to_string(uint16_t size, char *dst, uint32_t flags)
{
	config_flag_enum_t current;
	uint16_t length, total;

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

irom static void config_init(config_t *cfg)
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
	gpios_config_init(cfg->gpios);
}

irom void config_read_alt(config_t *cfg)
{
	eeprom_t eeprom;

	spi_flash_read(0x3c * SPI_FLASH_SEC_SIZE, (void *)&eeprom, sizeof(eeprom));

	if((eeprom.magic == config_magic) && (eeprom.version == config_version))
		*cfg = eeprom.config;
	else
		config_init(cfg);
}

irom void config_read(void)
{
	config_read_alt(&config);
}

irom void config_write_alt(const config_t *cfg)
{
	eeprom_t eeprom;

	eeprom.magic = config_magic;
	eeprom.version = config_version;
	eeprom.config = *cfg;

	ETS_UART_INTR_DISABLE();
	spi_flash_erase_sector(0x3c);
	spi_flash_write(0x3c * SPI_FLASH_SEC_SIZE, (void *)&eeprom, sizeof(eeprom));
	ETS_UART_INTR_ENABLE();
}

irom void config_write(void)
{
	config_write_alt(&config);
}

irom void config_dump(uint16_t size, char *dst)
{
	uint16_t length;
	config_t cfg;

	config_read_alt(&cfg);

	length = snprintf(dst, size,
			"> wlan ssid: %s\n"
			"> wlan passwd: %s\n"
			"> flags: ",
			cfg.ssid,
			cfg.passwd);
	size -= length;
	dst += length;

	length = config_flags_to_string(size, dst, config.flags);
	size -= length;
	dst += length;

	length = snprintf(dst, size, "%s", "\n> uart: ");
	size -= length;
	dst += length;

	length = uart_parameters_to_string(&cfg.uart, size, dst);
	size -= length;
	dst += length;

	length = snprintf(dst, size, "%s", "\n> gpios:\n");
	size -= length;
	dst += length;

	gpios_dump_string(&cfg.gpios[0], size, dst);
}
