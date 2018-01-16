#include "time.h"

#include "config.h"
#include "stats.h"

#include <user_interface.h>
#include <sntp.h>

typedef struct
{
	unsigned int ntp_server_valid:1;
} time_flags_t;

static time_flags_t time_flags;

// uptime

static unsigned int uptime_last_us;
static unsigned int uptime_base_us;
static unsigned int uptime_wraps;

irom static void uptime_init(void)
{
	uptime_last_us = 0;
	uptime_wraps = 0;

	uptime_base_us = system_get_time();
}

iram static void uptime_periodic(void)
{
	unsigned int uptime_now = system_get_time();

	if(uptime_now < uptime_last_us)
		uptime_wraps++;

	uptime_last_us = uptime_now;
}

irom void time_uptime_get(unsigned int *s, unsigned int *ms,
		unsigned int *raw1, unsigned int *raw2,
		unsigned int *base, unsigned int *wraps)
{
	uint64_t uptime_us;

	uptime_us = (((uint64_t)system_get_time()) | ((uint64_t)uptime_wraps << 32)) - uptime_base_us;

	if(s)
		*s = uptime_us / 1000000;

	if(ms)
		*ms = (uptime_us % 1000000) / 1000;

	if(raw1)
		*raw1 = uptime_us >> 32;

	if(raw2)
		*raw2 = uptime_us & 0xffffffff;

	if(base)
		*base = uptime_base_us;

	if(wraps)
		*wraps = 0;
}

// system

static unsigned int system_last_us;
static unsigned int system_base_us;
static unsigned int system_wraps;

irom static void system_init(void)
{
	system_last_us = 0;
	system_wraps = 0;

	system_base_us = system_get_time();
}

iram static void system_periodic(void)
{
	unsigned int system_now = system_get_time();

	if(system_now < system_last_us)
		system_wraps++;

	system_last_us = system_now;
}

irom void time_system_get(unsigned int *s, unsigned int *ms,
		unsigned int *raw1, unsigned int *raw2,
		unsigned int *base, unsigned int *wraps)
{
	uint64_t system_us;

	system_us = (((uint64_t)system_get_time()) | ((uint64_t)system_wraps << 32)) - system_base_us;

	if(s)
		*s = system_us / 1000000;

	if(ms)
		*ms = (system_us % 1000000) / 1000;

	if(raw1)
		*raw1 = system_us >> 32;

	if(raw2)
		*raw2 = system_us & 0xffffffff;

	if(base)
		*base = system_base_us;

	if(wraps)
		*wraps = 0;
}

// rtc

static uint64_t rtc_current_ns;
static unsigned int rtc_base_s;
static unsigned int rtc_last_value;
static unsigned int rtc_wraps;

irom static void rtc_init(void)
{
	unsigned int rtc_current_value;
	uint64_t calvalue_ns;

	rtc_current_value = system_get_rtc_time();
	rtc_last_value = rtc_current_value;

	calvalue_ns = ((uint64_t)system_rtc_clock_cali_proc() * 1000) >> 12;
	rtc_base_s = (rtc_current_value * calvalue_ns) / 1000000000;

	rtc_current_ns = 0;
	rtc_wraps = 0;
}

iram static void rtc_periodic(void)
{
	unsigned int rtc_current_value;
	uint64_t calvalue_ns, diff;

	rtc_current_value = system_get_rtc_time();

	if(rtc_current_value >= rtc_last_value)
		diff = (uint64_t)rtc_current_value - (uint64_t)rtc_last_value;
	else
	{
		rtc_wraps++;
		diff = ((uint64_t)1 << 32) - ((uint64_t)rtc_last_value - (uint64_t)rtc_current_value);
	}

	calvalue_ns = ((uint64_t)system_rtc_clock_cali_proc() * 1000) >> 12;
	diff *= calvalue_ns;

	rtc_current_ns += diff;

	rtc_last_value = rtc_current_value;
}

irom void time_rtc_get(unsigned int *s, unsigned int *ms,
		unsigned int *raw1, unsigned int *raw2,
		unsigned int *base, unsigned int *wraps)
{
	uint64_t current;

	current = rtc_current_ns;

	if(s)
		*s = (current / 1000000000);

	if(ms)
		*ms = (current % 1000000000) / 1000000;

	if(raw1)
		*raw1 = current & 0xffffffff;

	if(raw2)
		*raw2 = current >> 32;

	if(base)
		*base = rtc_base_s;

	if(wraps)
		*wraps = rtc_wraps;
}

// timer

static unsigned int timer_s;
static unsigned int timer_ms;
static unsigned int timer_wraps;

irom static void timer_init(void)
{
	timer_s = 0;
	timer_ms = 0;
	timer_wraps = 0;
}

iram static void timer_periodic(void)
{
	timer_ms += 100;

	if(timer_ms > 999)
	{
		timer_ms = 0;

		if(++timer_s == 0)
			timer_wraps++;
	}
}

