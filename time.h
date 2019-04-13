#ifndef time__h
#define time__h

#include "util.h"
#include "attribute.h"

#include <stdint.h>
#include <stdbool.h>

void time_uptime_stats(string_t *);
void time_system_stats(string_t *);
void time_rtc_stats(string_t *);
void time_timer_stats(string_t *);
void time_sntp_stats(string_t *);

void time_init(void);
void time_periodic(void);
void time_sntp_start(void);

void		time_set_hms(unsigned int h, unsigned int m, unsigned int s);
void		time_set_stamp(unsigned int base);
const char *time_get(unsigned int *h, unsigned int *m, unsigned int *s,
				unsigned int *Y, unsigned int *M, unsigned int *D);
uint64_t	time_get_us(void);

#endif
