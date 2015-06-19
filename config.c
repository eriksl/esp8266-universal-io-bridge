#include "config.h"

#include "util.h"

#include <ets_sys.h>
#include <c_types.h>
#include <spi_flash.h>

enum
{
	config_magic = 0x4afb4afb
};

typedef struct
{
	uint32_t magic;
	config_t config;
} eeprom_t;

config_t config;

static void config_init(config_t *cfg)
{
	cfg->config_valid = 0;
	cfg->strip_telnet = 0;
	cfg->uart.baud_rate = 9600;
	cfg->uart.data_bits = 8;
	cfg->uart.parity = parity_none;
	cfg->uart.stop_bits = 1;
}

void config_read_alt(config_t *cfg)
{
	eeprom_t eeprom;

	spi_flash_read(0x3c * SPI_FLASH_SEC_SIZE, (void *)&eeprom, sizeof(eeprom));

	if(eeprom.magic == config_magic)
		*cfg = eeprom.config;
	else
		config_init(cfg);
}

void config_read(void)
{
	config_read_alt(&config);
}

void config_write_alt(const config_t *cfg)
{
	eeprom_t eeprom;

	eeprom.magic = config_magic;
	eeprom.config = *cfg;
	eeprom.config.config_valid = 1;

	ETS_UART_INTR_DISABLE();
	spi_flash_erase_sector(0x3c);
	spi_flash_write(0x3c * SPI_FLASH_SEC_SIZE, (void *)&eeprom, sizeof(eeprom));
	ETS_UART_INTR_ENABLE();
}

void config_write(void)
{
	config_write_alt(&config);
}

void config_dump(uint16_t size, char *string)
{
	config_t cfg;

	config_read_alt(&cfg);

	snprintf(string, size,
			"> config valid: %u\n"
			"> strip telnet: %u\n"
			"> print debug: %u\n"
			"> uart: %s\n",
			cfg.config_valid,
			cfg.strip_telnet,
			cfg.print_debug,
			uart_parameters_to_string(&cfg.uart));
}
