#ifndef util_h
#define util_h

#include <sys/types.h>
#include <stdarg.h>
#include <stdint.h>

#include <c_types.h>
#include <osapi.h>
#include <ets_sys.h>

typedef enum __attribute__ ((__packed__))
{
	off = 0,
	no = 0,
	on = 1,
	yes = 1
} bool_t;

_Static_assert(sizeof(bool_t) == 1, "sizeof(bool_t) != 1");

#define noinline __attribute__ ((noinline))

// replacement for nasty #defines that give warnings

void pin_func_select(uint32_t pin_name, uint32_t pin_func);

// prototypes missing

size_t strlcpy(char *, const char *, size_t);
size_t strlcat(char *, const char *, size_t);
size_t strlen(const char *);
int strcmp(const char *, const char *);

unsigned long int strtoul(const char *, char **, int);

void *memset(void *, int, size_t);
void *memcpy(void *, const void *, size_t);

void *pvPortMalloc(size_t);
int ets_vsnprintf(char *, size_t, const char *, va_list);

void ets_isr_attach(int, void *, void *);
void ets_isr_mask(unsigned intr);
void ets_isr_unmask(unsigned intr);
void ets_timer_arm(ETSTimer *, uint32_t, bool);
void ets_timer_arm_new(ETSTimer *, uint32_t, bool, int);
void ets_timer_disarm(ETSTimer *);
void ets_timer_setfn(ETSTimer *, ETSTimerFunc *, void *);
void ets_install_putc1(void(*)(char));
void ets_delay_us(uint16_t);

// local utility functions missing from libc

int snprintf(char *, size_t, const char *, ...) __attribute__ ((format (printf, 3, 4)));
void *malloc(size_t);
int atoi(const char *);

// other handy functions

void reset(void);
const char *yesno(bool_t value);
const char *onoff(bool_t value);
int dprintf(const char *fmt, ...);
void msleep(uint16_t);
uint16_t double_to_string(double value, uint8_t precision, double top_decimal, uint16_t size, char *dst);

#endif
