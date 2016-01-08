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
	return(pvPortMalloc(size, "", 0));
}

irom void *zalloc(size_t size)
{
	return(pvPortZalloc(size, "", 0));
}

irom void *realloc(void *ptr, size_t size)
{
	return(pvPortRealloc(ptr, size, "", 0));
}

irom int memcmp(const void *s1, const void *s2, size_t s)
{
	return(ets_memcmp(s1, s2, s));
}

irom size_t strlen(const char *s)
{
	return(ets_strlen(s));
}

irom int strcmp(const char *s1, const char *s2)
{
	return(ets_strcmp(s1, s2));
}

irom int strncmp(const char *s1, const char *s2, size_t n)
{
	return(ets_strncmp(s1, s2, n));
}

irom void *memcpy(void *d, const void *s, size_t n)
{
	return(ets_memcpy(d, s, n));
}

irom void *memset(void *s, int c, size_t n)
{
	return(ets_memset(s, c, n));
}

irom char *strcpy(char *dest, const char *src)
{
	return(ets_strcpy(dest, src));
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

irom void msleep(unsigned int msec)
{
	while(msec-- > 0)
		os_delay_us(1000);
}

irom unsigned int double_to_string(double value, unsigned int precision, double top_decimal, unsigned int size, char *dst)
{
	double compare;
	unsigned int decimal;
	bool_t skip_leading_zeroes;
	unsigned int length;

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

			decimal = (unsigned int)(value / compare);
			value -= decimal * compare;

			if(length < size)
			{
				*dst = (char)(decimal + '0');
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

		if((unsigned int)compare == 1)
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
	unsigned int decimal;
	bool_t negative;

	if(*src == '-')
	{
		negative = true;
		src++;
	}
	else
		negative = false;

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

	return(negative ? 0 - result : result);
}

typedef union
{
	ip_addr_t	ip_addr;
	uint8_t		byte[3];
} ip_addr_to_bytes_t;

irom void split_ip_addr(ip_addr_t ip_addr, unsigned int byte[4])
{
	ip_addr_to_bytes_t ip_addr_to_bytes;

	ip_addr_to_bytes.ip_addr = ip_addr;

	byte[0] = ip_addr_to_bytes.byte[0];
	byte[1] = ip_addr_to_bytes.byte[1];
	byte[2] = ip_addr_to_bytes.byte[2];
	byte[3] = ip_addr_to_bytes.byte[3];
}

irom ip_addr_t join_ip_addr(const unsigned int byte[4])
{
	ip_addr_to_bytes_t ip_addr_to_bytes;

	ip_addr_to_bytes.byte[0] = byte[0];
	ip_addr_to_bytes.byte[1] = byte[1];
	ip_addr_to_bytes.byte[2] = byte[2];
	ip_addr_to_bytes.byte[3] = byte[3];

	return(ip_addr_to_bytes.ip_addr);
}

irom unsigned int ip_addr_to_string(unsigned int size, char *dst, ip_addr_t addr)
{
	unsigned int byte[4];

	split_ip_addr(addr, byte);
	return(snprintf(dst, size, "%u.%u.%u.%u", byte[0], byte[1], byte[2], byte[3]));
}

irom attr_pure ip_addr_t string_to_ip_addr(const char *src)
{
	ip_addr_to_bytes_t ip_addr_to_bytes;
	unsigned int ix;
	unsigned int current;

	current = 0;

	for(ix = 0; ix < 4; )
	{
		if(src && (*src >= '0') && (*src <= '9'))
		{
			current *= 10;
			current += *src - '0';
			src++;

			continue;
		}

		ip_addr_to_bytes.byte[ix++] = current;
		current = 0;

		if(src && (*src == '.'))
			src++;
	}

	return(ip_addr_to_bytes.ip_addr);
}

irom attr_pure bool ip_addr_valid(ip_addr_t ip_addr)
{
	ip_addr_to_bytes_t ip_addr_to_bytes;

	ip_addr_to_bytes.ip_addr = ip_addr;

	if(ip_addr_to_bytes.byte[0])
		return(true);

	if(ip_addr_to_bytes.byte[1])
		return(true);

	if(ip_addr_to_bytes.byte[2])
		return(true);

	if(ip_addr_to_bytes.byte[3])
		return(true);

	return(false);
}
