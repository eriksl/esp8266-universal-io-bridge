#include "stats.h"

#include "util.h"

#include "c_types.h"

stats_t stat_uart_rx_interrupts = 0;
stats_t stat_uart_tx_interrupts = 0;
stats_t	stat_application_periodic = 0;

static const char *format_string =
{
	"int uart rx: %u\n"
	"int uart tx: %u\n"
	"application periodic: %u\n"
};

ICACHE_FLASH_ATTR void stats_generate(size_t size, char *dst)
{
	snprintf(dst, size, format_string,
			stat_uart_rx_interrupts,
			stat_uart_tx_interrupts,
			stat_application_periodic);
}
