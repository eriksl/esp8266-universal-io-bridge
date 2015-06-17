#include "stats.h"

#include "util.h"

#include <c_types.h>
#include <user_interface.h>

stats_t stat_uart_rx_interrupts = 0;
stats_t stat_uart_tx_interrupts = 0;
stats_t	stat_application_periodic = 0;

static const char *format_string =
{
	"cpu frequency: %u\n"
	"spi flash id: %u\n"
	"int uart rx: %u\n"
	"int uart tx: %u\n"
	"application periodic: %u\n"
	"heap free: %u\n"
};

ICACHE_FLASH_ATTR void stats_generate(size_t size, char *dst)
{
	snprintf(dst, size, format_string,
			system_get_cpu_freq(),
			spi_flash_get_id(),
			stat_uart_rx_interrupts,
			stat_uart_tx_interrupts,
			stat_application_periodic,
			system_get_free_heap_size());
}
