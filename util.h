#ifndef util_h
#define util_h

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

#include <sys/types.h>
#include <stdarg.h>
#include <stdint.h>
#include <osapi.h>
#include <ets_sys.h>
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

_Static_assert(sizeof(bool_t) == 4, "sizeof(bool_t) != 4");

#define irom __attribute__((section(".irom0.text")))
#define iram __attribute__((section(".text")))
#define roflash __attribute__((section(".flash.rodata"))) __attribute__((aligned(sizeof(char*))))
#define noinline __attribute__ ((noinline))
#define always_inline inline __attribute__((always_inline))
#define attr_pure __attribute__ ((pure))
#define attr_const __attribute__ ((const))
#define attr_packed __attribute__ ((__packed__))
#define assert_size(type, size) _Static_assert(sizeof(type) == size, "sizeof(" #type ") != " #size)

typedef union
{
	ip_addr_t	ip_addr;
	uint8_t		byte[3];
} ip_addr_to_bytes_t;

typedef struct
{
	int size;
	int length;
	char *buffer;
} string_t;

extern string_t buffer_4k;

// defines missing

#define FUNC_U0RXD 0

// prototypes missing

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

void ets_isr_attach(int, void *, void *);
void ets_isr_mask(unsigned int);
void ets_isr_unmask(unsigned int);
void ets_timer_arm_new(ETSTimer *, uint32_t, bool_t, int);
void ets_timer_disarm(ETSTimer *);
void ets_timer_setfn(ETSTimer *, ETSTimerFunc *, void *);
void NmiTimSetFunc(void *);
void ets_delay_us(uint16_t);
int ets_memcmp(const void *, const void *, size_t);
size_t ets_strlen(const char *);
int ets_strcmp(const char *, const char *);
int ets_strncmp(const char *, const char *, size_t);
void *ets_memcpy(void *, const void *, size_t);
void *ets_memset(void *, int, size_t);
char *ets_strcpy(char *, const char *);

#define pvPortMalloc #pragma error dont use pvPortMalloc
#define pvPortZalloc #pragma error dont use pvPortZalloc
#define pvPortRealloc #pragma error dont use pvPortRealloc
#define vPortFree #pragma error dont use vPortFree

// other convenience functions

void reset(void);
const char *yesno(bool_t value);
const char *onoff(bool_t value);
int dprintf(const char *fmt, ...);
void msleep(int);
ip_addr_t ip_addr(const char *);
bool_t ip_addr_valid(ip_addr_t);
struct tm *sntp_localtime(const time_t *);

#define strcpy #pragma error strcpy unsafe
#define strncpy #pragma error strncpy unsafe
#define strcat #pragma error strcat unsafe
#define strncat #pragma error strncat unsafe

#define strlen #pragma error use ets_strlen
#define strcmp #pragma error use ets_strcmp
#define strncmp #pragma error use ets_strncmp
#define memcpy #pragma error use ets_memcpy
#define memset #pragma error use ets_memset
#define memcmp #pragma error use ets_memcmp
//size_t strlcpy(char *, const char *, size_t);
//void *memchr(const void *s, int c, size_t n);


// undocumented functions from ROM

typedef struct {
  uint32_t i[2];
  uint32_t buf[4];
  unsigned char in[64];
  unsigned char digest[16];
} MD5_CTX;

void MD5Init(MD5_CTX *mdContext);
void MD5Update(MD5_CTX *mdContext, const unsigned char *inBuf, unsigned int inLen);
void MD5Final(unsigned char hash[], MD5_CTX *mdContext);

// string handling

#define string_new(_linkage, _name, _size) \
	_linkage char _ ## _name ## _buf[_size]; \
	_linkage string_t _name = { .size = _size, .length = 0, .buffer = _ ## _name ## _buf }

string_t string_from_ptr(size_t size, char *buffer);
char * string_to_ptr(string_t *string);
const char * string_to_const_ptr(const string_t *string);
void string_set(string_t *string, char *buffer, int size, int length);

void string_format_ptr(string_t *dst, const char *, ...) __attribute__ ((format (printf, 2, 3)));
void string_cat_ptr(string_t *, const char *);

#define string_format(dst, fmt, ...) \
do { \
	static roflash const char fmt_flash[] = fmt; \
	string_format_ptr(dst, fmt_flash, __VA_ARGS__); \
} while(0)

#define string_cat(dst, src) \
do { \
	static roflash const char src_flash[] = src; \
	string_cat_ptr(dst, src_flash); \
} while(0)

#define string_copy(dst, src) \
do { \
	static roflash const char src_flash[] = src; \
	string_clear(dst); \
	string_cat_ptr(dst, src_flash); \
} while(0)

void string_cat_strptr(string_t *dst, const char *src);
int string_copy_string(string_t *dst, string_t *src);
void string_ip(string_t *dst, ip_addr_t);
int string_double(string_t *dst, double value, int precision, double top_decimal);
static inline int string_length(const string_t *dst) { return(dst->length); }
static inline int string_size(const string_t *dst) { return(dst->size - 1); }
static inline bool_t string_space(const string_t *dst) { return(string_length(dst) < string_size(dst)); }
static inline void string_clear(string_t *dst) { dst->length = 0; };
void string_setlength(string_t *dst, int length);
void string_append(string_t *dst, char c);
bool_t string_match(const string_t *, const char *);
bool_t string_match_string(const string_t *, const string_t *);
bool_t string_nmatch(const string_t *, const char *, int);
char string_index(const string_t *, int index);
int string_sep(const string_t *, int offset, int occurence, char c);
int string_find(const string_t *, int offset, char c);
void string_replace(string_t *, int index, char c);
void string_splice(string_t *dst, const string_t *src, int src_offset, int length);
void string_bin_to_hex(string_t *dst, const char *src, int length);
void string_crc32_init(void);
uint32_t string_crc32(const string_t *src, int offset, int length);

parse_error_t parse_string(int index, const string_t *in, string_t *out);
parse_error_t parse_int(int index, const string_t *src, int *dst, int base);
parse_error_t parse_float(int index, const string_t *, double *);
#endif
