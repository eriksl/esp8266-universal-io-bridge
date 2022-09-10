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

typedef struct attr_packed
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

typedef struct attr_packed
{
	unsigned int sntp_server_valid:1;
	unsigned int sntp_init_succeeded:1;
	unsigned int sntp_regular_mode:1;
} time_flags_t;

assert_size(time_flags_t, 1);

string_new(static, sntp_socket_receive_buffer, sizeof(sntp_network_t));
string_new(static, sntp_socket_send_buffer, sizeof(sntp_network_t));
static lwip_if_socket_t sntp_socket;

static time_flags_t time_flags;
static uint64_t time_base_s;
static int time_timezone;

static uint64_t		sntp_base_s = 0;
static uint64_t		sntp_current_ds = 0;
static ip_addr_t	sntp_server = { 0 };
static unsigned int sntp_sent;
static unsigned int sntp_received;
static unsigned int sntp_wait;

// uptime

static uint32_t uptime_last_us;
static uint32_t uptime_base_us;
static uint32_t uptime_wraps;

static void uptime_init(void)
{
	uptime_last_us = 0;
	uptime_wraps = 0;

	uptime_base_us = system_get_time();
}

attr_inline void uptime_periodic(void)
{
	uint32_t uptime_now = system_get_time();

	if(uptime_now < uptime_last_us)
		uptime_wraps++;

	uptime_last_us = uptime_now;
}

iram uint64_t time_get_us(void)
{
	return((((uint64_t)system_get_time()) | ((uint64_t)uptime_wraps << 32)) - uptime_base_us);
}

// system

static uint32_t system_last_us;
static uint32_t system_base_us;
static uint32_t system_wraps;

static void system_init(void)
{
	system_last_us = 0;
	system_wraps = 0;

	system_base_us = system_get_time();
}

attr_inline void system_periodic(void)
{
	uint32_t system_now;

	system_now =system_get_time();

	if(system_now < system_last_us)
		system_wraps++;

	system_last_us = system_now;
}

// rtc

static uint64_t rtc_current_ns;
static uint64_t rtc_base_s;
static uint64_t rtc_last_value;
static uint32_t rtc_wraps;

static void rtc_init(void)
{
	uint64_t rtc_current_value;
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
	uint64_t rtc_current_value, calvalue_ns, diff;

	rtc_current_value = system_get_rtc_time();

	if(rtc_current_value >= rtc_last_value)
		diff = rtc_current_value - rtc_last_value;
	else
	{
		rtc_wraps++;
		diff = ((uint64_t)1 << 32) - (rtc_last_value - rtc_current_value);
	}

	calvalue_ns = ((uint64_t)system_rtc_clock_cali_proc() * 1000) >> 12;
	diff *= calvalue_ns;

	rtc_current_ns += diff;

	rtc_last_value = rtc_current_value;
}

// timer

static uint64_t timer_s;
static uint32_t timer_ms;
static uint32_t timer_wraps;

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

