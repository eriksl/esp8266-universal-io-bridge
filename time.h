#ifndef time__h
#define time__h

#include <util.h>

string_t *time_uptime_stats(void);
string_t *time_system_stats(void);
string_t *time_rtc_stats(void);
string_t *time_timer_stats(void);
string_t *time_ntp_stats(void);

void time_ntp_init(void);
void time_init(void);
void time_periodic(void);

void		time_set_hms(unsigned int h, unsigned int m, unsigned int s);
void		time_set_stamp(unsigned int base);
const char *time_get(unsigned int *h, unsigned int *m, unsigned int *s,
				unsigned int *Y, unsigned int *M, unsigned int *D);
uint64_t	time_get_us(void);

#endif
