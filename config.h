#ifndef config_h
#define config_h

#include "i2c_sensor_config.h"
#include "uart.h"
#include "util.h"
#include "io_config.h"

#include <stdint.h>

#define DEFAULT_SSID "esp"
#define DEFAULT_PASSWD "espespesp"

enum
{
	config_magic = 0x4afc0002,
	config_version = 2,
};

typedef enum
{
	config_flag_strip_telnet,
	config_flag_print_debug,
	config_flag_tsl_high_sens,
	config_flag_bh_high_sens,
	config_flag_cpu_high_speed,
	config_flag_wlan_power_save,
	config_flag_enable_cfa634,
	config_flag_i2c_highspeed,
	config_flag_size
} config_flag_enum_t;

assert_size(config_flag_enum_t, 4);

typedef enum attr_packed
{
	config_wlan_mode_client,
	config_wlan_mode_ap
} config_wlan_mode_t;

assert_size(config_wlan_mode_t, 1);

typedef struct
{
	config_flag_enum_t id;
	const char *const short_name;
	const char *const long_name;
} config_flag_t;

typedef struct
{
	uint16_t port;
	uint16_t timeout;
} config_tcp_t;

typedef struct
{
	char ssid[32];
	char passwd[32];
	uint8_t	channel;
} config_ssid_t;

typedef struct
{
	uint32_t			magic;
	uint16_t			version;

	config_ssid_t		client_wlan;
	config_ssid_t		ap_wlan;
	config_wlan_mode_t	wlan_mode;

	struct
	{
		uint8_t	period;
	} pwm;

	struct
	{
		uint16_t padding;
	} padding;

	uint32_t			flags;
	uart_parameters_t	uart;

	config_tcp_t		bridge;
	config_tcp_t		command;

	config_io_t			status_trigger;
	config_io_t			assoc_trigger;

	struct
	{
		ip_addr_t	server;
		int8_t		timezone;
	} ntp;

	struct
	{
		uint16_t	flip_timeout;
		char		default_msg[32];
	} display;

	i2c_sensors_config_t	i2c_sensors;
	io_config_t				io_config;
} config_t;

_Static_assert(offsetof(config_t, flags) == 140, "offset");

extern config_t config;

bool_t	config_get_flag(config_flag_enum_t);
bool_t	config_set_flag(config_flag_enum_t, bool_t onoff);
bool_t	config_get_flag_by_name(const string_t *);
bool_t	config_set_flag_by_name(const string_t *, bool_t);
void	config_flags_to_string(string_t *, const char *, const char *, int);

void	config_read(config_t *);
void	config_write(config_t *);
void	config_dump(string_t *, const config_t *cfg);

bool_t	config_get_string(const char *id, int index1, int index2, string_t *value);
bool_t	config_get_int(const char *id, int index1, int index2, int *value);
bool_t	config_set_string(const char *id, int index1, int index2, const string_t *value, int value_offset, int value_length);
bool_t	config_set_int(const char *id, int index1, int index2, int value);
bool_t	config_delete(const char *id, int index1, int index2);

bool_t	config_export(const config_t *cfg, string_t *sector);
bool_t	config_write_text(const string_t *sector);
bool_t	config_read_text(string_t *sector);
bool_t	config_import(const string_t *sector);
void	config_dump_text(string_t *dst);

#endif
