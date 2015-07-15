#include "stats.h"

#include "util.h"

#include <c_types.h>
#include <user_interface.h>

stats_t stat_uart_rx_interrupts = 0;
stats_t stat_uart_tx_interrupts = 0;
stats_t	stat_timer = 0;
stats_t	stat_background_task = 0;
stats_t stat_application_periodic = 0;
stats_t stat_application_periodic_wrapped = 0;

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

irom void stats_generate(uint16_t size, char *dst)
{
	const struct rst_info *rst_info;

	rst_info = system_get_rst_info();

	snprintf(dst, size,
			"> firmware version date: %s\n"
			"> system id: %u\n"
			"> spi flash id: %u\n"
			"> cpu frequency: %u\n"
			"> flash map: %s\n"
			"> reset cause: %s\n"
			">\n"
			"> heap free: %u kb\n"
			"> uptime: %u s\n"
			">\n"
			"> int uart rx: %u\n"
			"> int uart tx: %u\n"
			"> timer fired: %u\n"
			"> background task: %u\n"
			"> application periodic task: %u\n"
			"> application periodic task timer wrap: %u\n",
			__DATE__ " " __TIME__,
			system_get_chip_id(),
			spi_flash_get_id(),
			system_get_cpu_freq(),
			flash_map[system_get_flash_size_map()],
			reset_map[rst_info->reason],
			system_get_free_heap_size(),
			system_get_time() / 1000000,
			stat_uart_rx_interrupts,
			stat_uart_tx_interrupts,
			stat_timer,
			stat_background_task,
			stat_application_periodic,
			stat_application_periodic_wrapped);
}
