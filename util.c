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
	static char buffer[128];
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

ICACHE_FLASH_ATTR void msleep(uint16_t msec)
{
	while(msec-- > 0)
		os_delay_us(1000);
}

ICACHE_FLASH_ATTR uint16_t double_to_string(double value, uint8_t precision, double top_decimal, uint16_t size, char *dst)
{
	double compare;
	uint8_t decimal;
	bool_t leading_space;
	uint16_t length;

	if(--size < 1)
		return(0);

	length = 0;

	if(value == 0)
	{
		if(length < size)
		{
			*dst = '0';
			dst++;
			length++;
		}

		*dst = '\0';
		return(length);
	}

	if(value < 0)
	{
		if(length < size)
		{
			*dst = '-';
			dst++;
			length++;
		}

		value = 0 - value;
	}

	leading_space = true;

	if(value > (10 * top_decimal))
	{
		if((length + 3) < size)
		{
			*dst++ = '+';
			*dst++ = '+';
			*dst++ = '+';
			length += 3;
		}

		*dst = '\0';
		return(length);
	}

	for(compare = top_decimal; compare > 0; compare /= 10)
	{
		if(compare <= value)
		{
			leading_space = false;

			decimal = (uint8_t)(value / compare);
			value -= decimal * compare;

			if(length < size)
			{
				*dst = (uint8_t)(decimal + '0');
				dst++;
				length++;
			}
		}
		else
		{
			if(!leading_space)
			{
				if(length < size)
				{
					*dst = '0';
					dst++;
					length++;
				}
			}
		}

		if((compare <= 1) && (precision == 0))
			break;

		if(compare == 1)
		{
			if(length < size)
			{
				*dst = '.';
				dst++;
				length++;
			}
		}

		if((compare <= 1) && (precision > 0))
			--precision;
	}

	*dst = '\0';
	return(length);
}
