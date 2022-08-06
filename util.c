#include "util.h"
#include "sys_time.h"
#include "uart.h"
#include "mailbox.h"
#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>
#include <stdarg.h>

static flash_sector_buffer_use_t flash_sector_buffer_use = fsb_free;
static bool flash_sector_buffer_private = false;
static string_new(attr_flash_align, flash_sector_buffer, SPI_FLASH_SEC_SIZE);

roflash const unsigned int crc16_tab[256] =
{
	0x0000, 0xc0c1, 0xc181, 0x0140, 0xc301, 0x03c0, 0x0280, 0xc241,
	0xc601, 0x06c0, 0x0780, 0xc741, 0x0500, 0xc5c1, 0xc481, 0x0440,
	0xcc01, 0x0cc0, 0x0d80, 0xcd41, 0x0f00, 0xcfc1, 0xce81, 0x0e40,
	0x0a00, 0xcac1, 0xcb81, 0x0b40, 0xc901, 0x09c0, 0x0880, 0xc841,
	0xd801, 0x18c0, 0x1980, 0xd941, 0x1b00, 0xdbc1, 0xda81, 0x1a40,
	0x1e00, 0xdec1, 0xdf81, 0x1f40, 0xdd01, 0x1dc0, 0x1c80, 0xdc41,
	0x1400, 0xd4c1, 0xd581, 0x1540, 0xd701, 0x17c0, 0x1680, 0xd641,
	0xd201, 0x12c0, 0x1380, 0xd341, 0x1100, 0xd1c1, 0xd081, 0x1040,
	0xf001, 0x30c0, 0x3180, 0xf141, 0x3300, 0xf3c1, 0xf281, 0x3240,
	0x3600, 0xf6c1, 0xf781, 0x3740, 0xf501, 0x35c0, 0x3480, 0xf441,
	0x3c00, 0xfcc1, 0xfd81, 0x3d40, 0xff01, 0x3fc0, 0x3e80, 0xfe41,
	0xfa01, 0x3ac0, 0x3b80, 0xfb41, 0x3900, 0xf9c1, 0xf881, 0x3840,
	0x2800, 0xe8c1, 0xe981, 0x2940, 0xeb01, 0x2bc0, 0x2a80, 0xea41,
	0xee01, 0x2ec0, 0x2f80, 0xef41, 0x2d00, 0xedc1, 0xec81, 0x2c40,
	0xe401, 0x24c0, 0x2580, 0xe541, 0x2700, 0xe7c1, 0xe681, 0x2640,
	0x2200, 0xe2c1, 0xe381, 0x2340, 0xe101, 0x21c0, 0x2080, 0xe041,
	0xa001, 0x60c0, 0x6180, 0xa141, 0x6300, 0xa3c1, 0xa281, 0x6240,
	0x6600, 0xa6c1, 0xa781, 0x6740, 0xa501, 0x65c0, 0x6480, 0xa441,
	0x6c00, 0xacc1, 0xad81, 0x6d40, 0xaf01, 0x6fc0, 0x6e80, 0xae41,
	0xaa01, 0x6ac0, 0x6b80, 0xab41, 0x6900, 0xa9c1, 0xa881, 0x6840,
	0x7800, 0xb8c1, 0xb981, 0x7940, 0xbb01, 0x7bc0, 0x7a80, 0xba41,
	0xbe01, 0x7ec0, 0x7f80, 0xbf41, 0x7d00, 0xbdc1, 0xbc81, 0x7c40,
	0xb401, 0x74c0, 0x7580, 0xb541, 0x7700, 0xb7c1, 0xb681, 0x7640,
	0x7200, 0xb2c1, 0xb381, 0x7340, 0xb101, 0x71c0, 0x7080, 0xb041,
	0x5000, 0x90c1, 0x9181, 0x5140, 0x9301, 0x53c0, 0x5280, 0x9241,
	0x9601, 0x56c0, 0x5780, 0x9741, 0x5500, 0x95c1, 0x9481, 0x5440,
	0x9c01, 0x5cc0, 0x5d80, 0x9d41, 0x5f00, 0x9fc1, 0x9e81, 0x5e40,
	0x5a00, 0x9ac1, 0x9b81, 0x5b40, 0x9901, 0x59c0, 0x5880, 0x9841,
	0x8801, 0x48c0, 0x4980, 0x8941, 0x4b00, 0x8bc1, 0x8a81, 0x4a40,
	0x4e00, 0x8ec1, 0x8f81, 0x4f40, 0x8d01, 0x4dc0, 0x4c80, 0x8c41,
	0x4400, 0x84c1, 0x8581, 0x4540, 0x8701, 0x47c0, 0x4680, 0x8641,
	0x8201, 0x42c0, 0x4380, 0x8341, 0x4100, 0x81c1, 0x8081, 0x4040,
};

