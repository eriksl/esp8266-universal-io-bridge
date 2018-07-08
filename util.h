#ifndef util_h
#define util_h

#include <sys/types.h>
#include <stdarg.h>
#include <stdint.h>
#include <osapi.h>
#include <ip_addr.h>

typedef enum
{
	off = 0,
	no = 0,
	on = 1,
	yes = 1
} bool_t;

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

_Static_assert(sizeof(bool_t) == 4, "sizeof(bool_t) != 4");

#define irom __attribute__((section(".irom0.text")))
#define iram __attribute__((section(".text")))
#define stack auto
#define roflash __attribute__((section(".flash.rodata"))) __attribute__((aligned(sizeof(uint32_t))))
#define noinline __attribute__ ((noinline))
#define always_inline inline __attribute__((always_inline))
#define attr_used __attribute__ ((unused))
#define attr_pure __attribute__ ((pure))
#define attr_const __attribute__ ((const))
#define attr_packed __attribute__ ((__packed__))
#define attr_speed __attribute__ ((optimize("O3", "unroll-loops")))
#define assert_size(type, size) _Static_assert(sizeof(type) == size, "sizeof(" #type ") != " #size)

// make sure we don't use the broken memory management

#undef pvPortMalloc
#undef pvPortZalloc
#undef pvPortRealloc
#undef vPortFree

#include <mem.h>

#undef pvPortMalloc
#undef pvPortZalloc
#undef pvPortRealloc
#undef vPortFree

#define pvPortMalloc #error dont use pvPortMalloc
#define pvPortZalloc #error dont use pvPortZalloc
#define pvPortRealloc #error dont use pvPortRealloc
#define vPortFree #error dont use vPortFree

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

void ets_isr_mask(unsigned int);
void ets_isr_unmask(unsigned int);
int ets_vsnprintf(char *, size_t, const char *, va_list);
void system_get_string_from_flash(const void *src, char *dst, size_t length);
struct tm *sntp_localtime(const time_t *);

int MD5Init(MD5_CTX *context);
int MD5Update(MD5_CTX *context, const void *, unsigned int length);
int MD5Final(unsigned char *hash, MD5_CTX *context);

int SHA1Init(SHA_CTX *context);
int SHA1Update(SHA_CTX *context, const void *, unsigned int length);
int SHA1Final(unsigned char *md, SHA_CTX *context);

enum { SHA_DIGEST_LENGTH = 20 };

// prototypes missing

double pow(double, double);

// functions missing from SDK libmain (but declared in headers)

/* int isxdigit(int c); */
/* void *memchr(const void *s, int c, size_t n); */

extern char flash_dram_buffer[1024];

// ugly kludge for incorrectly declared spi_flash_* functions */
#include <c_types.h>
#undef spi_flash_write
#define spi_flash_read _spi_flash_read
#define spi_flash_write _spi_flash_write
#include <spi_flash.h>
#undef spi_flash_read
#undef spi_flash_write
SpiFlashOpResult spi_flash_read(uint32_t src, void *dst, uint32_t size);
SpiFlashOpResult spi_flash_write(uint32_t dst, const void *src, uint32_t size);

// convenience functions

int strecpy(char *dst, const char *src, int size);
size_t strecpy_from_flash(char *dst, const uint32_t *src_flash, int size);
void reset(void);
const char *yesno(bool_t value);
const char *onoff(bool_t value);
int dprintf(const char *fmt, ...);
int log(const char *fmt, ...) __attribute__ ((format (printf, 1, 2)));
void logchar(char c);
void msleep(int);
ip_addr_t ip_addr(const char *);

// string functions

typedef struct
{
	int size;
	int length;
	char *buffer;
} string_t;

extern string_t logbuffer;

void string_format_cstr(string_t *dst, const char *fmt, ...) __attribute__ ((format (printf, 2, 3)));
void string_format_flash_ptr(string_t *dst, const char *, ...) __attribute__ ((format (printf, 2, 3)));
int string_sep(const string_t *, int offset, int occurrence, char c);
int string_find(const string_t *, int offset, char c);
void string_replace(string_t *, int index, char c);
void string_splice(string_t *dst, int dst_offset, const string_t *src, int src_offset, int length);
void string_trim_nl(string_t *dst);
void string_trim_string(string_t *haystack_string, const string_t *needle_string);
void string_bin_to_hex(string_t *dst, const char *src, int length);
void string_ip(string_t *dst, ip_addr_t);
void string_mac(string_t *dst, uint8 mac_addr[6]);
int string_double(string_t *dst, double value, int precision, double top_decimal);
void string_crc32_init(void);
uint32_t string_crc32(const string_t *src, int offset, int length);

