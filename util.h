#ifndef util_h
#define util_h

#pragma GCC diagnostic ignored "-Wpacked"
#pragma GCC diagnostic push
#include "lwip/ip_addr.h"
#pragma GCC diagnostic pop

#include "attribute.h"

#include <stdint.h>
#include <sys/types.h>
#include <string.h>
#include <stdbool.h>

typedef struct
{
	int size;
	int length;
	char *buffer;
} string_t;

enum
{
	off = 0,
	no = 0,
	on = 1,
	yes = 1
};

typedef enum
{
	parse_ok = 0,
	parse_out_of_range,
	parse_invalid,
} parse_error_t;

typedef uint8_t mac_addr_t[6];

typedef union
{
	mac_addr_t	mac_addr;
	uint8_t		byte[6];
} mac_addr_to_bytes_t;

typedef union
{
	ip_addr_t	ip_addr;
	uint8_t		byte[4];
} ip_addr_to_bytes_t;

_Static_assert(sizeof(_Bool) == 1, "sizeof(_Bool) != 1");

double pow(double, double);
double fmax(double, double);

extern char flash_dram_buffer[1024];

// convenience functions

attr_nonnull int strecpy(char *dst, const char *src, int size);
attr_nonnull size_t flash_to_dram(_Bool cstr, const void *src_flash, char *dst_dram, size_t length);
void reset(void);
const char *yesno(_Bool value);
const char *onoff(_Bool value);

void msleep(int);

attr_inline unsigned int umax(unsigned int a, unsigned int b)
{
	if(a > b)
		return(a);

	return(b);
}

attr_inline uint32_t ccount(void)
{
	uint32_t sr_ccount;

	asm volatile ("esync; rsr %0, ccount" : "=r"(sr_ccount));

	return(sr_ccount);
}

attr_inline void csleep(volatile uint32_t target)
{
	uint32_t start;

	if(target < 14)
		while(target-- > 0);
	else
		for(start = ccount(); (ccount() - start) < target; );
}

attr_nonnull ip_addr_t ip_addr(const char *);

// string functions

attr_nonnull void string_format_cstr(string_t *dst, const char *fmt, ...) __attribute__ ((format (printf, 2, 3)));
attr_nonnull void string_format_flash_ptr(string_t *dst, const char *, ...) __attribute__ ((format (printf, 2, 3)));
attr_nonnull int string_sep(const string_t *, int offset, int occurrence, char c);
attr_nonnull int string_find(const string_t *, int offset, char c);
attr_nonnull void string_replace(string_t *, int index, char c);
attr_nonnull void string_splice(string_t *dst, int dst_offset, const string_t *src, int src_offset, int length);
attr_nonnull _Bool string_trim_nl(string_t *dst);
attr_nonnull void string_trim_string(string_t *haystack_string, const string_t *needle_string);
attr_nonnull void string_bin_to_hex(string_t *dst, const char *src, int length);
attr_nonnull void string_ip(string_t *dst, ip_addr_t);
attr_nonnull void string_mac(string_t *dst, mac_addr_t);
//int string_bin(string_t *dst, unsigned int value, int precision, _Bool add_prefix);
attr_nonnull int string_double(string_t *dst, double value, int precision, double top_decimal);

