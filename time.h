#ifndef time__h
#define time__h

#include <util.h>

// system clock (system_get_time())

void	time_system_get(unsigned int *secs, unsigned int *msecs,
			unsigned int *raw1, unsigned int *raw2,
			unsigned int *base, unsigned int *wraps);

// rtc clock (system_get_rtc_time())

void	time_rtc_get(unsigned int *secs, unsigned int *msecs,
			unsigned int *raw, unsigned int *raw2,
			unsigned int *base, unsigned int *wraps);

// timer clock

void	time_timer_get(unsigned int *secs, unsigned int *msecs,
			unsigned int *raw, unsigned int *raw2,
			unsigned int *base, unsigned int *wraps);

// ntp clock

void		time_ntp_init(void);
void		time_ntp_get(unsigned int *secs, unsigned int *msecs,
				unsigned int *raw, unsigned int *raw2,
				unsigned int *base, unsigned int *wraps);

// generic interface

void		time_init(void);
void		time_periodic(void);
void		time_set_hms(unsigned int h, unsigned int m, unsigned int s);
void		time_set(unsigned int base);
const char *time_get(unsigned int *h, unsigned int *m, unsigned int *s,
				unsigned int *Y, unsigned int *M, unsigned int *D);

#endif
