#include "util.h"
#include "uart.h"
#include "ota.h"
#include "config.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>

#include <mem.h>
#include <user_interface.h>

#if IMAGE_OTA != 0
#include <rboot/rboot.h>
#endif

char flash_dram_buffer[1024];

string_t logbuffer =
{
	.size = 0x3fffeb2c - 0x3fffe000 - 16,
	.length = 0,
	.buffer = (char *)0x3fffe000,
};

int __errno;

// functions missing from SDK libmain (but declared in headers)

#ifdef isxdigit
undef isxdigit
#endif

attr_const int isxdigit(int c);
irom attr_const int isxdigit(int c)
{
	if((c >= '0' && (c <= '9')))
		return(1);

	if((c >= 'a' && (c <= 'f')))
		return(1);

	if((c >= 'A' && (c <= 'F')))
		return(1);

	return(0);
}

/* from http://www.leidinger.net/freebsd/dox/libkern/html/d9/dd9/memchr_8c_source.html */
irom void *memchr(const void *s, int c, size_t n)
{
	if(n != 0)
	{
		const unsigned char *p = s;

		do
		{
			if(*p++ == (unsigned char)c)
				return((void *)(uintptr_t)(p - 1));
		} while (--n != 0);
	}

	return(0);
}

// convenience functions

iram int strecpy(char *dst, const char *src, int size)
{
	int length = strlen(src);

	if(length >= size)
		length = size - 1;

	if(length < 0)
		length = 0;

	memcpy(dst, src, length);
	dst[length] = '\0';

	return(length);
}

iram size_t strecpy_from_flash(char *dst, const uint32_t *src_flash, int size)
{
	int from, to, byte, current8;
	uint32_t current32;

	for(from = 0, to = 0; (int)(from * sizeof(*src_flash)) < (size - 1); from++)
	{
		current32 = src_flash[from];

		for(byte = 4; byte > 0; byte--)
		{
			current8 = current32 & 0x000000ff;

			if(((to + 1) >= size) || (current8 == '\0'))
				goto done;

			dst[to++] = (char)current8;
			current32 = (current32 >> 8) & 0x00ffffff;
		}
	}

done:
	dst[to] = '\0';

	return(to);
}

irom void reset(void)
{
	system_restart();
}

irom attr_const const char *yesno(_Bool value)
{
	if(!value)
		return("no");

	return("yes");
}

irom attr_const const char *onoff(_Bool value)
{
	if(!value)
		return("off");

	return("on");
}

#if IMAGE_OTA == 1
irom attr_const const char *rboot_boot_mode(unsigned int index)
{
	switch(index)
	{
		case(MODE_STANDARD): return("standard");
		case(MODE_GPIO_ROM): return("gpio_rom");
		case(MODE_TEMP_ROM): return("temp_rom");
		case(MODE_GPIO_ERASES_SDKCONFIG): return("erase_sdk_config");
		case(MODE_GPIO_SKIP): return("gpio_skip");
		default: return("<unknown>");
	}
}
#endif

irom int log_from_flash(const char *fmt_in_flash, ...)
{
	va_list ap;
	int current, written;
	char fmt_in_dram[128];

	strecpy_from_flash(fmt_in_dram, (const uint32_t *)(const void *)fmt_in_flash, sizeof(fmt_in_dram));

	va_start(ap, fmt_in_flash);
	written = ets_vsnprintf(flash_dram_buffer, sizeof(flash_dram_buffer), fmt_in_dram, ap);
	va_end(ap);

	if(flags_cache.flag.log_to_uart)
	{
		for(current = 0; current < written; current++)
			uart_send(0, flash_dram_buffer[current]);

		uart_flush(0);
	}

	if(flags_cache.flag.log_to_buffer)
	{
		if((string_length(&logbuffer) + written) >= string_size(&logbuffer))
			string_clear(&logbuffer);

		string_append_cstr(&logbuffer, flash_dram_buffer);
	}

	return(written);
}

