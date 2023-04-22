#ifndef util_h
#define util_h

#include <lwip/ip_addr.h>

#include "attribute.h"
#include "sdk.h"

#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>
#include <string.h>

typedef enum
{
	parse_ok = 0,
	parse_out_of_range,
	parse_invalid,
} parse_error_t;

typedef struct
{
	int size;
	int length;
	char *buffer;
} string_t;

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

enum
{
	off = 0,
	no = 0,
	on = 1,
	yes = 1
};

_Static_assert(sizeof(bool) == 1, "sizeof(bool) != 1");

typedef enum
{
	fsb_free,
	fsb_config_read,
	fsb_config_write,
	fsb_config_write_dirty,
	fsb_config_cache,
	fsb_sequencer,
	fsb_display_picture,
	fsb_rboot,
} flash_sector_buffer_use_t;

#define flash_buffer_request(use, pvt, descr, str, cstr, size) \
do { \
    static roflash const char descr_flash[] = descr; \
	_flash_buffer_request(use, pvt, descr_flash, str, cstr, size); \
} while(0)

#define flash_buffer_release(use, descr) \
do { \
    static roflash const char descr_flash[] = descr; \
	_flash_buffer_release(use, descr_flash); \
} while(0)

void _flash_buffer_request(flash_sector_buffer_use_t use, bool pvt, const char *description,
		string_t **str, char **cstr, unsigned int *size);
void _flash_buffer_release(flash_sector_buffer_use_t use, const char *description);

flash_sector_buffer_use_t flash_buffer_using(void);
bool flash_buffer_using_1(flash_sector_buffer_use_t one);
bool flash_buffer_using_2(flash_sector_buffer_use_t one, flash_sector_buffer_use_t two);
bool flash_buffer_using_3(flash_sector_buffer_use_t one, flash_sector_buffer_use_t two, flash_sector_buffer_use_t three);

double pow(double, double);
double fmax(double, double);

void reset(void);
const char *yesno(bool value);
const char *onoff(bool value);

void msleep(int);

attr_inline unsigned int umin(unsigned int a, unsigned int b)
{
	if(a < b)
		return(a);

	return(b);
}

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

void power_save_enable(bool enable);

ip_addr_t ip_addr(const char *);

unsigned int attr_nonnull crc16(unsigned int, const uint8_t *);

extern string_t logbuffer;
extern unsigned int logbuffer_display_current;
void logbuffer_clear(void);

const void *flash_cache_pointer(uint32_t offset);

attr_result_used unsigned int utf8_to_unicode(const char *src, unsigned int dst_size, unsigned int *dst);

enum
{
	MD5_DIGEST_LENGTH = 16,
	SHA_DIGEST_LENGTH = 20,
};

unsigned char *MD5(const unsigned char *src, unsigned int length, uint8_t digest[MD5_DIGEST_LENGTH]);
void MD5_text(const unsigned char *src, unsigned int length, string_t *digest_text);
uint32_t MD5_trunc_32(unsigned int length, const unsigned char *src);
unsigned char *SHA1(const unsigned char *src, unsigned int length, uint8_t digest[SHA_DIGEST_LENGTH]);
void SHA1_text(const unsigned char *src, unsigned int length, string_t *digest_text);

void attr_nonnull log_from_flash_0(const char *f);
void attr_nonnull log_from_flash_n(const char *f, ...) __attribute__ ((format (printf, 1, 2)));

// ugly workaround to get #define argument number overloading
#define _GET_MACRO_(_1, _2, _3, _4, _5, _NAME_, ...) _NAME_
#define log(...) _GET_MACRO_(__VA_ARGS__, _log_4_, _log_3_, _log_2_, _log_1_, _log_0_)(__VA_ARGS__)

#define _log_0_(f) \
do { \
	static roflash const char log_fmt_flash[] = f; \
	log_from_flash_0(log_fmt_flash); \
} while(0)

#define _log_1_(f, p1) \
do { \
	static roflash const char log_fmt_flash[] = f; \
	log_from_flash_n(log_fmt_flash, p1); \
} while(0)

#define _log_2_(f, p1, p2) \
do { \
	static roflash const char log_fmt_flash[] = f; \
	log_from_flash_n(log_fmt_flash, p1, p2); \
} while(0)

#define _log_3_(f, p1, p2, p3) \
do { \
	static roflash const char log_fmt_flash[] = f; \
	log_from_flash_n(log_fmt_flash, p1, p2, p3); \
} while(0)

#define _log_4_(f, p1, p2, p3, p4) \
do { \
	static roflash const char log_fmt_flash[] = f; \
	log_from_flash_n(log_fmt_flash, p1, p2, p3, p4); \
} while(0)

void logchar_sdk(char c);

extern char		flash_dram_buffer[1024];
extern string_t flash_dram;

attr_inline int memory_compare(unsigned int length, const void *src1, const void *src2)
{
	if(src1 == src2)
		return(0);

	return(ets_memcmp(src1, src2, length));
}

attr_nonnull int strecpy(char *dst, const char *src, int size);
attr_nonnull unsigned int flash_to_dram(bool cstr, const void *src_flash, char *dst_dram, size_t length);