static void socket_sntp_callback_data_received(lwip_if_socket_t *socket, const lwip_if_callback_context_t *context)
{
	sntp_network_t *received_packet = (sntp_network_t *)string_buffer_nonconst(&sntp_socket_receive_buffer);

	if(context->length == sizeof(sntp_network_t))
	{
		sntp_received++;
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

static void time_sntp_init(void)
{
	sntp_network_t *send_packet = (sntp_network_t *)string_buffer_nonconst(&sntp_socket_send_buffer);

	time_flags.sntp_init_succeeded = 0;

	if(lwip_if_socket_create(&sntp_socket, "sntp", &sntp_socket_receive_buffer, &sntp_socket_send_buffer, 0, false, socket_sntp_callback_data_received))
	{
		send_packet->misc = sntp_misc_mode_client | (4 << sntp_misc_vn_shift);
		string_setlength(&sntp_socket_send_buffer, sizeof(sntp_network_t));
		time_flags.sntp_init_succeeded = 1;
	}
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

	sntp_wait = 0;
	sntp_sent = 0;
	sntp_received = 0;
}

bool time_sntp_get_server(ip_addr_t *server)
{
	if(!time_flags.sntp_init_succeeded || !time_flags.sntp_server_valid)
		return(false);

	server->addr = sntp_server.addr;

	return(true);
}

bool time_sntp_set_server(string_t *errormsg, ip_addr_t server)
{
	string_new(, ip_string, 32);

	string_ip(&ip_string, server);

	if(!config_open_write())
	{
		if(errormsg)
			string_append(errormsg, "cannot set config (open, sntp server)\n");
		return(false);
	}

	config_delete("sntp.", true, -1, -1);

	if(server.addr == 0)
		time_flags.sntp_server_valid = 0;
	else
	{
		if(!config_set_string("sntp.server", string_to_cstr(&ip_string), -1, -1))
		{
			config_abort_write();
			if(errormsg)
				string_append(errormsg, "cannot set config (set, sntp server)\n");
			return(false);
		}

		time_flags.sntp_server_valid = 1;
	}

	if(!config_close_write())
	{
		if(errormsg)
			string_append(errormsg, "cannot set config (close, sntp server)\n");
		return(false);
	}

	if(time_flags.sntp_server_valid)
		sntp_server = server;

	return(true);
}

static void time_sntp_periodic(void)
{
	if(!time_flags.sntp_init_succeeded || !time_flags.sntp_server_valid)
		return;

	sntp_current_ds++;

	if(sntp_wait == 0)
	{
		if(!lwip_if_sendto(&sntp_socket, &sntp_server, 123))
			log("sntp send failed\n");

		sntp_sent++;
		sntp_wait = time_flags.sntp_regular_mode ? 6000 : 50;
	}
	else
		sntp_wait--;
}

// generic interface

static void stampms_to_dhm(uint64_t stamp, unsigned int ms, unsigned int *d, unsigned *h, unsigned int *m, unsigned int *s)
{
	*d =		stamp / (24 * 60 * 60);
	stamp -=	*d * 24 * 60 * 60;
	*h =		stamp / (60 * 60);
	stamp -=	*h * 60 * 60;
	*m =		stamp / 60;
	stamp -=	*m * 60;
	*s =		stamp;
}

void time_init(void)
{
	time_base_s = 0;

	if(!config_get_int("time.timezone", &time_timezone, -1, -1))
		time_timezone = 0;

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

void time_set_stamp(uint64_t stamp)
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

static void time_stamp_to_components(uint64_t stamp,
			unsigned int *h, unsigned int *m, unsigned int *s,
			unsigned int *Y, unsigned int *M, unsigned int *D)
{
	struct tm *tm;
	time_t ticks;

	ticks = stamp;
	tm = localtime(&ticks);

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
}

const char *time_get(unsigned int *h, unsigned int *m, unsigned int *s,
			unsigned int *Y, unsigned int *M, unsigned int *D)
{
	uint64_t stamp;
	const char *source;

	if(time_flags.sntp_init_succeeded && time_flags.sntp_server_valid && (sntp_base_s > 0)) // we have sntp sync
	{
		source = "sntp";
		stamp = sntp_base_s + (sntp_current_ds / 10) + (time_timezone * 3600);
	}
	else
	{
		source = "rtc";
		stamp = time_base_s + (rtc_current_ns / 1000000000);
	}

	time_stamp_to_components(stamp, h, m, s, Y, M, D);

	return(source);
}

bool time_set_timezone(int tz, string_t *errormsg)
{
	if(time_timezone == tz)
		return(true);

	if(!config_open_write())
	{
		if(errormsg)
			string_append(errormsg, "cannot set config (open, timezone)\n");
		return(false);
	}

	if(tz == 0)
		config_delete("time.", true, -1, -1);
	else
	{
		if(!config_set_int("time.timezone", tz, -1, -1))
		{
			config_abort_write();
			if(errormsg)
				string_append(errormsg, "cannot set config (set, timezone)\n");
			return(false);
		}
	}

	if(!config_close_write())
	{
		if(errormsg)
			string_append(errormsg, "cannot set config (close, timezone)\n");
		return(false);
	}

	time_timezone = tz;

	return(true);
}

int time_get_timezone(void)
{
	return(time_timezone);
}

void time_stats(string_t *dst)
{
	unsigned int Y, M, D, h, m, s;
	unsigned int msecs, base, wraps, lsw, msw;
	uint64_t stamp_us, secs, sntp_secs, rtc_secs;

	string_format(dst, "> %-6s %-3s %-2s:%-2s:%-2s.%-3s %-8s %-8s %-8s %-5s\n", "subsys", "day", "HH", "MM", "SS", "ms", "msw", "lsw", "base", "wraps");

	stamp_us =	time_get_us();
	secs =		stamp_us / 1000000;
	msecs =		(stamp_us % 1000000) / 1000;
	msw =		stamp_us >> 32;
	lsw =		stamp_us & 0xffffffff;
	base =		uptime_base_us;
	wraps =		uptime_wraps;
	stampms_to_dhm(secs, msecs, &D, &h, &m, &s);
	string_format(dst, "> %-6s %3u %02u:%02u:%02u.%03u %08x %08x %08x %5u\n", "uptime", D, h, m, s, msecs, msw, lsw, base, wraps);

	stamp_us =	(((uint64_t)system_get_time()) | ((uint64_t)system_wraps << 32)) - system_base_us;
	secs =		stamp_us / 1000000;
	msecs =		(stamp_us % 1000000) / 1000;
	msw =		stamp_us >> 32;
	lsw =		stamp_us & 0xffffffff;
	base =		system_base_us;
	wraps =		system_wraps;
	stampms_to_dhm(secs, msecs, &D, &h, &m, &s);
	string_format(dst, "> %-6s %3u %02u:%02u:%02u.%03u %08x %08x %08x %5u\n", "system", D, h, m, s, msecs, msw, lsw, base, wraps);

	secs =	timer_s;
	msecs =	timer_ms;
	wraps =	timer_wraps;
	stampms_to_dhm(secs, msecs, &D, &h, &m, &s);
	string_format(dst, "> %-6s %3u %02u:%02u:%02u.%03u %08x %08x %08x %5u\n", "timer", D, h, m, s, msecs, 0U, 0U, 0U, wraps);

	secs =	(rtc_current_ns / 1000000000);
	msecs =	(rtc_current_ns % 1000000000) / 1000000;
	msw =	rtc_current_ns >> 32;
	lsw =	rtc_current_ns & 0xffffffff;
	base =	rtc_base_s;
	wraps =	rtc_wraps;
	rtc_secs = secs;
	stampms_to_dhm(secs, msecs, &D, &h, &m, &s);
	string_format(dst, "> %-6s %3u %02u:%02u:%02u.%03u %08x %08x %08x %5u\n", "rtc", D, h, m, s, msecs, msw, lsw, base, wraps);

	secs =	sntp_current_ds / 10 + (time_timezone * 3600);
	msecs =	(sntp_current_ds % 10) * 100;
	base =	sntp_base_s;
	sntp_secs = secs;
	stampms_to_dhm(secs, msecs, &D, &h, &m, &s);
	string_format(dst, "> %-6s %3u %02u:%02u:%02u.%03u %08x %08x %08x %5u\n", "sntp", D, h, m, s, msecs, 0U, 0U, base, wraps);

	string_format(dst, "> sntp: server set: %s, socket create succeeded: %s, mode: %s, next update in %u seconds, sent: %u, received: %u\n",
		yesno(time_flags.sntp_server_valid),
		yesno(time_flags.sntp_init_succeeded),
		time_flags.sntp_regular_mode ? "normal/background" : "initial/burst",
		sntp_wait, sntp_sent, sntp_received);

	time_stamp_to_components(time_base_s, &h, &m, &s, &Y, &M, &D);
	string_format(dst, "> time base: %04u/%02u/%02u %02u:%02u:%02u\n", Y, M, D, h, m, s);
	time_stamp_to_components(rtc_secs + time_base_s, &h, &m, &s, &Y, &M, &D);
	string_format(dst, "> rtc time:  %04u/%02u/%02u %02u:%02u:%02u\n", Y, M, D, h, m, s);
	if(time_flags.sntp_init_succeeded && time_flags.sntp_server_valid && (sntp_base_s > 0))
	{
		time_stamp_to_components(sntp_secs + sntp_base_s, &h, &m, &s, &Y, &M, &D);
		string_format(dst, "> sntp time: %04u/%02u/%02u %02u:%02u:%02u\n", Y, M, D, h, m, s);
		string_append(dst, "> source: sntp\n");
	}
	else
		string_append(dst, "> source: rtc\n");

	string_format(dst, "> time zone: %d\n", time_get_timezone());
}
