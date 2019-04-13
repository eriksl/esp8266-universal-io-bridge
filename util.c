#include "util.h"
#include "uart.h"
#include "ota.h"
#include "config.h"

#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

char flash_dram_buffer[1024];

string_t logbuffer =
{
	.size = 0x3fffeb2c - 0x3fffe000 - 16,
	.length = 0,
	.buffer = (char *)0x3fffe000,
};

int attr_used __errno;

// functions used in SDK libmain and coming from libc (which we don't use)

#ifdef isxdigit
undef isxdigit
#endif

attr_const int isxdigit(int c);
attr_const int isxdigit(int c)
{
	if((c >= '0' && (c <= '9')))
		return(1);

	if((c >= 'a' && (c <= 'f')))
		return(1);

	if((c >= 'A' && (c <= 'F')))
		return(1);

	return(0);
}

#ifdef isdigit
undef isdigit
#endif

attr_const int isdigit(int c);
attr_const int isdigit(int c)
{
	if((c >= '0' && (c <= '9')))
		return(1);

	return(0);
}

/* from http://www.leidinger.net/freebsd/dox/libkern/html/d9/dd9/memchr_8c_source.html */
void *memchr(const void *s, int c, size_t n)
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

void espconn_init(void);
void espconn_init(void)
{
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

iram size_t flash_to_dram(bool cstr, const void *src_flash_unaligned, char *dst_dram, size_t length)
{
	const uint32_t *src_flash;
	unsigned int src_flash_index;
	unsigned int src_flash_sub_index;
	unsigned int dst_dram_index;

	src_flash = (const uint32_t *)((uint32_t)src_flash_unaligned & ~0b11);
	src_flash_sub_index = (uint32_t)src_flash_unaligned & 0b11;

	for(src_flash_index = 0, dst_dram_index = 0; dst_dram_index < length; dst_dram_index++)
	{
		dst_dram[dst_dram_index] = (src_flash[src_flash_index] >> (src_flash_sub_index << 3)) & 0xff;

		if(cstr)
		{
			if(!dst_dram[dst_dram_index])
				break;

			if((dst_dram_index + 1) >= length)
			{
				dst_dram[dst_dram_index] = 0;
				break;
			}
		}

		if((++src_flash_sub_index & 0b11) == 0)
		{
			src_flash_sub_index = 0;
			src_flash_index++;
		}
	}

	return(dst_dram_index);
}

void reset(void)
{
	system_restart();
}

attr_const const char *yesno(bool value)
{
	if(!value)
		return("no");

	return("yes");
}

attr_const const char *onoff(bool value)
{
	if(!value)
		return("off");

	return("on");
}

int log_from_flash(const char *fmt_in_flash, ...)
{
	va_list ap;
	int current, written;
	char fmt_in_dram[128];

	flash_to_dram(true, fmt_in_flash, fmt_in_dram, sizeof(fmt_in_dram));

	va_start(ap, fmt_in_flash);
	written = vsnprintf(flash_dram_buffer, sizeof(flash_dram_buffer), fmt_in_dram, ap);
	va_end(ap);

	if(config_flags_match(flag_log_to_uart))
	{
		for(current = 0; current < written; current++)
			uart_send(0, flash_dram_buffer[current]);

		uart_flush(0);
	}

	if(config_flags_match(flag_log_to_buffer))
	{
		if((string_length(&logbuffer) + written) >= string_size(&logbuffer))
			string_clear(&logbuffer);

		string_append_cstr(&logbuffer, flash_dram_buffer);
	}

	return(written);
}

iram void logchar(char c)
{
	if(config_flags_match(flag_log_to_uart))
	{
		uart_send(0, c);
		uart_flush(0);
	}

	if(config_flags_match(flag_log_to_buffer))
	{
		if((logbuffer.length + 1) >= logbuffer.size)
			string_clear(&logbuffer);

		string_append_char(&logbuffer, c);
	}
}

void msleep(int msec)
{
	while(msec-- > 0)
	{
		system_soft_wdt_feed();
		os_delay_us(1000);
	}
}

attr_pure ip_addr_t ip_addr(const char *src)
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

parse_error_t parse_string(int index, const string_t *src, string_t *dst, char delimiter)
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

parse_error_t parse_int_all(int index, const string_t *src, unsigned int *dst, int base, char delimiter, bool do_signed)
{
	bool valid, negative;
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

parse_error_t parse_float(int index, const string_t *src, double *dst, char delimiter)
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

void string_format_cstr(string_t *dst, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	dst->length += vsnprintf(dst->buffer + dst->length, dst->size - dst->length - 1, fmt, ap);
	va_end(ap);

	if(dst->length > (dst->size - 1))
		dst->length = dst->size - 1;

	dst->buffer[dst->length] = '\0';
}

iram void string_format_flash_ptr(string_t *dst, const char *fmt_flash, ...)
{
	va_list ap;
	int rendered_length, buffer_remaining;

	flash_to_dram(true, fmt_flash, flash_dram_buffer, sizeof(flash_dram_buffer));

	// no space left at all, do nothing

	if((buffer_remaining = dst->size - dst->length - 1) <= 0)
		return;

	va_start(ap, fmt_flash);
	rendered_length = vsnprintf(dst->buffer + dst->length, buffer_remaining, flash_dram_buffer, ap);
	va_end(ap);

	// some snprintf implementations can return -1 when output doesn't fit the output buffer
	// some snprintf implementations can return the original buffer size when the output doesn't fit the output buffer
	// this means we can't always rely on the return value, assume vsnprintf filled all of the buffer_remaining space in those cases

	if((rendered_length < 0) || (rendered_length > buffer_remaining))
		rendered_length = buffer_remaining;

	dst->length += rendered_length;

	dst->buffer[dst->length] = '\0';
}

bool attr_nonnull string_match_cstr_flash(const string_t *s1, const char *s2)
{
	char s2_in_dram[64];

	flash_to_dram(true, s2, s2_in_dram, sizeof(s2_in_dram));

	return(string_match_cstr(s1, s2_in_dram));
}

int attr_pure string_sep(const string_t *src, int offset, int occurrence, char c)
{
	for(; (offset < src->size) && (offset < src->length) && (occurrence > 0); offset++)
		if(string_at(src, offset) == c)
			occurrence--;

	if((offset >= src->size) || (offset >= src->length))
		offset = -1;

	return(offset);
}

int attr_pure string_find(const string_t *src, int offset, char c)
{
	for(; offset < src->length; offset++)
		if(string_at(src, offset) == c)
			return(offset);

	return(-1);
}

void string_replace(string_t *dst, int offset, char c)
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

void string_splice(string_t *dst, int dst_offset, const string_t *src, int src_offset, int length)
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

bool string_trim_nl(string_t *dst)
{
	bool trimmed = false;

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

void string_bin_to_hex(string_t *dst, const unsigned char *src, int length)
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

void string_ip(string_t *dst, ip_addr_t addr)
{
	ip_addr_to_bytes_t ip_addr_to_bytes;
	ip_addr_to_bytes.ip_addr = addr;

	string_format(dst, "%u.%u.%u.%u",
		ip_addr_to_bytes.byte[0],
		ip_addr_to_bytes.byte[1],
		ip_addr_to_bytes.byte[2],
		ip_addr_to_bytes.byte[3]);
}

void string_mac(string_t *dst, mac_addr_t addr)
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
int string_bin(string_t *dst, unsigned int value, int precision, bool add_prefix)
{
	int length = 0;
	int bitpos;
	bool bit;
	bool skip_leading;

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

int string_double(string_t *dst, double value, int precision, double top_decimal)
{
	double compare;
	int decimal;
	bool skip_leading_zeroes;
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

// missing from libc

void *_malloc_r(struct _reent *r, size_t sz)
{
	extern void* pvPortMalloc(size_t sz, const char *, unsigned, bool);
	return(pvPortMalloc(sz, "", 0, false));
}

void *_calloc_r(struct _reent *r, size_t a, size_t b)
{
	extern void* pvPortCalloc(size_t count,size_t size,const char *,unsigned);
	return(pvPortCalloc(a, b, "", 0));
}

void _free_r(struct _reent *r, void *x)
{
	extern void vPortFree (void *p, const char *, unsigned);
	return(vPortFree(x, "", 0));
}

void *_realloc_r(struct _reent *r, void *x, size_t sz)
{
	extern void* pvPortRealloc (void *p, size_t n, const char *, unsigned);
	return(pvPortRealloc(x, sz, "", 0));
}
