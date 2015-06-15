#ifndef config_h
#define config_h

typedef struct
{
	unsigned int strip_telnet:1;
} config_t;

extern config_t config;

#endif
