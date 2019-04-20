#include "sys_time.h"
#include "config.h"
#include "stats.h"
#include "sdk.h"
#include "util.h"
#include "lwip-interface.h"

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

enum
{
	sntp_misc_li_mask =		0b11000000,
	sntp_misc_li_shift =	6,
	sntp_misc_vn_mask =		0b00111000,
	sntp_misc_vn_shift =	3,
	sntp_misc_mode_mask =	0b00000111,
	sntp_misc_mode_shift =	0,
	sntp_misc_mode_client =	0b00000011,
};

typedef struct
{
	uint8_t	misc;
	uint8_t stratum;
	uint8_t	poll;
	uint8_t	precision;
	uint8_t	root_delay[4];
	uint8_t	root_dispersion[4];
	uint8_t	reference_identifier[4];
	uint8_t	reference_timestamp[8];
	uint8_t	originate_timestamp[8];
	uint8_t	receive_timestamp[8];
	uint8_t	transmit_timestamp[8];
} sntp_network_t;

assert_size(sntp_network_t, 48);

typedef struct
{
	unsigned int sntp_server_valid:1;
	unsigned int sntp_init_succeeded:1;
	unsigned int sntp_regular_mode:1;
} time_flags_t;

string_new(static, sntp_socket_receive_buffer, sizeof(sntp_network_t));
string_new(static, sntp_socket_send_buffer, sizeof(sntp_network_t));
static lwip_if_socket_t sntp_socket;

static void sms_to_date(string_t *dst, int s, int ms, int r1, int r2, int b, int w);

static time_flags_t time_flags;

// uptime

static unsigned int uptime_last_us;
static unsigned int uptime_base_us;
static unsigned int uptime_wraps;

static void uptime_init(void)
{
	uptime_last_us = 0;
	uptime_wraps = 0;

	uptime_base_us = system_get_time();
}

attr_inline void uptime_periodic(void)
{
	unsigned int uptime_now = system_get_time();

	if(uptime_now < uptime_last_us)
		uptime_wraps++;

	uptime_last_us = uptime_now;
}

iram uint64_t time_get_us(void)
{
	return((((uint64_t)system_get_time()) | ((uint64_t)uptime_wraps << 32)) - uptime_base_us);
}

static void uptime_get(unsigned int *s, unsigned int *ms,
		unsigned int *raw1, unsigned int *raw2,
		unsigned int *base, unsigned int *wraps)
{
	uint64_t uptime_us = time_get_us();

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
		*wraps = uptime_wraps;
}

void time_uptime_stats(string_t *dst)
{
	unsigned int secs, msecs, raw1, raw2, base, wraps;

	uptime_get(&secs, &msecs, &raw1, &raw2, &base, &wraps);
	sms_to_date(dst, secs, msecs, raw1, raw2, base, wraps);
}

// system

static unsigned int system_last_us;
static unsigned int system_base_us;
static unsigned int system_wraps;

static void system_init(void)
{
	system_last_us = 0;
	system_wraps = 0;

	system_base_us = system_get_time();
}

attr_inline void system_periodic(void)
{
	unsigned int system_now = system_get_time();

	if(system_now < system_last_us)
		system_wraps++;

	system_last_us = system_now;
}

static void system_get(unsigned int *s, unsigned int *ms,
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
		*wraps = system_wraps;
}

void time_system_stats(string_t *dst)
{
	unsigned int secs, msecs, raw1, raw2, base, wraps;

	system_get(&secs, &msecs, &raw1, &raw2, &base, &wraps);
	sms_to_date(dst, secs, msecs, raw1, raw2, base, wraps);
}

// rtc

static uint64_t rtc_current_ns;
static unsigned int rtc_base_s;
static unsigned int rtc_last_value;
static unsigned int rtc_wraps;

static void rtc_init(void)
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

attr_inline void rtc_periodic(void)
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

