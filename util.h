#ifndef util_h
#define util_h

#include <sys/types.h>
#include <stdarg.h>
#include <stdint.h>

#include <c_types.h>
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
#define attr_pure __attribute__ ((pure))
#define attr_const __attribute__ ((const))

typedef struct
{
	int size;
	int length;
	char *buffer;
} string_t;

extern string_t buffer_4k;

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
void ets_isr_mask(unsigned intr);
void ets_isr_unmask(unsigned intr);
void ets_timer_arm_new(ETSTimer *, uint32_t, bool, int);
void ets_timer_disarm(ETSTimer *);
void ets_timer_setfn(ETSTimer *, ETSTimerFunc *, void *);
void ets_delay_us(uint16_t);
int ets_memcmp(const void *, const void *, size_t);
size_t ets_strlen(const char *);
int ets_strcmp(const char *, const char *);
int ets_strncmp(const char *, const char *, size_t);
void *ets_memcpy(void *, const void *, size_t);
void *ets_memset(void *, int, size_t);
char *ets_strcpy(char *, const char *);

void *pvPortMalloc(size_t size, const char *file, unsigned int line);
void *pvPortZalloc(size_t size, const char *file, unsigned int line);
void *pvPortRealloc(void *ptr, size_t size, const char *file, unsigned int line);
void vPortFree(void *ptr, const char *file, int line);
struct tm *sntp_localtime(const time_t *);

#define strcpy #error strcpy unsafe
#define strncpy #error strncpy unsafe
#define strcat #error strcat unsafe
#define strncat #error strncat unsafe

#define strlen #error use ets_strlen
#define strcmp #error use ets_strcmp
#define strncmp #error use ets_strncmp
//#define memcpy #error use ets_memcpy
#define memset #error use ets_memset
#define memcmp #error use ets_memcmp
//size_t strlcpy(char *, const char *, size_t);

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

void string_format_ptr(string_t *dst, const char *, ...) __attribute__ ((format (printf, 2, 3)));
void string_cat_ptr(string_t *, const char *);

#define string_format(dst, fmt, ...) \
{ \
	static roflash const char fmt_flash[] = fmt; \
	string_format_ptr(dst, fmt_flash, __VA_ARGS__); \
}

#define string_cat(dst, src) \
{ \
	static roflash const char src_flash[] = src; \
	string_cat_ptr(dst, src_flash); \
}

#define string_copy(dst, src) \
{ \
	static roflash const char src_flash[] = src; \
	string_clear(dst); \
	string_cat_ptr(dst, src_flash); \
}

int string_copy_string(string_t *dst, string_t *src);
void string_ip(string_t *dst, ip_addr_t);
int string_double(string_t *dst, double value, int precision, double top_decimal);
static inline int string_length(const string_t *dst) { return(dst->length); }
static inline int string_size(const string_t *dst) { return(dst->size - 1); }
static inline bool string_space(const string_t *dst) { return(string_length(dst) < string_size(dst)); }
static inline void string_clear(string_t *dst) { dst->length = 0; };
void string_setlength(string_t *dst, int length);
void string_append(string_t *dst, char c);
bool string_match(const string_t *, const char *);
bool string_match_string(const string_t *, const string_t *);
bool string_nmatch(const string_t *, const char *, int);
char string_index(const string_t *, int index);
int string_sep(const string_t *, int offset, int occurence, char c);
int string_bin_to_hex(string_t *dst, const string_t *src, int offset);
int string_hex_to_bin(string_t *dst, const string_t *src, int offset);

parse_error_t parse_string(int index, const string_t *in, string_t *out);
parse_error_t parse_int(int index, const string_t *src, int *dst, int base);
parse_error_t parse_float(int index, const string_t *, double *);

// other convenience functions

void reset(void);
const char *yesno(bool_t value);
const char *onoff(bool_t value);
int dprintf(const char *fmt, ...);
void msleep(int);
ip_addr_t ip_addr(const char *);
bool ip_addr_valid(ip_addr_t);
void pin_func_select(uint32_t pin_name, uint32_t pin_func);

#endif
