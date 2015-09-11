#include "stats.h"

#include "util.h"
#include "config.h"

#include <c_types.h>
#include <user_interface.h>

uint32_t stat_uart_rx_interrupts;
uint32_t stat_uart_tx_interrupts;
uint32_t stat_timer_fast;
uint32_t stat_timer_slow;
uint32_t stat_timer_second;
uint32_t stat_background_task;
uint32_t stat_i2c_init_time_us;
uint32_t stat_display_init_time_us;

uint8_t	ut_days;
uint8_t	ut_hours;
uint8_t	ut_mins;
uint8_t	ut_secs;
uint8_t	ut_tens;

uint8_t	rt_days;
uint8_t	rt_hours;
uint8_t	rt_mins;
uint8_t	rt_secs;
uint8_t	rt_tens;

static const char *flash_map[] =
{
	"4 Mb map 256/256",
	"2 Mb no map",
	"8 Mb map 512/512",
	"16 Mb map 512/512",
	"32 Mb map 512/512",
	"16 Mb map 1024/1024",
	"32 Mb map 1024/1024",
	"unknown"
};

static const char *reset_map[] =
{
	"power on",
	"hardware watchdog",
	"exception",
	"software watchdog",
	"user reset",
	"deep sleep awake",
	"unknown"
};

static const char *phy[] = {
	"unknown",
	"802.11b",
	"802.11g",
	"802.11n",
	"unknown"
};

static const char *slp[] =
{
	"none",
	"light",
	"modem",
	"unknown"
};

irom void stats_generate(unsigned int size, char *dst)
{
	const struct rst_info *rst_info;
	struct station_config sc_default, sc_current;
	unsigned int system_time;

	system_time = system_get_time();
	rst_info = system_get_rst_info();

	wifi_station_get_config_default(&sc_default);
	wifi_station_get_config(&sc_current);

	snprintf(dst, size,
			"> firmware version date: %s\n"
			"> system id: %u\n"
			"> spi flash id: %u\n"
			"> cpu frequency: %u\n"
			"> flash map: %s\n"
			"> reset cause: %s\n"
			">\n"
			"> heap free: %u bytes\n"
			"> system clock: %u.%06u s\n"
			"> uptime: %u %02d:%02d:%02d\n"
			"> real time: %u %02d:%02d:%02d\n"
			"> size of config: %u\n"
			">\n"
			"> int uart rx: %u\n"
			"> int uart tx: %u\n"
			"> timer_fast fired: %u\n"
			"> timer_slow fired: %u\n"
			"> timer_second fired: %u\n"
			"> background task: %u\n"
			"> i2c initialisation time: %u us\n"
			"> display initialisation time: %u us\n"
			">\n"
			"> default ssid: %s, passwd: %s\n"
			"> current ssid: %s, passwd: %s\n"
			"> phy mode: %s\n"
			"> sleep mode: %s\n"
			"> channel: %u\n"
			"> signal strength: %d dB\n",
			__DATE__ " " __TIME__,
			system_get_chip_id(),
			spi_flash_get_id(),
			system_get_cpu_freq(),
			flash_map[system_get_flash_size_map()],
			reset_map[rst_info->reason],
			system_get_free_heap_size(),
			system_time / 1000000,
			system_time % 1000000,
			ut_days, ut_hours, ut_mins, ut_secs,
			rt_days, rt_hours, rt_mins, rt_secs,
			sizeof(config_t),
			stat_uart_rx_interrupts,
			stat_uart_tx_interrupts,
			stat_timer_fast,
			stat_timer_slow,
			stat_timer_second,
			stat_background_task,
			stat_i2c_init_time_us,
			stat_display_init_time_us,
			sc_default.ssid, sc_default.password,
			sc_current.ssid, sc_current.password,
			phy[wifi_get_phy_mode()],
			slp[wifi_get_sleep_type()],
			wifi_get_channel(),
			wifi_station_get_rssi());
}