static void rtc_get(unsigned int *s, unsigned int *ms,
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

void time_rtc_stats(string_t *dst)
{
	unsigned int secs, msecs, raw1, raw2, base, wraps;

	rtc_get(&secs, &msecs, &raw1, &raw2, &base, &wraps);
	sms_to_date(dst, secs, msecs, raw1, raw2, base, wraps);
}

// timer

static unsigned int timer_s;
static unsigned int timer_ms;
static unsigned int timer_wraps;

static void timer_init(void)
{
	timer_s = 0;
	timer_ms = 0;
	timer_wraps = 0;
}

attr_inline void timer_periodic(void)
{
	timer_ms += 100;

	if(timer_ms > 999)
	{
		timer_ms = 0;

		if(++timer_s == 0)
			timer_wraps++;
	}
}

static void timer_get(unsigned int *s, unsigned int *ms,
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

void time_timer_stats(string_t *dst)
{
	unsigned int secs, msecs, raw1, raw2, base, wraps;

	timer_get(&secs, &msecs, &raw1, &raw2, &base, &wraps);
	sms_to_date(dst, secs, msecs, raw1, raw2, base, wraps);
}

// SNTP

static unsigned int	sntp_base_s = 0;
static unsigned int	sntp_current_ds = 0;
static ip_addr_t	sntp_server;
static int			sntp_timezone;

static void socket_sntp_callback_data_received(lwip_if_socket_t *socket, unsigned int length)
{
	sntp_network_t *received_packet = (sntp_network_t *)string_buffer_nonconst(&sntp_socket_receive_buffer);

	if(length == sizeof(sntp_network_t))
	{
		stat_sntp_received++;
		time_flags.sntp_regular_mode = 1;

		sntp_base_s =
			((received_packet->transmit_timestamp[0] << 24) |
			(received_packet->transmit_timestamp[1] << 16) |
			(received_packet->transmit_timestamp[2] << 8) |
			(received_packet->transmit_timestamp[3] << 0)) - 2208988800ULL;

		sntp_current_ds = 0;
	}

	string_clear(&sntp_socket_receive_buffer);
	lwip_if_receive_buffer_unlock(socket);
}

void time_sntp_start(void)
{
	string_new(, ip, 32);

	sntp_current_ds = 0;
	sntp_base_s = 0;
	time_flags.sntp_server_valid = 0;
	time_flags.sntp_regular_mode = 0;

	if(!time_flags.sntp_init_succeeded)
		return;

	if(config_get_string("sntp.server", &ip, -1, -1) && !string_match_cstr(&ip, "0.0.0.0"))
	{
		sntp_server = ip_addr(string_to_cstr(&ip));
		time_flags.sntp_server_valid = 1;
	}

	if(!config_get_int("sntp.tz", &sntp_timezone, -1, -1))
		sntp_timezone = 0;

	stat_sntp_poll = 0;
	stat_sntp_received = 0;
}

static void time_sntp_init(void)
{
	sntp_network_t *send_packet = (sntp_network_t *)string_buffer_nonconst(&sntp_socket_send_buffer);

	time_flags.sntp_init_succeeded = 0;

	if(lwip_if_socket_create(&sntp_socket, &sntp_socket_receive_buffer, &sntp_socket_send_buffer, 0, false, false, socket_sntp_callback_data_received))
	{
		send_packet->misc = sntp_misc_mode_client | (4 << sntp_misc_vn_shift);
		string_setlength(&sntp_socket_send_buffer, sizeof(sntp_network_t));
		time_flags.sntp_init_succeeded = 1;
	}
}

static void time_sntp_periodic(void)
{
	if(!time_flags.sntp_init_succeeded || !time_flags.sntp_server_valid)
		return;

	sntp_current_ds++;

	if(stat_sntp_poll == 0)
	{
		if(!lwip_if_sendto(&sntp_socket, &sntp_server, 123))
			log("sntp send failed\n");

		stat_sntp_poll = time_flags.sntp_regular_mode ? 6000 : 50;
	}
	else
		stat_sntp_poll--;
}

static void sntp_get(unsigned int *s, unsigned int *ms,
		unsigned int *raw1, unsigned int *raw2,
		unsigned int *base, unsigned int *wraps)
{
	if(!time_flags.sntp_server_valid)
	{
		if(s)
			*s = 0;

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
		if(s)
			*s = sntp_current_ds / 10;

		if(ms)
			*ms = (sntp_current_ds % 10) * 100;

		if(raw1)
			*raw1 = sntp_base_s + (sntp_current_ds / 10) + (sntp_timezone * 3600);

		if(raw2)
			*raw2 = 0;

		if(base)
			*base = sntp_base_s;

		if(wraps)
			*wraps = 0;
	}
}

void time_sntp_stats(string_t *dst)
{
	unsigned int secs, msecs, raw1, raw2, base, wraps;

	sntp_get(&secs, &msecs, &raw1, &raw2, &base, &wraps);
	sms_to_date(dst, secs, msecs, raw1, raw2, base, wraps);
}


// generic interface

static void sms_to_date(string_t *dst, int s, int ms, int r1, int r2, int b, int w)
{
	int d, h, m;

	d = s / (24 * 60 * 60);
	s -= d * 24 * 60 * 60;
	h = s / (60 * 60);
	s -= h * 60 * 60;
	m = s / 60;
	s -= m * 60;

	string_format(dst, "%2d %02d:%02d:%02d.%03d (r1=%d,r2=%d,b=%d,w=%d)", d, h, m, s, ms, r1, r2, b, w);
}

static unsigned int time_base_s;

void time_init(void)
{
	time_base_s = 0;

	uptime_init();
	system_init();
	rtc_init();
	timer_init();
	time_sntp_init();
}

void time_periodic(void)
{
	uptime_periodic();
	system_periodic();
	rtc_periodic();
	timer_periodic();
	time_sntp_periodic();
}

void time_set_stamp(unsigned int stamp)
{
	time_base_s = stamp;
	system_init();
	rtc_init();
	timer_init();
}

void time_set_hms(unsigned int h, unsigned int m, unsigned int s)
{
	time_set_stamp((h * 3600) + (m * 60) + s);
}

const char *time_get(unsigned int *h, unsigned int *m, unsigned int *s,
			unsigned int *Y, unsigned int *M, unsigned int *D)
{
	unsigned int time_s = 0;
	time_t ticks_s;
	const char *source;
	struct tm *tm;

	if(time_flags.sntp_init_succeeded && time_flags.sntp_server_valid && (sntp_base_s > 0)) // we have sntp sync
	{
		source = "sntp";
		sntp_get(0, 0, &time_s, 0, 0, 0);
	}
	else
	{
		source = "rtc";
		rtc_get(&time_s, 0, 0, 0, 0, 0);
		time_s = (unsigned int)(time_s + time_base_s);
	}

	ticks_s  = time_s;
	tm = localtime(&ticks_s);

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
