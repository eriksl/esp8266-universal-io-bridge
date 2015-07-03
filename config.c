#include "config.h"

#include "util.h"
#include "gpios.h"

#include <ets_sys.h>
#include <c_types.h>
#include <spi_flash.h>

enum
{
	config_magic = 0x4afb4afc,
	config_version = 5
};

typedef struct
{
	uint32_t magic;
	uint32_t version;
	config_t config;
} eeprom_t;

config_t config;

ICACHE_FLASH_ATTR static void config_init(config_t *cfg)
{
	cfg->config_valid = 0;
	cfg->strip_telnet = 0;
	cfg->uart.baud_rate = 9600;
	cfg->uart.data_bits = 8;
	cfg->uart.parity = parity_none;
	cfg->uart.stop_bits = 1;

	gpios_config_init(cfg->gpios);
}

ICACHE_FLASH_ATTR void config_read_alt(config_t *cfg)
{
	eeprom_t eeprom;

	spi_flash_read(0x3c * SPI_FLASH_SEC_SIZE, (void *)&eeprom, sizeof(eeprom));

	if((eeprom.magic == config_magic) && (eeprom.version == config_version))
		*cfg = eeprom.config;
	else
		config_init(cfg);
}

ICACHE_FLASH_ATTR void config_read(void)
{
	config_read_alt(&config);
}

ICACHE_FLASH_ATTR void config_write_alt(const config_t *cfg)
{
	eeprom_t eeprom;

	eeprom.magic = config_magic;
	eeprom.version = config_version;
	eeprom.config = *cfg;
	eeprom.config.config_valid = 1;

	ETS_UART_INTR_DISABLE();
	spi_flash_erase_sector(0x3c);
	spi_flash_write(0x3c * SPI_FLASH_SEC_SIZE, (void *)&eeprom, sizeof(eeprom));
	ETS_UART_INTR_ENABLE();
}

ICACHE_FLASH_ATTR void config_write(void)
{
	config_write_alt(&config);
}

ICACHE_FLASH_ATTR void config_dump(uint16_t size, char *string)
{
	uint16_t length;
	config_t cfg;

	config_read_alt(&cfg);

	length = snprintf(string, size,
			"> config valid: %s\n"
			"> strip telnet: %s\n"
			"> print debug: %s\n"
			"> uart: ",
			yesno(cfg.config_valid),
			onoff(cfg.strip_telnet),
			onoff(cfg.print_debug));
	size -= length;
	string += length;

	length = uart_parameters_to_string(&cfg.uart, size, string);
	size -= length;
	string += length;

	strlcpy(string, "\n", size);
	length = strlen(string);
	size -= length;
	string += length;

	gpios_dump_string(&cfg.gpios[0], size, string);
}
