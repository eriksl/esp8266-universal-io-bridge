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

_Static_assert(sizeof(bool_t) == 4, "sizeof(bool_t) != 4");

#define irom __attribute__((section(".irom0.text")))
#define iram __attribute__((section(".text")))
#define roflash __attribute__((section(".flash.rodata"))) __attribute__((aligned(sizeof(char*))))
#define noinline __attribute__ ((noinline))
#define attr_pure __attribute__ ((pure))
#define attr_const __attribute__ ((const))

// replacement for nasty #defines that give warnings

void pin_func_select(uint32_t pin_name, uint32_t pin_func);

// prototypes missing

int ets_vsnprintf(char *, size_t, const char *, va_list);

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

// local utility functions missing from libc

int snprintf(char *, size_t, const char *, ...) __attribute__ ((format (printf, 3, 4)));
int snprintf_roflash(char *, size_t, const char *, ...) __attribute__ ((format (printf, 3, 4)));
size_t strlcpy_roflash(char *dst, const char *from_flash_ptr, size_t size);
void *malloc(size_t);
void *zalloc(size_t);
void *realloc(void *, size_t);

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

// other convenience functions

void reset(void);
const char *yesno(bool_t value);
const char *onoff(bool_t value);
int dprintf(const char *fmt, ...);
void msleep(unsigned int);
unsigned int double_to_string(double value, unsigned int precision, double top_decimal, unsigned int size, char *dst);
double string_to_double(const char *);
void split_ip_addr(ip_addr_t, unsigned int [4]);
ip_addr_t join_ip_addr(const unsigned int [4]);
unsigned int ip_addr_to_string(unsigned int size, char *dst, ip_addr_t);
ip_addr_t string_to_ip_addr(const char *);
bool ip_addr_valid(ip_addr_t);
void md5_hash_to_string(const char *hash, unsigned int size, char *string);
int string_to_int_ex(const char *startptr, const char **endptr, unsigned int size, int base, bool *valid);
int string_to_int(const char *startptr);
int hex_string_to_int(const char *startptr);
int hex_to_bin(int src_length, const char *src, int dst_size, char *dst);

#define strcpy #error strcpy unsafe
#define strncpy #error strncpy unsafe
#define strcat #error strcat unsafe
#define strncat #error strncat unsafe

#endif