unsigned int crc16(unsigned int length, const uint8_t *data)
{
	uint16_t crc;
	unsigned int current;
	unsigned int index;

	for(crc = 0, current = 0; current < length; current++)
	{
		index = (crc ^ data[current]) & 0x00ff;
		crc = (crc >> 8) ^ crc16_tab[index];
	}

	return(crc);
}

char flash_dram_buffer[1024];

string_t flash_dram =
{
	.size = sizeof(flash_dram_buffer),
	.length = 0,
	.buffer = flash_dram_buffer,
};

iram int strecpy(char *dst, const char *src, int size)
{
	int length = strlen(src);

	if(length >= size)
		length = size - 1;

	if(length < 0)
		length = 0;

	memcpy(dst, src, length);
	dst[length] = '\0';

	return(length);
}

iram unsigned int flash_to_dram(bool cstr, const void *src_flash_unaligned, char *dst_dram, size_t length)
{
	const uint32_t *src_flash;
	unsigned int src_flash_index;
	unsigned int src_flash_sub_index;
	unsigned int dst_dram_index;

	src_flash = (const uint32_t *)((uint32_t)src_flash_unaligned & ~0b11);
	src_flash_sub_index = (uint32_t)src_flash_unaligned & 0b11;

	for(src_flash_index = 0, dst_dram_index = 0; dst_dram_index < length; dst_dram_index++)
	{
		dst_dram[dst_dram_index] = (src_flash[src_flash_index] >> (src_flash_sub_index << 3)) & 0xff;

		if(cstr)
		{
			if(!dst_dram[dst_dram_index])
				break;

			if((dst_dram_index + 1) >= length)
			{
				dst_dram[dst_dram_index] = 0;
				break;
			}
		}

		if((++src_flash_sub_index & 0b11) == 0)
		{
			src_flash_sub_index = 0;
			src_flash_index++;
		}
	}

	return(dst_dram_index);
}

void string_format_cstr(string_t *dst, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	dst->length += vsnprintf(dst->buffer + dst->length, dst->size - dst->length - 1, fmt, ap);
	va_end(ap);

	if(dst->length > (dst->size - 1))
		dst->length = dst->size - 1;

	dst->buffer[dst->length] = '\0';
}

iram void string_format_flash_ptr(string_t *dst, const char *fmt_flash, ...)
{
	va_list ap;
	int rendered_length, buffer_remaining;

	flash_to_dram(true, fmt_flash, flash_dram_buffer, sizeof(flash_dram_buffer));

	// no space left at all, do nothing

	if((buffer_remaining = dst->size - dst->length - 1) <= 0)
		return;

	va_start(ap, fmt_flash);
	rendered_length = vsnprintf(dst->buffer + dst->length, buffer_remaining, flash_dram_buffer, ap);
	va_end(ap);

	// some snprintf implementations can return -1 when output doesn't fit the output buffer
	// some snprintf implementations can return the original buffer size when the output doesn't fit the output buffer
	// this means we can't always rely on the return value, assume vsnprintf filled all of the buffer_remaining space in those cases

	if((rendered_length < 0) || (rendered_length > buffer_remaining))
		rendered_length = buffer_remaining;

	dst->length += rendered_length;

	dst->buffer[dst->length] = '\0';
}

