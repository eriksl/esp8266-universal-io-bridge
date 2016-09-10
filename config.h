#ifndef config_h
#define config_h

#include "uart.h"
#include "util.h"

#include <stdint.h>

typedef enum
{
	config_wlan_mode_client,
	config_wlan_mode_ap
} config_wlan_mode_t;

typedef union
{
	struct
	{
		unsigned int strip_telnet:1;
		unsigned int print_debug:1;
		unsigned int tsl_high_sens:1;
		unsigned int bh_high_sens:1;
		unsigned int cpu_high_speed:1;
		unsigned int wlan_power_save:1;
		unsigned int enable_cfa634:1;
		unsigned int i2c_high_speed:1;
	} flag;

	uint32_t intval;
} config_flags_t;

config_flags_t	config_flags_get(void);
bool_t			config_flags_set(config_flags_t);
void			config_flags_to_string(string_t *);
bool_t			config_flags_change(const string_t *, bool_t add);

bool_t			config_get_string(const char *id, int index1, int index2, string_t *value);
bool_t			config_get_int(const char *id, int index1, int index2, int *value);
bool_t			config_set_string(const char *id, int index1, int index2, const string_t *value, int value_offset, int value_length);
bool_t			config_set_int(const char *id, int index1, int index2, int value);
unsigned int	config_delete(const char *id, int index1, int index2, bool_t wildcard);

bool_t			config_read(void);
unsigned int	config_write(void);
void			config_dump(string_t *);

#endif
