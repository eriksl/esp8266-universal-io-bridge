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

typedef struct
{
	unsigned int strip_telnet:1;
	unsigned int log_to_uart:1;
	unsigned int tsl_high_sens:1;
	unsigned int bh_high_sens:1;
	unsigned int cpu_high_speed:1;
	unsigned int wlan_power_save:1;
	unsigned int dummy_1:1;
	unsigned int dummy_2:1;
	unsigned int log_to_buffer:1;
	unsigned int auto_sequencer:1;
	unsigned int pwm1_extend:1;
	unsigned int tmd_high_sens:1;
	unsigned int apds3_high_sens:1;
	unsigned int apds6_high_sens:1;
} config_flags_t;

typedef union
{
	config_flags_t flags;
	uint32_t intval;
} _config_flags_t;

void			config_flags_to_string(string_t *);
_Bool			config_flags_change(const string_t *, _Bool add);

_Bool			config_get_string(const string_t *id, int index1, int index2, string_t *value);
_Bool			config_get_int(const string_t *id, int index1, int index2, uint32_t *value);
_Bool			config_set_string(const string_t *id, int index1, int index2, const string_t *value, int value_offset, int value_length);
_Bool			config_set_int(const string_t *id, int index1, int index2, uint32_t value);
unsigned int	config_delete(const string_t *id, int index1, int index2, _Bool wildcard);

_Bool			config_read(void);
unsigned int	config_write(void);
void			config_dump(string_t *);


attr_inline config_flags_t config_flags_get(void)
{
	extern _config_flags_t flags_cache;
	return(flags_cache.flags);
}
#endif
