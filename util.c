#include "util.h"

#include "user_main.h"
#include "queue.h"
#include "uart.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>

#include <mem.h>
#include <user_interface.h>

static char dram_buffer[1024];
string_new(, buffer_4k, 4096 + 4);

int ets_vsnprintf(char *, size_t, const char *, va_list);

// string handling

irom static size_t copy_flash_to_ram(char *dst, const char *from_ptr_byte, int size)
{
	int from, to;
	uint32_t current32, byte;
	uint8_t current8;
	const uint32_t *from_ptr;

	from_ptr = (const uint32_t *)(const void *)from_ptr_byte;

	for(from = 0, to = 0; (int)(from * sizeof(*from_ptr)) < (size - 1); from++)
	{
		current32 = from_ptr[from];

		for(byte = 4; byte > 0; byte--)
		{
			if((current8 = (current32 & 0x000000ff)) == '\0')
				goto done;

			if((to + 1) >= size)
				goto done;

			dst[to++] = (char)current8;
			current32 = (current32 >> 8) & 0x00ffffff;
		}
	}

done:
	dst[to] = '\0';

	return(to);
}

irom void string_set(string_t *dst, char *buffer, int size, int length)
{
	dst->buffer = buffer;
	dst->size   = size + 1;
	dst->length = length;
}

irom string_t string_from_ptr(size_t size, char *buffer)
{
	string_t string = { size, ets_strlen(buffer), buffer };

	return(string);
}

irom char *string_to_ptr(string_t *string)
{
	string->buffer[string->length] = '\0';

	return(string->buffer);
}

irom attr_pure const char *string_to_const_ptr(const string_t *string)
{
	return(string->buffer);
}

irom void string_format_ptr(string_t *dst, const char *fmt_flash, ...)
{
	va_list ap;

	copy_flash_to_ram(dram_buffer, fmt_flash, sizeof(dram_buffer));

	va_start(ap, fmt_flash);
	dst->length += ets_vsnprintf(dst->buffer + dst->length, dst->size - dst->length - 1, dram_buffer, ap);
	va_end(ap);

	if(dst->length > (dst->size - 1))
		dst->length = dst->size - 1;

	dst->buffer[dst->length] = '\0';
}

irom void string_cat_strptr(string_t *dst, const char *src)
{
	if(dst->length < dst->size)
		dst->length += strlcpy(dst->buffer + dst->length, src, dst->size - dst->length);
	else
		dst->buffer[dst->size - 1] = '\0';
}

irom void string_cat_ptr(string_t *dst, const char *src)
{
	if(dst->length < dst->size)
		dst->length += copy_flash_to_ram(dst->buffer + dst->length, src, dst->size - dst->length);
	else
		dst->buffer[dst->size - 1] = '\0';
}

irom int string_copy_string(string_t *dst, string_t *src)
{
	int length;

	if((string_length(src) + 1) >= string_size(dst))
		length = string_size(dst) - 1;
	else
		length = string_length(src);

	ets_memcpy(dst->buffer, src->buffer, length);

	dst->length = length;
	dst->buffer[dst->length] = '\0';

	return(length);
}

/* from OpenBSD http://code.google.com/p/honeyd/source/browse/trunk/honeyd/strlcpy.c */
irom size_t strlcpy(char *dst, const char *src, size_t siz)
{
	char *d = dst;
	const char *s = src;
	size_t n = siz;

	if (n == 0)
		return(ets_strlen(s));

	while (*s != '\0')
	{
		if (n != 1)
		{
			*d++ = *s;
			n--;
		}
		s++;
	}

	*d = '\0';

	return(s - src);
}

typedef union
{
	ip_addr_t	ip_addr;
	uint8_t		byte[3];
} ip_addr_to_bytes_t;

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

irom int string_double(string_t *dst, double value, int precision, double top_decimal)
{
	double compare;
	int decimal;
	bool_t skip_leading_zeroes;
	int original_length;

	original_length = string_length(dst);

	if(value < 0)
	{
		string_append(dst, '-');
		value = 0 - value;
	}

	skip_leading_zeroes = true;

	if(value > (10 * top_decimal))
	{
		string_append(dst, '+');
		string_append(dst, '+');
		string_append(dst, '+');

		return(string_length(dst) - original_length);
	}

	for(compare = top_decimal; compare > 0; compare /= 10)
	{
		if(value >= compare)
		{
			skip_leading_zeroes = false;

			decimal = (unsigned int)(value / compare);
			value -= decimal * compare;

			string_append(dst, (char)(decimal + '0'));
		}
		else
			if(!skip_leading_zeroes)
				string_append(dst, '0');

		if((compare <= 1) && (precision == 0))
			break;

		if((unsigned int)compare == 1)
		{
			if(skip_leading_zeroes)
			{
				string_append(dst, '0');
				skip_leading_zeroes = false;
			}

			string_append(dst, '.');
		}

		if((compare <= 1) && (precision > 0))
			--precision;
	}

	if(string_length(dst) == original_length)
		string_append(dst, '0');

	return(string_length(dst) - original_length);
}

irom void string_setlength(string_t *dst, int length)
{
	if((length + 1) > dst->size)
		dst->length = dst->size - 1;
	else
		dst->length = length;

	dst->buffer[dst->length] = '\0';
}