iram attr_speed void logchar(char c)
{
	if(flags_cache.flag.log_to_uart)
	{
		uart_send(0, c);
		uart_flush(0);
	}

	if(flags_cache.flag.log_to_buffer)
	{
		if((logbuffer.length + 1) >= logbuffer.size)
			string_clear(&logbuffer);

		string_append_char(&logbuffer, c);
	}
}

irom void msleep(int msec)
{
	while(msec-- > 0)
	{
		system_soft_wdt_feed();
		os_delay_us(1000);
	}
}

irom attr_pure ip_addr_t ip_addr(const char *src)
{
	ip_addr_to_bytes_t ip_addr_to_bytes;
	int ix, current;

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

irom parse_error_t parse_string(int index, const string_t *src, string_t *dst, char delimiter)
{
	uint8_t current;
	int offset;

	if((offset = string_sep(src, 0, index, delimiter)) < 0)
		return(parse_out_of_range);

	for(; offset < src->length; offset++)
	{
		current = string_at(src, offset);

		if(current == delimiter)
			break;

		if((current > ' ') && (current <= '~'))
			string_append_char(dst, current);
	}

	return(parse_ok);
}

irom parse_error_t parse_int_all(int index, const string_t *src, uint32_t *dst, int base, char delimiter, _Bool do_signed)
{
	_Bool valid, negative;
	uint32_t value;
	int offset;
	char current;

	negative = false;
	value = 0;
	valid = false;

	if((offset = string_sep(src, 0, index, delimiter)) < 0)
		return(parse_out_of_range);

	if(base == 0)
	{
		if(((offset + 1) < src->length) &&
				(string_at(src, offset) == '0') &&
				(string_at(src, offset + 1) == 'x'))
		{
			base = 16;
			offset += 2;
		}
		else
			base = 10;
	}

	if((offset < src->length) && (base == 10))
	{
		if(string_at(src, offset) == '-')
		{
			negative = true;
			offset++;
		}

		if(string_at(src, offset) == '+')
			offset++;
	}

	for(; offset < src->length; offset++)
	{
		current = string_at(src, offset);

		if((current >= 'A') && (current <= 'Z'))
			current |= 0x20;

		if((current >= '0') && (current <= '9'))
		{
			value *= base;
			value += current - '0';
		}
		else
		{
			if((base > 10) && (current >= 'a') && (current <= ('a' + base - 11)))
			{
				value *= base;
				value += current - 'a' + 10;
			}
			else
			{
				if((current != '\0') && (current != delimiter) && (current != '\n') && (current != '\r'))
					valid = false;

				break;
			}
		}

		valid = true;
	}

	if(!valid)
		return(parse_invalid);

	if(do_signed && negative)
		*dst = 0 - value;
	else
		*dst = value;

	return(parse_ok);
}

irom parse_error_t parse_float(int index, const string_t *src, double *dst, char delimiter)
{
	int offset;
	int decimal;
	_Bool negative;
	_Bool valid;
	double result;
	char current;

	valid = false;
	negative = false;
	offset = 0;
	result = 0;
	decimal = 0;

	if((offset = string_sep(src, 0, index, delimiter)) < 0)
		return(parse_out_of_range);

	if((offset < src->length) && (string_at(src, offset) == '-'))
	{
		negative = true;
		offset++;
	}

	for(; offset < src->length; offset++)
	{
		current = string_at(src, offset);

		if((current == '.') || (current == ','))
		{
			if(decimal == 0)
				decimal = 1;
			else
				break;
		}
		else
		{
			if((current < '0') || (current > '9'))
			{
				break;
			}
			else
			{
				valid = true;

				if(decimal > 0)
				{
					decimal *= 10;
					result += (double)(current - '0') / (double)decimal;
				}
				else
				{
					result *= 10;
					result += (double)(current - '0');
				}
			}
		}
	}

	if(!valid)
		return(parse_invalid);

	if(negative)
		*dst = 0 - result;
	else
		*dst = result;

	return(parse_ok);
}

irom void string_format_cstr(string_t *dst, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	dst->length += ets_vsnprintf(dst->buffer + dst->length, dst->size - dst->length - 1, fmt, ap);
	va_end(ap);

	if(dst->length > (dst->size - 1))
		dst->length = dst->size - 1;

	dst->buffer[dst->length] = '\0';
}

iram attr_speed void string_format_flash_ptr(string_t *dst, const char *fmt_flash, ...)
{
	va_list ap;

	strecpy_from_flash(flash_dram_buffer, (const uint32_t *)(const void *)fmt_flash, sizeof(flash_dram_buffer));

	va_start(ap, fmt_flash);
	dst->length += ets_vsnprintf(dst->buffer + dst->length, dst->size - dst->length - 1, flash_dram_buffer, ap);
	va_end(ap);

	if(dst->length > (dst->size - 1))
		dst->length = dst->size - 1;

	dst->buffer[dst->length] = '\0';
}

irom int attr_pure string_sep(const string_t *src, int offset, int occurrence, char c)
{
	for(; (offset < src->size) && (offset < src->length) && (occurrence > 0); offset++)
		if(string_at(src, offset) == c)
			occurrence--;

	if((offset >= src->size) || (offset >= src->length))
		offset = -1;

	return(offset);
}

irom int attr_pure string_find(const string_t *src, int offset, char c)
{
	for(; offset < src->length; offset++)
		if(string_at(src, offset) == c)
			return(offset);

	return(-1);
}

irom void string_replace(string_t *dst, int offset, char c)
{
	if((offset + 1) < dst->size)
	{
		dst->buffer[offset] = c;

		if(offset > dst->length)
		{
			dst->length = offset;
			dst->buffer[dst->length + 1] = '\0';
		}
	}
}

irom void string_splice(string_t *dst, int dst_offset, const string_t *src, int src_offset, int length)
{
	if(dst_offset < 0)
		dst_offset = dst->length;

	if(src_offset < 0)
		src_offset = 0;

	if(length < 0)
		length = src->length - src_offset;

	if((src_offset + length) > src->length)
		length = src->length - src_offset;

	if((dst_offset + length) > dst->size)
		length = dst->size - dst_offset;

	memcpy(dst->buffer + dst_offset, src->buffer + src_offset, length);

	string_setlength(dst, dst_offset + length);
}

irom _Bool string_trim_nl(string_t *dst)
{
	_Bool trimmed = false;

	if((dst->length > 0) && (dst->buffer[dst->length - 1] == '\n'))
	{
		trimmed = true;

		dst->length--;

		if((dst->length > 0) && (dst->buffer[dst->length - 1] == '\r'))
			dst->length--;
	}
	else
	{
		if((dst->length > 0) && (dst->buffer[dst->length - 1] == '\r'))
		{
			trimmed = true;

			dst->length--;

			if((dst->length > 0) && (dst->buffer[dst->length - 1] == '\n'))
				dst->length--;
		}
	}

	return(trimmed);
}

irom void string_bin_to_hex(string_t *dst, const char *src, int length)
{
	int offset;
	uint8_t out;

	for(offset = 0; offset < length ; offset++)
	{
		out = (src[offset] & 0xf0) >> 4;

		if(out > 9)
			out = (out - 10) + 'a';
		else
			out = out + '0';

		string_append_char(dst, out);

		out = (src[offset] & 0x0f) >> 0;

		if(out > 9)
			out = (out - 10) + 'a';
		else
			out = out + '0';

		string_append_char(dst, out);
	}
}

irom void string_ip(string_t *dst, ip_addr_t addr)
{
	ip_addr_to_bytes_t ip_addr_to_bytes;
	ip_addr_to_bytes.ip_addr = addr;

	string_format(dst, "%u.%u.%u.%u",
		ip_addr_to_bytes.byte[0],
		ip_addr_to_bytes.byte[1],
		ip_addr_to_bytes.byte[2],
		ip_addr_to_bytes.byte[3]);
}

irom void string_mac(string_t *dst, uint8 addr[6])
{
	int ix;
	mac_addr_to_bytes_t mac_addr_to_bytes;

	for(ix = 0; ix < 6; ix++)
		mac_addr_to_bytes.mac_addr[ix] = addr[ix];

	string_format(dst, "%02x:%02x:%02x:%02x:%02x:%02x",
		mac_addr_to_bytes.byte[0],
		mac_addr_to_bytes.byte[1],
		mac_addr_to_bytes.byte[2],
		mac_addr_to_bytes.byte[3],
		mac_addr_to_bytes.byte[4],
		mac_addr_to_bytes.byte[5]);
}

#if 0
irom int string_bin(string_t *dst, unsigned int value, int precision, _Bool add_prefix)
{
	int length = 0;
	int bitpos;
	_Bool bit;
	_Bool skip_leading;

	if(precision < 1)
	{
		precision = 32;
		skip_leading = true;
	}
	else
		skip_leading = false;

	if(add_prefix)
		string_append(dst, "0b");

	for(bitpos = precision - 1; bitpos >= 0; bitpos--)
	{
		bit = !!(value & (1 << bitpos));

		if(bit)
			skip_leading = false;

		if(skip_leading && (bitpos > 0))
			continue;

		string_append_char(dst, bit ? '1' : '0');
		length++;
	}

	return(length);
}
#endif

irom int string_double(string_t *dst, double value, int precision, double top_decimal)
{
	double compare;
	int decimal;
	_Bool skip_leading_zeroes;
	int original_length;

	original_length = dst->length;

	if(value < 0)
	{
		string_append_char(dst, '-');
		value = 0 - value;
	}

	skip_leading_zeroes = true;

	if(value > (10 * top_decimal))
	{
		string_append_char(dst, '+');
		string_append_char(dst, '+');
		string_append_char(dst, '+');

		return(dst->length - original_length);
	}

	for(compare = top_decimal; compare > 0; compare /= 10)
	{
		if(value >= compare)
		{
			skip_leading_zeroes = false;

			decimal = (unsigned int)(value / compare);
			value -= decimal * compare;

			string_append_char(dst, (char)(decimal + '0'));
		}
		else
			if(!skip_leading_zeroes)
				string_append_char(dst, '0');

		if((compare <= 1) && (precision == 0))
			break;

		if((unsigned int)compare == 1)
		{
			if(skip_leading_zeroes)
			{
				string_append_char(dst, '0');
				skip_leading_zeroes = false;
			}

			string_append_char(dst, '.');
		}

		if((compare <= 1) && (precision > 0))
			--precision;
	}

	if(dst->length == original_length)
		string_append_char(dst, '0');

	return(dst->length - original_length);
}

/**********************************************************************
 * Copyright (c) 2000 by Michael Barr.  This software is placed into
 * the public domain and may be used for any purpose.  However, this
 * notice must not be changed or removed and no warranty is either
 * expressed or implied by its publication or distribution.
 **********************************************************************/

static uint32_t string_crc_table[256];

irom void string_crc32_init(void)
{
	unsigned int dividend, bit;
	uint32_t remainder;

	for(dividend = 0; dividend < (sizeof(string_crc_table) / sizeof(*string_crc_table)); dividend++)
	{
		remainder = dividend << (32 - 8);

		for (bit = 8; bit > 0; --bit)
		{
			if (remainder & (1 << 31))
				remainder = (remainder << 1) ^ 0x04c11db7;
			else
				remainder = (remainder << 1);
		}

		string_crc_table[dividend] = remainder;
	}
}

irom attr_pure uint32_t string_crc32(const string_t *src, int offset, int length)
{
	uint32_t remainder = 0xffffffff;
	uint8_t data;
	int src_length;

	src_length = src->length;

	for(; (length > 0) && (offset < src_length); offset++, length--)
	{
		data = string_at(src, offset) ^ (remainder >> (32 - 8));
		remainder = string_crc_table[data] ^ (remainder << 8);
	}

	return(remainder ^ 0xffffffff);
}
