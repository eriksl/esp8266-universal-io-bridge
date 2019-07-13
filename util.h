#ifndef util_h
#define util_h

#pragma GCC diagnostic ignored "-Wpacked"
#pragma GCC diagnostic push
#include <lwip/ip_addr.h>
#pragma GCC diagnostic pop

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

attr_nonnull ip_addr_t ip_addr(const char *);

extern string_t logbuffer;

unsigned int attr_nonnull log_from_flash_no_format(const char *data);
unsigned int attr_nonnull log_from_flash(const char *fmt_in_flash, ...) __attribute__ ((format (printf, 1, 2)));

#define log(fmt, ...) \
do { \
	static roflash const char log_flash[] = fmt; \
	log_from_flash(log_flash, ## __VA_ARGS__); \
} while(0)

#define log_no_format(fmt, ...) \
do { \
	static roflash const char log_flash[] = fmt; \
	log_from_flash_no_format(log_flash, ## __VA_ARGS__); \
} while(0)

void logchar(char c);

#endif