bool attr_nonnull string_match_cstr_flash(const string_t *s1, const char *s2)
{
	char s2_in_dram[64];

	flash_to_dram(true, s2, s2_in_dram, sizeof(s2_in_dram));

	return(string_match_cstr(s1, s2_in_dram));
}

int attr_pure string_sep(const string_t *src, int offset, int occurrence, char c)
{
	for(; (offset < src->size) && (offset < src->length) && (occurrence > 0); offset++)
		if(string_at(src, offset) == c)
			occurrence--;

	if((offset >= src->size) || (offset >= src->length))
		offset = -1;

	return(offset);
}

int attr_pure string_find(const string_t *src, int offset, char c)
{
	for(; offset < src->length; offset++)
		if(string_at(src, offset) == c)
			return(offset);

	return(-1);
}

void string_replace(string_t *dst, int offset, char c)
{
	if((offset + 1) < dst->size)
	{
		dst->buffer[offset] = c;

		if(offset > dst->length)
		{
			dst->length = offset;
			dst->buffer[dst->length + 1] = '\0';
		}
	}
}

void string_splice(string_t *dst, int dst_offset, const string_t *src, int src_offset, int length)
{
	if(dst_offset < 0)
		dst_offset = dst->length;

	if(src_offset < 0)
		src_offset = 0;

	if(length < 0)
		length = src->length - src_offset;

	if((src_offset + length) > src->length)
		length = src->length - src_offset;

	if((dst_offset + length) > dst->size)
		length = dst->size - dst_offset;

	memcpy(dst->buffer + dst_offset, src->buffer + src_offset, length);

	string_setlength(dst, dst_offset + length);
}

int string_trim(string_t *dst, int amount)
{
	if(amount < 0)
		amount = dst->length;

	if(amount > dst->size)
		amount = dst->size;

	if(amount > dst->length)
		amount = dst->length;

	dst->length -= amount;

	return(dst->length);
}

bool string_trim_nl(string_t *dst)
{
	bool trimmed = false;

	if((dst->length > 0) && (dst->buffer[dst->length - 1] == '\n'))
	{
		trimmed = true;

		dst->length--;

		if((dst->length > 0) && (dst->buffer[dst->length - 1] == '\r'))
			dst->length--;
	}
	else
	{
		if((dst->length > 0) && (dst->buffer[dst->length - 1] == '\r'))
		{
			trimmed = true;

			dst->length--;

			if((dst->length > 0) && (dst->buffer[dst->length - 1] == '\n'))
				dst->length--;
		}
	}

	return(trimmed);
}

void string_bin_to_hex(string_t *dst, const unsigned char *src, int length)
{
	int offset;
	uint8_t out;

	for(offset = 0; offset < length; offset++)
	{
		out = (src[offset] & 0xf0) >> 4;

		if(out > 9)
			out = (out - 10) + 'a';
		else
			out = out + '0';

		string_append_char(dst, out);

		out = (src[offset] & 0x0f) >> 0;

		if(out > 9)
			out = (out - 10) + 'a';
		else
			out = out + '0';

		string_append_char(dst, out);
	}
}

void string_ip(string_t *dst, ip_addr_t addr)
{
	ip_addr_to_bytes_t ip_addr_to_bytes;
	ip_addr_to_bytes.ip_addr = addr;

	string_format(dst, "%u.%u.%u.%u",
		ip_addr_to_bytes.byte[0],
		ip_addr_to_bytes.byte[1],
		ip_addr_to_bytes.byte[2],
		ip_addr_to_bytes.byte[3]);
}

