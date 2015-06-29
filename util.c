#include "util.h"

#include "user_main.h"
#include "queue.h"
#include "uart.h"

#include <stdarg.h>
#include <stdint.h>

#include <mem.h>
#include <user_interface.h>

ICACHE_FLASH_ATTR int dprintf(const char *fmt, ...)
{
	static char buffer[64];
	va_list ap;
	int current, n;

	va_start(ap, fmt);
	n = ets_vsnprintf(buffer, sizeof(buffer), fmt, ap);
	va_end(ap);

	for(current = 0; current < n; current++)
		if(!queue_full(uart_send_queue))
			queue_push(uart_send_queue, buffer[current]);

	uart_start_transmit(!queue_empty(uart_send_queue));

	return(n);
}

ICACHE_FLASH_ATTR int snprintf(char *buffer, size_t size, const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = ets_vsnprintf(buffer, size, fmt, ap);
	va_end(ap);

	return(n);
}

ICACHE_FLASH_ATTR void *malloc(size_t size)
{
	return(os_malloc(size));
}

ICACHE_FLASH_ATTR void reset(void)
{
	system_restart();
}

ICACHE_FLASH_ATTR const char *yesno(bool_t value)
{
	if(!value)
		return("no");

	return("yes");
}

ICACHE_FLASH_ATTR const char *onoff(bool_t value)
{
	if(!value)
		return("off");

	return("on");
}