irom void string_append(string_t *dst, char c)
{
	if(string_space(dst))
	{
		dst->buffer[dst->length++] = c;
		dst->buffer[dst->length] = '\0';
	}
	else
		dst->buffer[dst->size - 1] = '\0';
}

irom bool string_match(const string_t *s1, const char *s2)
{
	return(!ets_strcmp(s1->buffer, s2));
}

irom bool string_match_string(const string_t *s1, const string_t *s2)
{
	return(string_match(s1, s2->buffer));
}

irom bool string_nmatch(const string_t *s1, const char *s2, int n)
{
	return(!ets_strncmp(s1->buffer, s2, n));
}

irom attr_pure char string_index(const string_t *s, int index)
{
	if(index < s->length)
		return(s->buffer[index]);
	else
		return('\0');
}

irom attr_pure int string_sep(const string_t *src, int offset, int occurence, char c)
{
	for(; (offset < string_length(src)) && (occurence > 0); offset++)
		if(string_index(src, offset) == c)
			occurence--;

	if((offset >= string_size(src)) || (offset >= string_length(src)))
		return(-1);

	return(offset);
}

irom attr_pure int string_find(const string_t *src, int offset, char c)
{
	for(; offset < string_length(src); offset++)
		if(string_index(src, offset) == c)
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

irom void string_splice(string_t *dst, const string_t *src, int src_offset, int length)
{
	const char *from;
	char *to;

	if((src_offset + length) > string_length(src))
		length = string_length(src) - src_offset;

	if((string_length(dst) + length) > string_size(dst))
		length = string_size(dst) - string_length(dst);

	if(length <= 0)
		return;

	from = string_to_const_ptr(src);
	to = string_to_ptr(dst);

	ets_memcpy(to + string_length(dst), from + src_offset, length); 

	string_setlength(dst, string_length(dst) + length);
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

		string_append(dst, out);

		out = (src[offset] & 0x0f) >> 0;

		if(out > 9)
			out = (out - 10) + 'a';
		else
			out = out + '0';

		string_append(dst, out);
	}
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

	src_length = string_length(src);

	for(; (length > 0) && (offset < src_length); offset++, length--)
	{
		data = string_index(src, offset) ^ (remainder >> (32 - 8));
		remainder = string_crc_table[data] ^ (remainder << 8);
	}

	return(remainder ^ 0xffffffff);
}

irom parse_error_t parse_string(int index, const string_t *src, string_t *dst)
{
	uint8_t current;
	int offset;

	if((offset = string_sep(src, 0, index, ' ')) < 0)
		return(parse_out_of_range);

	for(; offset < string_length(src); offset++)
	{
		current = string_index(src, offset);

		if(current == ' ')
			break;

		if((current > ' ') && (current <= '~'))
			string_append(dst, current);
	}

	return(parse_ok);
}

irom parse_error_t parse_int(int index, const string_t *src, int *dst, int base)
{
	bool negative, valid;
	int value;
	int offset;
	char current;

	negative = false;
	value = 0;
	valid = false;

	if((offset = string_sep(src, 0, index, ' ')) < 0)
		return(parse_out_of_range);

	if(base == 0)
	{
		if(((offset + 1) < string_length(src)) &&
				(string_index(src, offset) == '0') &&
				(string_index(src, offset + 1) == 'x'))
		{
			base = 16;
			offset += 2;
		}
		else
			base = 10;
	}

	if((offset < string_length(src)) && (base == 10))
	{
		if(string_index(src, offset) == '-')
		{
			negative = true;
			offset++;
		}

		if(string_index(src, offset) == '+')
			offset++;
	}

	for(; offset < string_length(src); offset++)
	{
		current = string_index(src, offset);

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
				if((current != '\0') && (current != ' ') && (current != '\n') && (current != '\r'))
					valid = false;

				break;
			}
		}

		valid = true;
	}

	if(!valid)
		return(parse_invalid);

	if(negative)
		*dst = 0 - value;
	else
		*dst = value;

	return(parse_ok);
}

irom parse_error_t parse_float(int index, const string_t *src, double *dst)
{
	int offset;
	int decimal;
	bool negative;
	bool valid;
	double result;
	char current;

	valid = false;
	negative = false;
	offset = 0;
	result = 0;
	decimal = 0;

	if((offset = string_sep(src, 0, index, ' ')) < 0)
		return(parse_out_of_range);

	if((offset < string_length(src)) && (string_index(src, offset) == '-'))
	{
		negative = true;
		offset++;
	}

	for(; offset < string_length(src); offset++)
	{
		current = string_index(src, offset);

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

// other convenience functions

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

irom int dprintf(const char *fmt, ...)
{
	va_list ap;
	int current, n;

	va_start(ap, fmt);
	n = ets_vsnprintf(dram_buffer, sizeof(dram_buffer), fmt, ap);
	va_end(ap);

	for(current = 0; current < n; current++)
		if(!queue_full(&data_send_queue))
			queue_push(&data_send_queue, dram_buffer[current]);

	queue_push(&data_send_queue, '\r');
	queue_push(&data_send_queue, '\n');

	uart_start_transmit(!queue_empty(&data_send_queue));

	return(n);
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