bool string_to_mac(mac_addr_t *addr, const string_t *src)
{
	unsigned int ix, octet[6];

	if(sscanf(string_buffer(src), "%x:%x:%x:%x:%x:%x ",
			&octet[0], &octet[1], &octet[2],
			&octet[3], &octet[4], &octet[5]) != 6)
		return(false);

	for(ix = 0; ix < 6; ix++)
		(*addr)[ix] = octet[ix];

	return(true);
}

void string_mac(string_t *dst, mac_addr_t addr)
{
	int ix;
	mac_addr_to_bytes_t mac_addr_to_bytes;

	for(ix = 0; ix < 6; ix++)
		mac_addr_to_bytes.mac_addr[ix] = addr[ix];

	string_format(dst, "%02x:%02x:%02x:%02x:%02x:%02x",
		mac_addr_to_bytes.byte[0],
		mac_addr_to_bytes.byte[1],
		mac_addr_to_bytes.byte[2],
		mac_addr_to_bytes.byte[3],
		mac_addr_to_bytes.byte[4],
		mac_addr_to_bytes.byte[5]);
}

void _flash_buffer_request(flash_sector_buffer_use_t use, bool pvt, const char *description,
		string_t **string, char **cstr, unsigned int *size)
{
	if(string_size(&flash_sector_buffer) != SPI_FLASH_SEC_SIZE)
	{
		log("request flash buffer: ");
		log_from_flash_0(description);
		log(": sector buffer size wrong: %d: ", string_size(&flash_sector_buffer));
		goto error;
	}

	if((flash_sector_buffer_use != fsb_free) && ((flash_sector_buffer_use != use) && flash_sector_buffer_private))
	{
		log("request flash buffer: ");
		log_from_flash_0(description);
		log(": in use by %u: ", flash_sector_buffer_use);
		goto error;
	}

	flash_sector_buffer_use = use;
	flash_sector_buffer_private = pvt;

	if(string)
		*string = &flash_sector_buffer;

	if(cstr)
		*cstr = string_buffer_nonconst(&flash_sector_buffer);

	if(size)
		*size = string_size(&flash_sector_buffer);

	return;

error:
	if(string)
		*string = (string_t *)0;

	if(cstr)
		*cstr = (char *)0;

	if(size)
		*size = 0;

	return;
}

void _flash_buffer_release(flash_sector_buffer_use_t use, const char *description)
{
	if(flash_sector_buffer_use == fsb_free)
	{
		log("release flash buffer: double free: ");
		log_from_flash_0(description);
		log("\n");
	}

	if((flash_sector_buffer_use != use) && flash_sector_buffer_private)
	{
		log("release flash buffer: conflicting free: from %u to %u: ", flash_sector_buffer_use, use);
		log_from_flash_0(description);
		log("\n");
	}

	flash_sector_buffer_use = fsb_free;
	flash_sector_buffer_private = false;
}

flash_sector_buffer_use_t flash_buffer_using(void)
{
	return(flash_sector_buffer_use);
}

bool flash_buffer_using_1(flash_sector_buffer_use_t one)
{
	if(flash_sector_buffer_use == one)
		return(true);

	return(false);
}

bool flash_buffer_using_2(flash_sector_buffer_use_t one, flash_sector_buffer_use_t two)
{
	if(flash_sector_buffer_use == one)
		return(true);

	if(flash_sector_buffer_use == two)
		return(true);

	return(false);
}

bool flash_buffer_using_3(flash_sector_buffer_use_t one, flash_sector_buffer_use_t two, flash_sector_buffer_use_t three)
{
	if(flash_sector_buffer_use == one)
		return(true);

	if(flash_sector_buffer_use == two)
		return(true);

	if(flash_sector_buffer_use == three)
		return(true);

	return(false);
}

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

const void *flash_cache_pointer(uint32_t offset)
{
	static void * const flash_window_start = (void *)0x40200000;

	return((void *)((uint8_t *)flash_window_start + offset));
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
		wifi_set_listen_interval(3);
		wifi_set_sleep_level(MAX_SLEEP_T);
		wifi_set_sleep_type(MODEM_SLEEP_T);
	}
	else
		wifi_set_sleep_type(NONE_SLEEP_T);
}

