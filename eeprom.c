#include "eeprom.h"

#include "util.h"
#include "esp-missing-decls.h"

#include <ets_sys.h>
#include <c_types.h>
#include <spi_flash.h>

enum
{
	eeprom_magic = 0x4afb4afb
};

typedef struct
{
	uint32_t magic;
	config_t config;
} eeprom_t;

uint8_t eeprom_read(config_t *cfg)
{
	eeprom_t eeprom;

	spi_flash_read(0x3c * SPI_FLASH_SEC_SIZE, (void *)&eeprom, sizeof(eeprom));

	if(eeprom.magic != eeprom_magic)
		return(0);

	*cfg = eeprom.config;
	return(1);
}

void eeprom_write(const config_t *cfg)
{
	eeprom_t eeprom;

	eeprom.magic = eeprom_magic;
	eeprom.config = *cfg;

	ETS_UART_INTR_DISABLE();
	spi_flash_erase_sector(0x3c);
	spi_flash_write(0x3c * SPI_FLASH_SEC_SIZE, (void *)&eeprom, sizeof(eeprom));
	ETS_UART_INTR_ENABLE();
}

void eeprom_dump(uint16_t size, char *string)
{
	config_t cfg;

	if(eeprom_read(&cfg))
	{
		snprintf(string, size,
				"> strip_telnet: %d\n",
				cfg.strip_telnet);
	}
	else
		strlcpy(string, "eeprom read ERROR\n", size);
}
