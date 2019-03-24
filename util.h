#ifndef util_h
#define util_h

// ugly workaround for SDK header "c_types.h" that gets included from osapi.h,
// that defines uint32_t and int32_t to non standard types.
#define uint32_t _uint32_t_
#define int32_t _int32_t_
#include <osapi.h>
#undef uint32_t
#undef int32_t

#include <stdint.h>
#include <sys/types.h>
#include <stdarg.h>

#include "attribute.h"

typedef struct
{
	int size;
	int length;
	char *buffer;
} string_t;

#include "lwip-interface.h"

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
	uint8 		mac_addr[6];
	uint8_t		byte[6];
} mac_addr_to_bytes_t;

typedef union
{
	ip_addr_t	ip_addr;
	uint8_t		byte[4];
} ip_addr_to_bytes_t;

_Static_assert(sizeof(_Bool) == 1, "sizeof(_Bool) != 1");

// make sure we don't use the broken memory management

#define vPortFree _x_vPortFree
#define pvPortMalloc _x_pvPortMalloc
#define pvPortCalloc _x_pvPortCalloc
#define pvPortRealloc _x_pvPortRealloc
#define pvPortZalloc _x_pvPortZalloc
#include <mem.h>
#undef vPortFree
#undef pvPortMalloc
#undef pvPortCalloc
#undef pvPortRealloc
#undef pvPortZalloc

#define strcpy #pragma error strcpy unsafe
#define strncpy #pragma error strncpy unsafe
#define strcat #pragma error strcat unsafe
#define strncat #pragma error strncat unsafe

// prototypes missing and undocumented ROM functions

typedef struct {
  uint32_t i[2];
  uint32_t buf[4];
  unsigned char in[64];
  unsigned char digest[16];
} MD5_CTX;

typedef struct {
    unsigned int h0, h1, h2, h3, h4;
    unsigned int Nl, Nh;
    unsigned int data[16];
    unsigned int num;
} SHA_CTX;

struct tm
{
  int	tm_sec;
  int	tm_min;
  int	tm_hour;
  int	tm_mday;
  int	tm_mon;
  int	tm_year;
  int	tm_wday;
  int	tm_yday;
  int	tm_isdst;
};

attr_nonnull int ets_vsnprintf(char *, size_t, const char *, va_list);
attr_nonnull struct tm *sntp_localtime(const time_t *);

attr_nonnull int MD5Init(MD5_CTX *context);
attr_nonnull int MD5Update(MD5_CTX *context, const void *, unsigned int length);
attr_nonnull int MD5Final(unsigned char *hash, MD5_CTX *context);

attr_nonnull int SHA1Init(SHA_CTX *context);
attr_nonnull int SHA1Update(SHA_CTX *context, const void *, unsigned int length);
attr_nonnull int SHA1Final(unsigned char *md, SHA_CTX *context);

enum { SHA_DIGEST_LENGTH = 20 };

// prototypes missing

double pow(double, double);
double fmax(double, double);

// functions missing from SDK libmain (but declared in headers)

/* int isxdigit(int c); */
/* int isdigit(int c); */
/* void *memchr(const void *s, int c, size_t n); */

extern char flash_dram_buffer[1024];

// ugly kludge for incorrectly declared spi_flash_* functions */
#undef spi_flash_write
#define spi_flash_read _spi_flash_read
#define spi_flash_write _spi_flash_write
#include <spi_flash.h>
#undef spi_flash_read
#undef spi_flash_write
attr_nonnull SpiFlashOpResult spi_flash_read(uint32_t src, void *dst, uint32_t size);
attr_nonnull SpiFlashOpResult spi_flash_write(uint32_t dst, const void *src, uint32_t size);

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

attr_inline void usleep(int usec)
{
	os_delay_us(usec);
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
attr_nonnull void string_mac(string_t *dst, uint8 mac_addr[6]);
//int string_bin(string_t *dst, unsigned int value, int precision, _Bool add_prefix);
attr_nonnull int string_double(string_t *dst, double value, int precision, double top_decimal);
void string_crc32_init(void);
attr_nonnull uint32_t string_crc32(const string_t *src, int offset, int length);

#define string_new(_attributes, _name, _size) \
	_attributes char _ ## _name ## _buf[_size] = { 0 }; \
	_attributes string_t _name = { .size = _size, .length = 0, .buffer = _ ## _name ## _buf }

#define string_init(_name, _string) \
	string_new(, _name, sizeof(_string)); \
	string_append(&_name, _string);

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
attr_nonnull parse_error_t parse_int_all(int index, const string_t *src, uint32_t *dst, int base, char delim, _Bool do_signed);
attr_nonnull parse_error_t parse_float(int index, const string_t *, double *, char delim);

attr_inline attr_nonnull irom parse_error_t parse_uint(int index, const string_t *src, uint32_t *dst, int base, char delimiter)
{
	return(parse_int_all(index, src, dst, base, delimiter, false));
}

attr_inline attr_nonnull parse_error_t parse_int(int index, const string_t *src, int32_t *dst, int base, char delimiter)
{
	parse_error_t error;
	uint32_t value;

	error = parse_int_all(index, src, &value, base, delimiter, true);
	*dst = (int32_t)value;
	return(error);
}

// logging

extern string_t logbuffer;

int attr_nonnull log_from_flash(const char *fmt_in_flash, ...) __attribute__ ((format (printf, 1, 2)));

#define log(fmt, ...) \
do { \
	static roflash const char fmt_flash[] = fmt; \
	log_from_flash(fmt_flash, ## __VA_ARGS__); \
} while(0)

void logchar(char c);

#endif
