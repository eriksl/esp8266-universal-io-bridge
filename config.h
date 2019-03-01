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

enum
{
	flag_none =				0 << 0,
	flag_strip_telnet =		1 << 0,
	flag_log_to_uart =		1 << 1,
	flag_tsl_high_sens =	1 << 2,
	flag_bh_high_sens =		1 << 3,
	flag_cpu_high_speed =	1 << 4,
	flag_wlan_power_save =	1 << 5,
	flag_uart0_tx_inv =		1 << 6,
	flag_uart0_rx_inv =		1 << 7,
	flag_log_to_buffer =	1 << 8,
	flag_auto_sequencer =	1 << 9,
	flag_pwm1_extend =		1 << 10,
	flag_tmd_high_sens =	1 << 11,
	flag_apds3_high_sens =	1 << 12,
	flag_apds6_high_sens =	1 << 13,
	flag_uart1_tx_inv =		1 << 14,
};

void			config_flags_to_string(_Bool nl, const char *, string_t *);
_Bool			config_flags_change(const string_t *, _Bool add);

_Bool			config_get_string(const string_t *id, int index1, int index2, string_t *value);
_Bool			config_get_int(const string_t *id, int index1, int index2, uint32_t *value);
_Bool			config_set_string(const string_t *id, int index1, int index2, const string_t *value, int value_offset, int value_length);
_Bool			config_set_int(const string_t *id, int index1, int index2, uint32_t value);
unsigned int	config_delete(const string_t *id, int index1, int index2, _Bool wildcard);

_Bool			config_read(void);
unsigned int	config_write(void);
void			config_dump(string_t *);

attr_inline uint32_t config_flags_match(uint32_t match_flags)
{
	extern uint32_t flags_cache;
	return(!!(flags_cache & match_flags));
}
#endif
