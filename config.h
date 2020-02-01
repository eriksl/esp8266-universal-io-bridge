#ifndef config_h
#define config_h

#include "uart.h"
#include "util.h"
#include "stats.h"

#include <stdint.h>
#include <stdbool.h>

typedef enum
{
	config_wlan_mode_client,
	config_wlan_mode_ap
} config_wlan_mode_t;

enum
{
	flag_none =					0 << 0,
	flag_strip_telnet =			1 << 0,
	flag_log_to_uart =			1 << 1,
	flag_tsl_high_sens =		1 << 2,
	flag_bh_high_sens =			1 << 3,
	flag_cpu_high_speed =		1 << 4,
	flag_wlan_power_save =		1 << 5,
	flag_enable_eastrising =	1 << 6,
	flag_eastrising_fontchip =	1 << 7,
	flag_log_to_buffer =		1 << 8,
	flag_auto_sequencer =		1 << 9,
	flag_pwm1_extend =			1 << 10,
	flag_tmd_high_sens =		1 << 11,
	flag_apds3_high_sens =		1 << 12,
	flag_apds6_high_sens =		1 << 13,
	flag_dummy2 =				1 << 14,
	flag_udp_term_empty =		1 << 15,
	flag_enable_orbital =		1 << 16,
	flag_cmd_from_uart =		1 << 17,
	flag_log_to_display =		1 << 18,
	flag_ssd_height_32 =		1 << 19,
};

void			config_flags_to_string(bool nl, const char *, string_t *);
bool			config_flag_change(unsigned int flag, bool set);
void			config_flag_change_nosave(unsigned int flag, bool set);
bool			config_flag_change_from_string(const string_t *, bool set);

bool			config_init(void);
bool			config_dump(string_t *);
bool			config_open_read(void);
bool			config_walk(string_t *id, string_t *value);
bool			config_close_read(void);

bool			config_open_write(void);
unsigned int	config_delete_flashptr(const char *match_name, bool wildcard, int index1, int index2);
bool			config_set_string_flashptr(const char *id, const char *value, int param1, int param2);
bool			config_set_int_flashptr(const char *match_name, int value, int index1, int index2);
bool			config_set_uint_flashptr(const char *match_name, unsigned int value, int index1, int index2);
bool			config_close_write(void);
void			config_abort_write(void);

bool			config_get_string_flashptr(const char *id, string_t *value, int param1, int param2);
bool			config_get_int_flashptr(const char *match_name, int *return_value, int param1, int param2);
bool			config_get_uint_flashptr(const char *match_name, unsigned int *return_value, int param1, int param2);

#define config_get_string(name, value, p1, p2) \
({ \
	static roflash const char name_flash[] = name; \
	config_get_string_flashptr(name_flash, value, p1, p2); \
})

#define config_get_int(name, value, p1, p2) \
({ \
	static roflash const char name_flash[] = name; \
	config_get_int_flashptr(name_flash, value, p1, p2); \
})

#define config_get_uint(name, value, p1, p2) \
({ \
	static roflash const char name_flash[] = name; \
	config_get_uint_flashptr(name_flash, value, p1, p2); \
})

#define config_delete(name, wildcard, p1, p2) \
({ \
	static roflash const char name_flash[] = name; \
	config_delete_flashptr(name_flash, wildcard, p1, p2); \
})

#define config_set_string(name, value, p1, p2) \
({ \
	static roflash const char name_flash[] = name; \
	config_set_string_flashptr(name_flash, value, p1, p2); \
})

#define config_set_int(name, value, p1, p2) \
({ \
	static roflash const char name_flash[] = name; \
	config_set_int_flashptr(name_flash, value, p1, p2); \
})

#define config_set_uint(name, value, p1, p2) \
({ \
	static roflash const char name_flash[] = name; \
	config_set_uint_flashptr(name_flash, value, p1, p2); \
})

attr_inline uint32_t config_flags_match(uint32_t match_flags)
{
	extern unsigned int config_flags;
	return(!!(config_flags & match_flags));
}
#endif
