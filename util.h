#ifndef util_h
#define util_h

#include <lwip/ip_addr.h>

#include "attribute.h"
#include "sys_string.h"

#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>
#include <string.h>

enum
{
	off = 0,
	no = 0,
	on = 1,
	yes = 1
};

_Static_assert(sizeof(bool) == 1, "sizeof(bool) != 1");

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

extern string_t logbuffer;
extern unsigned int logbuffer_display_current;
void logbuffer_clear(void);

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

void logchar(char c);

#endif