#define string_new(_linkage, _name, _size) \
	_linkage char _ ## _name ## _buf[_size] = { 0 }; \
	_linkage string_t _name = { .size = _size, .length = 0, .buffer = _ ## _name ## _buf }

#define string_init(_name, _string) \
	string_new(stack, _name, sizeof(_string)); \
	string_append(&_name, _string);

#define string_append(dst, src) \
do { \
	static roflash const char src_flash[] = src; \
	string_append_cstr_flash(dst, src_flash); \
} while(0)

#define string_format(dst, fmt, ...) \
do { \
	static roflash const char fmt_flash[] = fmt; \
	string_format_flash_ptr(dst, fmt_flash, __VA_ARGS__); \
} while(0)

always_inline static int string_length(const string_t *dst)
{
	return(dst->length);
}

always_inline static int string_size(const string_t *dst)
{
	return(dst->size);
}

always_inline static int string_empty(const string_t *dst)
{
	return(dst->length == 0);
}

always_inline static const char *string_buffer(const string_t *string)
{
	return(string->buffer);
}

always_inline static char *string_buffer_nonconst(string_t *string)
{
	return(string->buffer);
}

always_inline static bool_t string_space(const string_t *dst)
{
	return(dst->length < dst->size);
}

always_inline static void string_clear(string_t *dst)
{
	dst->length = 0;
};

always_inline static void string_set(string_t *dst, char *buffer, int size, int length)
{
	dst->buffer = buffer;
	dst->size   = size;
	dst->length = length;
}

always_inline static void string_setlength(string_t *dst, int length)
{
	if(length > dst->size)
		length = dst->size;

	dst->length = length;
}

always_inline static bool_t string_match_string(const string_t *s1, const string_t *s2)
{
	if((s1->length == s2->length) && (memcmp(s1->buffer, s2->buffer, s1->length) == 0))
		return(true);

	return(false);
}

always_inline static bool_t string_match_cstr(const string_t *s1, const char *s2)
{
	int length = strlen(s2);

	if((s1->length == length) && (memcmp(s1->buffer, s2, length) == 0))
		return(true);

	return(false);
}

always_inline static bool_t string_nmatch_string(const string_t *s1, const string_t *s2, int n)
{
	if((s1->length >= n) && (s2->length >= n) && (memcmp(s1->buffer, s2->buffer, n) == 0))
		return(true);

	return(false);
}

always_inline static bool_t string_nmatch_cstr(const string_t *s1, const char *s2, int n)
{
	int length = strlen(s2);

	if((s1->length >= n) && (length >= n) && (memcmp(s1->buffer, s2, n) == 0))
		return(true);

	return(false);
}

always_inline static string_t string_from_cstr(size_t size, char *cstr)
{
	string_t string = { size, strlen(cstr), cstr };

	return(string);
}

always_inline static const char *string_to_cstr(string_t *string)
{
	if(string->length >= string->size)
		string->length = string->size - 1;

	string->buffer[string->length] = '\0';

	return(string->buffer);
}

always_inline static char string_at(const string_t *s, int at)
{
	if((unsigned int)at < (unsigned int)s->length)
		return(s->buffer[at]);
	else
		return('\0');
}

always_inline static void string_append_char(string_t *dst, char c)
{
	if(dst->length < dst->size)
		dst->buffer[dst->length++] = c;

	dst->buffer[dst->length] = '\0';
}

always_inline static void string_append_cstr(string_t *dst, const char *src)
{
	dst->length += strecpy(dst->buffer + dst->length, src, dst->size - dst->length);
}

always_inline static void string_append_cstr_flash(string_t *dst, const char *src)
{
	dst->length += strecpy_from_flash(dst->buffer + dst->length, (const uint32_t *)(const void *)src, dst->size - dst->length);
}

always_inline static void string_append_string(string_t *dst, const string_t *src)
{
	int length = src->length;

	if((dst->length + length) > dst->size)
		length = dst->size - dst->length;

	memcpy(dst->buffer + dst->length, src->buffer, length);

	dst->length += length;
}

parse_error_t parse_string(int index, const string_t *in, string_t *out, char delim);
parse_error_t parse_int(int index, const string_t *src, int *dst, int base, char delim);
parse_error_t parse_float(int index, const string_t *, double *, char delim);
#endif
