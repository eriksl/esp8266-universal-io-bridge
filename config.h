#ifndef config_h
#define config_h

#include <stdint.h>

typedef struct
{
	unsigned int config_valid:1;
	unsigned int strip_telnet:1;
} config_t;

extern config_t config;

void config_read_alt(config_t *);
void config_read(void);
void config_write_alt(const config_t *);
void config_write(void);
void config_dump(uint16_t size, char *);

#endif
