#include "sys_string.h"
#include "attribute.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>

char flash_dram_buffer[1024];

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

attr_nonnull parse_error_t parse_uint(int index, const string_t *src, unsigned int *dst, int base, char delimiter)
{
	int offset;
	unsigned int rv;
	const char *nptr;
	char *endptr;

	if((offset = string_sep(src, 0, index, delimiter)) < 0)
		return(parse_out_of_range);

	nptr = string_buffer(src) + offset;
	rv = strtoul(nptr, &endptr, base);

	if(nptr == endptr)
		return(parse_invalid);

	*dst = rv;

	return(parse_ok);
}

attr_nonnull parse_error_t parse_int(int index, const string_t *src, int *dst, int base, char delimiter)
{
	int offset;
	int rv;
	const char *nptr;
	char *endptr;

	if((offset = string_sep(src, 0, index, delimiter)) < 0)
		return(parse_out_of_range);

	nptr = string_buffer(src) + offset;
	rv = strtol(nptr, &endptr, base);

	if(nptr == endptr)
		return(parse_invalid);

	*dst = rv;

	return(parse_ok);
}

parse_error_t parse_float(int index, const string_t *src, double *dst, char delimiter)
{
	int offset;
	double rv;
	const char *nptr;
	char *endptr;

	if((offset = string_sep(src, 0, index, delimiter)) < 0)
		return(parse_out_of_range);

	nptr = string_buffer(src) + offset;
	rv = strtod(nptr, &endptr);

	if(nptr == endptr)
		return(parse_invalid);

	*dst = rv;

	return(parse_ok);
}

