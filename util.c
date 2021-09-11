#include "util.h"
#include "sys_string.h"
#include "sys_time.h"
#include "uart.h"
#include "mailbox.h"
#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

unsigned int logbuffer_display_current = 0;
static bool newline_logged = true;

string_t logbuffer =
{
	.size = 0x3fffeb2c - 0x3fffe000 - 16,
	.length = 0,
	.buffer = (char *)0x3fffe000,
};

int attr_used __errno;

void espconn_init(void);
void espconn_init(void)
{
}

void reset(void)
{
	system_restart();
}

attr_const const char *yesno(bool value)
{
	if(!value)
		return("no");

	return("yes");
}

attr_const const char *onoff(bool value)
{
	if(!value)
		return("off");

	return("on");
}

void logbuffer_clear(void)
{
	logbuffer_display_current = 0;
	string_clear(&logbuffer);
}

static void log_date_time(void)
{
	unsigned int month, day, hour, minute;

	if(config_flags_match(flag_log_date | flag_log_time))
	{
		time_get(&hour, &minute, (unsigned int *)0, (unsigned int *)0, &month, &day);

		if(config_flags_match(flag_log_date))
			string_format(&logbuffer, "%02u/%02u ", month, day);

		if(config_flags_match(flag_log_time))
			string_format(&logbuffer, "%02u:%02u ", hour, minute);
	}
}

static void log_finish(const string_t *from, string_t *to)
{
	if(config_flags_match(flag_log_to_uart))
		uart_send_string(0, from);

	if(config_flags_match(flag_log_to_buffer))
	{
		if((string_length(to) + string_length(from)) >= string_size(to))
			logbuffer_clear();

		string_append_string(to, from);
	}

	newline_logged = string_at(from, string_length(from) - 1);
}

void log_from_flash_0(const char *data_in_flash)
{
	int length;

	if(config_flags_match(flag_log_to_buffer) && newline_logged)
		log_date_time();

	length = flash_to_dram(true, data_in_flash, string_buffer_nonconst(&flash_dram), string_size(&flash_dram));
	string_setlength(&flash_dram, length);

	log_finish(&flash_dram, &logbuffer);
}

void log_from_flash_n(const char *fmt_in_flash, ...)
{
	int length;
	va_list ap;
	char fmt_in_dram[128];

	if(config_flags_match(flag_log_to_buffer) && newline_logged)
		log_date_time();

	flash_to_dram(true, fmt_in_flash, fmt_in_dram, sizeof(fmt_in_dram));

	va_start(ap, fmt_in_flash);
	length = vsnprintf(string_buffer_nonconst(&flash_dram), string_size(&flash_dram), fmt_in_dram, ap);
	va_end(ap);

	if(length < 0)
		return;

	string_setlength(&flash_dram, length);

	log_finish(&flash_dram, &logbuffer);
}

iram void logchar(char c)
{
	if(config_flags_match(flag_log_to_uart))
	{
		uart_send(0, c);
		uart_flush(0);
	}

	if(config_flags_match(flag_log_to_buffer))
	{
		if(newline_logged)
			log_date_time();

		if((logbuffer.length + 1) >= logbuffer.size)
			logbuffer_clear();

		string_append_char(&logbuffer, c);

		newline_logged = (c == '\n');
	}
}

void msleep(int msec)
{
	while(msec-- > 0)
	{
		system_soft_wdt_feed();
		os_delay_us(1000);
	}
}

attr_pure ip_addr_t ip_addr(const char *src)
{
	ip_addr_to_bytes_t ip_addr_to_bytes;
	unsigned int byte_index, current_value;

	if(!src)
		goto error;

	for(byte_index = 0, current_value = 0; *src && (byte_index < 4); src++)
	{
		if(*src == '.')
		{
			ip_addr_to_bytes.byte[byte_index++] = current_value;
			current_value = 0;
			continue;
		}

		if((*src >= '0') && (*src <= '9'))
		{
			current_value *= 10;
			current_value += (uint8_t)*src - '0';
			continue;
		}

		goto error;
	}

	if(byte_index != 3)
		goto error;

	ip_addr_to_bytes.byte[byte_index] = current_value;

	return(ip_addr_to_bytes.ip_addr);

error:
	ip_addr_to_bytes.byte[0] = 0;
	ip_addr_to_bytes.byte[1] = 0;
	ip_addr_to_bytes.byte[2] = 0;
	ip_addr_to_bytes.byte[3] = 0;

	return(ip_addr_to_bytes.ip_addr);
}

void power_save_enable(bool enable)
{
	if(enable)
	{
		if(config_flags_match(flag_wlan_power_save))
		{
			wifi_set_listen_interval(2);
			wifi_set_sleep_level(MAX_SLEEP_T);
			wifi_set_sleep_type(MODEM_SLEEP_T);
		}
		else
			wifi_set_sleep_type(NONE_SLEEP_T);
	}

	if(!enable)
		wifi_set_sleep_type(NONE_SLEEP_T);
}

// missing from libc

void *_malloc_r(struct _reent *r, size_t sz)
{
	return(pvPortMalloc(sz, "", 0, false));
}

void *_calloc_r(struct _reent *r, size_t a, size_t b)
{
	return(pvPortCalloc(a, b, "", 0));
}

void _free_r(struct _reent *r, void *x)
{
	return(vPortFree(x, "", 0));
}

void *_realloc_r(struct _reent *r, void *x, size_t sz)
{
	return(pvPortRealloc(x, sz, "", 0));
}
