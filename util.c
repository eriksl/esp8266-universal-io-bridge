#include "util.h"

#include <stdarg.h>
#include <stdint.h>

#include <mem.h>
#include <user_interface.h>

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

ICACHE_FLASH_ATTR const char *yesno(uint8_t value)
{
	if(!value)
		return("no");

	return("yes");
}

ICACHE_FLASH_ATTR const char *onoff(uint8_t value)
{
	if(!value)
		return("off");

	return("on");
}