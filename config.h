#ifndef config_h
#define config_h

#include "uart.h"
#include "gpios.h"
#include "util.h"
#include "i2c_sensor.h"

#include <stdint.h>

enum
{
	config_magic = 0x4afc0001,
	config_major_version = 1,
	config_minor_version = 11
};

typedef enum
{
	config_flag_strip_telnet,
	config_flag_print_debug,
	config_flag_tsl_high_sens,
	config_flag_bh_high_sens,
	config_flag_disable_wlan_bootstrap,
	config_flag_cpu_high_speed,
	config_flag_phy_force,
	config_flag_wlan_power_save,
	config_flag_wlan_sdk_connect,
	config_flag_size
} config_flag_enum_t;

_Static_assert(sizeof(config_flag_enum_t) == 4, "sizeof(config_flag_enum_t) != 4");

typedef struct
{
	config_flag_enum_t id;
	const char *const short_name;
	const char *const long_name;
} config_flag_t;

typedef struct
{
	uint32_t			magic;
	uint16_t			major_version;
	uint16_t			minor_version;
	char				ssid[32];
	char				passwd[32];
	uint32_t			flags;
	uart_parameters_t	uart;
	uint16_t			bridge_tcp_port;
	ip_addr_t			ntp_server;
	int8_t				ntp_timezone;
	uint8_t				i2c_delay;
	char				display_default_msg[32];
	int8_t				stat_trigger_gpio;
	int8_t				wlan_trigger_gpio;
	gpio_config_t		gpios;
	i2c_sensor_config_t	i2c_sensors;
} config_t;

extern config_t config;
extern config_t tmpconfig;

bool_t	config_get_flag(config_flag_enum_t);
bool_t	config_set_flag(config_flag_enum_t, bool_t onoff);
bool_t	config_get_flag_by_name(const string_t *);
bool_t	config_set_flag_by_name(const string_t *, bool_t);
void	config_flags_to_string(string_t *, const char *, const char *, int);

void	config_read(config_t *);
void	config_write(config_t *);
void	config_dump(string_t *, const config_t *cfg);

#endif