#define string_new(_attributes, _name, _size) \
	_attributes char _ ## _name ## _buf[_size] = { 0 }; \
	_attributes string_t _name = { .size = _size, .length = 0, .buffer = _ ## _name ## _buf }

#define string_append(dst, src) \
do { \
	static roflash const char src_flash[] = src; \
	string_append_cstr_flash(dst, src_flash); \
} while(0)

#define string_format(dst, fmt, ...) \
do { \
	static roflash const char fmt_flash[] = fmt; \
	string_format_flash_ptr(dst, fmt_flash, ## __VA_ARGS__); \
} while(0)

attr_inline attr_nonnull int string_length(const string_t *dst)
{
	return(dst->length);
}

attr_inline attr_nonnull int string_size(const string_t *dst)
{
	return(dst->size);
}

attr_inline attr_nonnull int string_empty(const string_t *dst)
{
	return(dst->length == 0);
}

attr_inline attr_nonnull const char *string_buffer(const string_t *string)
{
	return(string->buffer);
}

attr_inline attr_nonnull char *string_buffer_nonconst(string_t *string)
{
	return(string->buffer);
}

attr_inline attr_nonnull _Bool string_space(const string_t *dst)
{
	return(dst->length < dst->size);
}

attr_inline attr_nonnull void string_clear(string_t *dst)
{
	dst->length = 0;
};

attr_inline attr_nonnull void string_set(string_t *dst, char *buffer, int size, int length)
{
	dst->buffer = buffer;
	dst->size   = size;
	dst->length = length;
}

attr_inline attr_nonnull void string_setlength(string_t *dst, int length)
{
	if(length > dst->size)
		length = dst->size;

	dst->length = length;
}

attr_inline attr_nonnull _Bool string_match_string(const string_t *s1, const string_t *s2)
{
	if((s1->length == s2->length) && (memcmp(s1->buffer, s2->buffer, s1->length) == 0))
		return(true);

	return(false);
}

attr_inline _Bool attr_nonnull string_match_cstr(const string_t *s1, const char *s2)
{
	int length = strlen(s2);

	if((s1->length == length) && (memcmp(s1->buffer, s2, length) == 0))
		return(true);

	return(false);
}

attr_inline attr_nonnull _Bool string_nmatch_string(const string_t *s1, const string_t *s2, int n)
{
	if((s1->length >= n) && (s2->length >= n) && (memcmp(s1->buffer, s2->buffer, n) == 0))
		return(true);

	return(false);
}

attr_inline attr_nonnull _Bool string_nmatch_cstr(const string_t *s1, const char *s2, int n)
{
	int length = strlen(s2);

	if((s1->length >= n) && (length >= n) && (memcmp(s1->buffer, s2, n) == 0))
		return(true);

	return(false);
}

attr_inline attr_nonnull string_t string_from_cstr(size_t size, char *cstr)
{
	string_t string = { size, strlen(cstr), cstr };

	return(string);
}

attr_inline const char *string_to_cstr(string_t *string)
{
	if((string == (string_t *)0) || (string->size < 1) || (string->length < 1) || (string->buffer == (const char *)0))
		return("");

	if(string->length >= string->size)
		string->length = string->size - 1;

	string->buffer[string->length] = '\0';

	return(string->buffer);
}

attr_inline attr_nonnull char string_at(const string_t *s, int at)
{
	if((unsigned int)at < (unsigned int)s->length)
		return(s->buffer[at]);
	else
		return('\0');
}

attr_inline attr_nonnull void string_append_char(string_t *dst, char c)
{
	if((dst->length + 2) < dst->size)
		dst->buffer[dst->length++] = c;

	dst->buffer[dst->length] = '\0';
}

attr_inline attr_nonnull void string_append_byte(string_t *dst, uint8_t c)
{
	if(dst->length < dst->size)
		dst->buffer[dst->length++] = c;
}

attr_inline attr_nonnull void string_append_bytes(string_t *dst, const uint8_t *src, int length)
{
	if((dst->length + length) > dst->size)
		length = dst->size - dst->length;

	memcpy(dst->buffer + dst->length, src, length);

	dst->length += length;
}

attr_inline attr_nonnull void string_append_cstr(string_t *dst, const char *src)
{
	dst->length += strecpy(dst->buffer + dst->length, src, dst->size - dst->length);
}

attr_inline attr_nonnull void string_append_cstr_flash(string_t *dst, const char *src_flash)
{
	dst->length += flash_to_dram(true, src_flash, dst->buffer + dst->length, dst->size - dst->length);
}

attr_inline attr_nonnull void string_append_string(string_t *dst, const string_t *src)
{
	string_append_bytes(dst, src->buffer, src->length);
}

attr_inline attr_nonnull void string_copy_string(string_t *dst, const string_t *src)
{
	dst->length = 0;
	string_append_string(dst, src);
}

attr_nonnull parse_error_t parse_string(int index, const string_t *in, string_t *out, char delim);
attr_nonnull parse_error_t parse_int_all(int index, const string_t *src, unsigned int *dst, int base, char delim, _Bool do_signed);
attr_nonnull parse_error_t parse_float(int index, const string_t *, double *, char delim);

attr_inline attr_nonnull parse_error_t parse_uint(int index, const string_t *src, unsigned int *dst, int base, char delimiter)
{
	return(parse_int_all(index, src, dst, base, delimiter, false));
}

attr_inline attr_nonnull parse_error_t parse_int(int index, const string_t *src, int *dst, int base, char delimiter)
{
	parse_error_t error;
	unsigned int value;

	error = parse_int_all(index, src, &value, base, delimiter, true);
	*dst = (int)value;
	return(error);
}

// logging

extern string_t logbuffer;

int attr_nonnull log_from_flash(const char *fmt_in_flash, ...) __attribute__ ((format (printf, 1, 2)));

#define log(fmt, ...) \
do { \
	static roflash const char log_flash[] = fmt; \
	log_from_flash(log_flash, ## __VA_ARGS__); \
} while(0)

void logchar(char c);

#endif