irom void time_timer_get(unsigned int *s, unsigned int *ms,
		unsigned int *raw1, unsigned int *raw2,
		unsigned int *base, unsigned int *wraps)
{
	if(s)
		*s = timer_s;

	if(ms)
		*ms = timer_ms;

	if(raw1)
		*raw1 = timer_s;

	if(raw2)
		*raw2 = timer_ms;

	if(base)
		*base = 0;

	if(wraps)
		*wraps = timer_wraps;
}

// ntp

static unsigned int ntp_base_s = 0;
static ip_addr_to_bytes_t ntp_server;
static int ntp_timezone;

irom void time_ntp_init(void)
{
	int ix;
	int byte;

	sntp_stop();

	for(ix = 0; ix < 4; ix++)
		if(!config_get_int("ntp.server.%u", ix, 0, &byte))
			break;
		else
			ntp_server.byte[ix] = (uint8_t)byte;

	if(ix >= 4)
		time_flags.ntp_server_valid = 1;
	else
	{
		time_flags.ntp_server_valid = 0;

		for(ix = 0; ix < 4; ix++)
			ntp_server.byte[ix] = 0;
	}

	if(!config_get_int("ntp.tz", -1, -1, &ntp_timezone))
		ntp_timezone = 0;

	sntp_setserver(0, &ntp_server.ip_addr);
	sntp_set_timezone(ntp_timezone);

	if(time_flags.ntp_server_valid)
		sntp_init();
}

iram static void ntp_periodic(void)
{
	static int delay = 0;
	static bool_t initial_burst = true;
	time_t ntp_s;

	if(!time_flags.ntp_server_valid)
		return;

	delay++;

	if(delay < 10) // always check at most once a second or less frequently
		return;

	if(!initial_burst && (delay < 6000)) // after initial burst only check every 10 minutes
		return;

	delay = 0;

	if((ntp_s = sntp_get_current_timestamp()) > 0)
	{
		initial_burst = false;
		stat_update_ntp++;
		sntp_stop();
		sntp_init(); // FIXME SDK bug, stop and start ntp to get continuous updating
	}

	if(ntp_base_s == 0)
		ntp_base_s = ntp_s;
}

irom void time_ntp_get(unsigned int *s, unsigned int *ms,
		unsigned int *raw1, unsigned int *raw2,
		unsigned int *base, unsigned int *wraps)
{
	time_t current;

	if(!time_flags.ntp_server_valid)
	{
		if(ms)
			*ms = 0;

		if(raw1)
			*raw1 = 0;

		if(raw2)
			*raw2 = 0;

		if(base)
			*base = 0;

		if(wraps)
			*wraps = 0;
	}
	else
	{
		current = sntp_get_current_timestamp();

		if(s)
		{
			if(current > 0)
				*s = current - ntp_base_s;
			else
				*s = 0;
		}

		if(ms)
			*ms = 0;

		if(raw1)
			*raw1 = current;

		if(raw2)
			*raw2 = 0;

		if(base)
			*base = ntp_base_s;

		if(wraps)
			*wraps = 0;
	}
}

// generic interface

static unsigned int time_base_s;

irom void time_init(void)
{
	time_base_s = 0;

	uptime_init();
	system_init();
	rtc_init();
	timer_init();
	time_ntp_init();
}

iram void time_periodic(void)
{
	uptime_periodic();
	system_periodic();
	rtc_periodic();
	timer_periodic();
	ntp_periodic();
}

irom void time_set_stamp(unsigned int stamp)
{
	time_base_s = stamp;
	system_init();
	rtc_init();
	timer_init();
}

irom void time_set_hms(unsigned int h, unsigned int m, unsigned int s)
{
	time_set_stamp((h * 3600) + (m * 60) + s);
}

irom const char *time_get(unsigned int *h, unsigned int *m, unsigned int *s,
			unsigned int *Y, unsigned int *M, unsigned int *D)
{
	unsigned int time_s = 0;
	time_t ticks_s;
	const char *source;
	struct tm *tm;

	if(ntp_base_s > 0) // we have ntp sync
	{
		source = "ntp";
		time_ntp_get(0, 0, &time_s, 0, 0, 0);
	}
	else
	{
		source = "rtc";
		time_rtc_get(&time_s, 0, 0, 0, 0, 0);
		time_s = (unsigned int)(time_s + time_base_s);
	}

	ticks_s  = time_s;
	tm = sntp_localtime(&ticks_s);

	if(Y)
		*Y = tm->tm_year + 1900;

	if(M)
		*M  = tm->tm_mon + 1;

	if(D)
		*D = tm->tm_mday;

	if(h)
		*h = tm->tm_hour;

	if(m)
		*m = tm->tm_min;

	if(s)
		*s = tm->tm_sec;

	return(source);
}
