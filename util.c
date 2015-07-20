#include "util.h"

#include "user_main.h"
#include "queue.h"
#include "uart.h"

#include <stdarg.h>
#include <stdint.h>

#include <mem.h>
#include <user_interface.h>

irom int dprintf(const char *fmt, ...)
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

irom int snprintf(char *buffer, size_t size, const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = ets_vsnprintf(buffer, size, fmt, ap);
	va_end(ap);

	return(n);
}

irom void pin_func_select(uint32_t pin_name, uint32_t pin_func)
{
	uint32_t pin_value;

	pin_value = READ_PERI_REG(pin_name);
	pin_value &= ~(PERIPHS_IO_MUX_FUNC << PERIPHS_IO_MUX_FUNC_S);
	pin_value |= (pin_func & (1 << 2)) << (PERIPHS_IO_MUX_FUNC_S + 2);
	pin_value |= (pin_func & (1 << 1)) << (PERIPHS_IO_MUX_FUNC_S + 0);
	pin_value |= (pin_func & (1 << 0)) << (PERIPHS_IO_MUX_FUNC_S + 0);

	WRITE_PERI_REG(pin_name, pin_value);
}

irom void *malloc(size_t size)
{
	return(os_malloc(size));
}

irom void reset(void)
{
	system_restart();
}

irom attr_const const char *yesno(bool_t value)
{
	if(!value)
		return("no");

	return("yes");
}

irom attr_const const char *onoff(bool_t value)
{
	if(!value)
		return("off");

	return("on");
}

irom void msleep(uint16_t msec)
{
	while(msec-- > 0)
		os_delay_us(1000);
}

irom uint16_t double_to_string(double value, uint8_t precision, double top_decimal, uint16_t size, char *dst)
{
	double compare;
	uint8_t decimal;
	bool_t skip_leading_zeroes;
	uint16_t length;

	if(--size < 1)
		return(0);

	length = 0;

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

	skip_leading_zeroes = true;

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
		if(value >= compare)
		{
			skip_leading_zeroes = false;

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
			if(!skip_leading_zeroes)
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

		if((uint32_t)compare == 1)
		{
			if(skip_leading_zeroes)
			{
				if(length < size)
				{
					*dst = '0';
					dst++;
					length++;
				}

				skip_leading_zeroes = false;
			}

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

	if(length == 0)
	{
		if(length < size)
		{
			*dst = '0';
			dst++;
			length++;
		}
	}

	*dst = '\0';
	return(length);
}

irom attr_pure double string_to_double(const char *src)
{
	double result;
	uint32_t decimal;

	for(result = 0, decimal = 0; *src; src++)
	{
		if((*src == '.') || (*src == ','))
		{
			if(decimal == 0)
				decimal = 1;
			else
				break;
		}
		else
		{
			if((*src < '0') || (*src > '9'))
			{
				break;
			}
			else
			{
				if(decimal > 0)
				{
					decimal *= 10;
					result += (double)(*src - '0') / (double)decimal;
				}
				else
				{
					result *= 10;
					result += (double)(*src - '0');
				}
			}
		}
	}

	return(result);
}