attr_nonnull void string_format_cstr(string_t *dst, const char *fmt, ...) __attribute__ ((format (printf, 2, 3)));
attr_nonnull void string_format_flash_ptr(string_t *dst, const char *, ...) __attribute__ ((format (printf, 2, 3)));
attr_nonnull int string_sep(const string_t *, int offset, int occurrence, char c);
attr_nonnull int string_find(const string_t *, int offset, char c);
attr_nonnull void string_replace(string_t *, int index, char c);
attr_nonnull void string_splice(string_t *dst, int dst_offset, const string_t *src, int src_offset, int length);
attr_nonnull int string_trim(string_t *dst, int amount);
attr_nonnull bool string_trim_nl(string_t *dst);
attr_nonnull void string_bin_to_hex(string_t *dst, const unsigned char *src, int length);
attr_nonnull void string_ip(string_t *dst, ip_addr_t);
attr_nonnull void mac_to_string(string_t *dst, const mac_addr_t);
attr_nonnull bool string_to_mac(mac_addr_t *addr, const string_t *src);

#define string_new(_attributes, _name, _size) \
	_attributes char _ ## _name ## _buf[_size] = { 0 }; \
	_attributes string_t _name = { .size = _size, .length = 0, .buffer = _ ## _name ## _buf }

#define string_init(_attributes, _name, _string) \
	_attributes char _ ## _name ## _buf[sizeof(_string) + 1] = _string; \
	_attributes string_t _name = { .size = sizeof(_string) + 1, .length = sizeof(_string), .buffer = _ ## _name ## _buf }

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

attr_inline attr_nonnull bool string_full(const string_t *dst)
{
	if(dst->length == dst->size)
		return(true);

	if(((dst->length + 1) == dst->size) && (dst->buffer[dst->length] == '\0'))
		return(true);

	return(false);
}

attr_inline attr_nonnull const char *string_buffer(const string_t *string)
{
	return(string->buffer);
}

attr_inline attr_nonnull char *string_buffer_nonconst(string_t *string)
{
	return(string->buffer);
}

attr_inline attr_nonnull bool string_space(const string_t *dst)
{
	return((dst->length + 1) < dst->size);
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

attr_inline attr_nonnull bool string_match_string(const string_t *s1, const string_t *s2)
{
	if((s1->length == s2->length) && (memory_compare(s1->length, s1->buffer, s2->buffer) == 0))
		return(true);

	return(false);
}

attr_inline bool attr_nonnull string_match_cstr(const string_t *s1, const char *s2)
{
	int length = strlen(s2);

	if((s1->length == length) && (memory_compare(length, s1->buffer, s2) == 0))
		return(true);

	return(false);
}

bool attr_nonnull string_match_cstr_flash(const string_t *s1, const char *s2);

attr_inline attr_nonnull bool string_nmatch_string(const string_t *s1, const string_t *s2, int n)
{
	if((s1->length >= n) && (s2->length >= n) && (memory_compare(n, s1->buffer, s2->buffer) == 0))
		return(true);

	return(false);
}

attr_inline attr_nonnull bool string_nmatch_cstr(const string_t *s1, const char *s2, int n)
{
	int length = strlen(s2);

	if((s1->length >= n) && (length >= n) && (memory_compare(n, s1->buffer, s2) == 0))
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

attr_inline attr_nonnull char string_front(const string_t *s)
{
	if((s->size < s->length) || (s->length == 0))
		return('\0');

	return(s->buffer[0]);
}

attr_inline attr_nonnull char string_back(const string_t *s)
{
	if((s->size < s->length) || (s->length == 0))
		return('\0');

	return(s->buffer[s->length - 1]);
}

attr_inline attr_nonnull bool string_printable(const string_t *s, int at)
{
	char value;

	if(at >= s->length)
		return(false);

	value = s->buffer[at];

	if((value >= ' ') && (value <= '~'))
		return(true);

	return(false);
}

attr_inline attr_nonnull void string_append_char(string_t *dst, char c)
{
	if((dst->length + 2) < dst->size)
	{
		dst->buffer[dst->length++] = c;
		dst->buffer[dst->length] = '\0';
	}
}

attr_inline attr_nonnull void string_append_chars(string_t *dst, char c, unsigned int amount)
{
	while(amount-- > 0)
		string_append_char(dst, c);
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

	memcpy(dst->buffer + dst->length, (const void *)src, length);

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
	string_append_bytes(dst, (const void *)src->buffer, src->length);
}

attr_inline attr_nonnull void string_copy_string(string_t *dst, const string_t *src)
{
	dst->length = 0;
	string_append_string(dst, src);
}

attr_nonnull void string_word_to_bin(string_t *dst, unsigned int word, unsigned int bits);
attr_nonnull parse_error_t parse_string(int index, const string_t *in, string_t *out, char delim);
attr_nonnull parse_error_t parse_uint(int index, const string_t *src, unsigned int *dst, int base, char delimiter);
attr_nonnull parse_error_t parse_int(int index, const string_t *src, int *dst, int base, char delimiter);
attr_nonnull parse_error_t parse_float(int index, const string_t *src, double *dst, char delimiter);
attr_nonnull void string_decode_http(string_t *to, const string_t *from);

#endif