unsigned int utf8_to_unicode(const char *src, unsigned int dst_size, unsigned int *dst)
{
	enum
	{
		u8p_state_base = 0,
		u8p_state_utf8_byte_3 = 1,
		u8p_state_utf8_byte_2 = 2,
		u8p_state_utf8_byte_1 = 3,
		u8p_state_done = 4
	} state;

	unsigned int src_current;
	unsigned int src_index;
	unsigned int dst_index;
	unsigned int unicode;

	for(src_index = 0, dst_index = 0, state = u8p_state_base, unicode = 0; src && dst && (dst_index < dst_size) && src[src_index]; src_index++)
	{
		src_current = (unsigned int)src[src_index];

		switch(state)
		{
			case u8p_state_base:
			{
				if((src_current & 0xe0) == 0xc0) // first of two bytes (11 bits)
				{
					unicode = src_current & 0x1f;
					state = u8p_state_utf8_byte_1;
					continue;
				}
				else
					if((src_current & 0xf0) == 0xe0) // first of three bytes (16 bits)
					{
						unicode = src_current & 0x0f;
						state = u8p_state_utf8_byte_2;
						continue;
					}
					else
						if((src_current & 0xf8) == 0xf0) // first of four bytes (21 bits)
						{
							unicode = src_current & 0x07;
							state = u8p_state_utf8_byte_3;
							continue;
						}
						else
							if((src_current & 0x80) == 0x80)
							{
								log("utf8 parser: invalid utf8, bit 7 set: %x %c\n", src_current, (int)src_current);
								unicode = '*';
							}
							else
								unicode = src_current & 0x7f;

				break;
			}

			case u8p_state_utf8_byte_3 ... u8p_state_utf8_byte_1:
			{
				if((src_current & 0xc0) == 0x80) // following bytes
				{
					unicode = (unicode << 6) | (src_current & 0x3f);

					if(++state != u8p_state_done)
						continue;
				}
				else
				{
					log("utf8 parser: invalid utf8, no prefix on following byte, state: %u: %x %c\n", state, src_current, (int)src_current);
					unicode = '*';
				}

				break;
			}

			default:
			{
				log("utf8 parser: invalid state %u\n", state);
				unicode = '*';
			}
		}

		dst[dst_index++] = unicode;
		unicode = 0;
		state = u8p_state_base;
	}

	return(dst_index);
}

unsigned char *MD5(const unsigned char *src, unsigned int length, uint8_t digest[MD5_DIGEST_LENGTH])
{
	MD5_CTX context;

	MD5Init(&context);
	MD5Update(&context, src, length);
	MD5Final(digest, &context);
	return(digest);
}

uint32_t MD5_trunc_32(const unsigned char *src, unsigned int length)
{
	uint32_t checksum;
	uint8_t digest[MD5_DIGEST_LENGTH];

	MD5(src, length, digest);

	checksum = (digest[0] << 24) | (digest[1] << 16) | (digest[2] << 8) | (digest[3] << 0);

	return(checksum);
}

void MD5_text(const unsigned char *src, unsigned int length, string_t *digest_text)
{
	unsigned char digest[MD5_DIGEST_LENGTH];

	MD5(src, length, digest);
	string_bin_to_hex(digest_text, digest, MD5_DIGEST_LENGTH);
}

unsigned char *SHA1(const unsigned char *src, unsigned int length, uint8_t digest[SHA_DIGEST_LENGTH])
{
	SHA_CTX context;

	SHA1Init(&context);
	SHA1Update(&context, src, length);
	SHA1Final(digest, &context);
	return(digest);
}

void SHA1_text(const unsigned char *src, unsigned int length, string_t *digest_text)
{
	unsigned char digest[SHA_DIGEST_LENGTH];

	SHA1(src, length, digest);
	string_bin_to_hex(digest_text, digest, SHA_DIGEST_LENGTH);
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
