#ifndef eeprom_h
#define eeprom_h

#include "config.h"

#include <stdint.h>

uint8_t	eeprom_read(config_t *);
void	eeprom_write(const config_t *);
void	eeprom_dump(uint16_t size, char *string);

#endif
