
#include "util.h"
#include "io.h"
#include "i2c_sensor.h"
#include "mailbox.h"
#include "dispatch.h"
#include "sdk.h"

#include <stdint.h>
#include <stdbool.h>

#define CONFIG_MAGIC "%4afc0002%"

typedef struct
{
	attr_flash_align uint32_t value;
	attr_flash_align const char *name;
} config_flag_name_t;

assert_size(config_flag_name_t, 8);

roflash static const config_flag_name_t config_flag_names[] =
{
	{	flag_strip_telnet,			"strip-telnet",				},
	{	flag_log_to_uart,			"log-to-uart",				},
	{	flag_log_date,				"log-date",					},
	{	flag_log_time,				"log-time",					},
	{	flag_cpu_high_speed,		"cpu-high-speed",			},
	{	flag_wlan_power_save,		"wlan-power-save",			},
	{	flag_display_clock,			"display-clock",			},
	{	flag_log_to_buffer,			"log-to-buffer",			},
	{	flag_auto_sequencer,		"auto-sequencer",			},
	{	flag_pwm1_extend,			"pwm1-extend",				},
	{	flag_terminate_output,		"terminate-output",			},
	{	flag_cmd_from_uart,			"cmd-from-uart",			},
	{	flag_log_to_display,		"log-to-display",			},
	{	flag_ssd_height_32,			"ssd-height-32",			},
	{	flag_none,					"",							},
};

unsigned int config_flags;

static unsigned int config_current_index;

static int config_tail(string_t *config_string, unsigned int size)
{
	int current, c[2];

	for(current = sizeof(CONFIG_MAGIC) - 1; (current + 1) < (int)size; current++)
	{
		c[0] = string_at(config_string, current + 0);
		c[1] = string_at(config_string, current + 1);

		if(c[0] == '\0')
		{
			log("config tail: config corrupt, clearing\n");
			current = sizeof(CONFIG_MAGIC) - 2;
			break;
		}

		if((c[0] == '\n') && (c[1] == '\n'))
			break;
	}

	current++;

	return(current);
}

bool config_init(void)
{
	config_flags = flag_log_to_uart | flag_log_to_buffer | flag_cmd_from_uart;

	if(!config_get_uint("flags", &config_flags, -1, -1))
		return(false);

	return(true);
}

static attr_nonnull bool config_open_read(string_t **config_string, char **config_buffer, unsigned int *size)
{
	flash_sector_buffer_use_t use;
	string_new(, magic_string, 16);

	stat_config_read_requests++;

	use = flash_buffer_using();

	flash_buffer_request(fsb_config_read, true, "config open read", config_string, config_buffer, size);

	if(!*config_string)
	{
		log("config open read: failed to request buffer\n");
		return(false);
	}

	if((use != fsb_config_cache) && (use != fsb_config_read))
	{
		if(spi_flash_read(USER_CONFIG_SECTOR * *size, *config_buffer, *size) != SPI_FLASH_RESULT_OK)
		{
			log("config open read: failed to read config sector 0x%x\n", (unsigned int)USER_CONFIG_SECTOR);
			flash_buffer_release(fsb_config_read, "config open read");
			return(false);
		}

		string_setlength(*config_string, *size);

		stat_config_read_loads++;
	}

	string_format(&magic_string, "%s\n", CONFIG_MAGIC);

	if(!string_nmatch_string(*config_string, &magic_string, string_length(&magic_string)))
	{
		log("config open read: magic mismatch\n");
		string_clear(*config_string);
		string_append_string(*config_string, &magic_string);
		string_append(*config_string, "\n"); // config sector should end at \n\n
	}

	config_current_index = string_length(&magic_string);

	return(true);
}

static bool config_close_read(void)
{
	if(!flash_buffer_using_1(fsb_config_read))
	{
		log("config_close_read: sector buffer in use: %u\n", flash_buffer_using());
		return(false);
	}

	flash_buffer_release(fsb_config_read, "config close read");
	flash_buffer_request(fsb_config_cache, false, "config close read", (string_t **)0, (char **)0, (unsigned int *)0);

	return(true);
}

bool config_open_write(void)
{
	string_t *config_string;
	char *config_buffer;
	unsigned int size;

	if(!config_open_read(&config_string, &config_buffer, &size))
		return(false);

	flash_buffer_release(fsb_config_read, "config open write");
	flash_buffer_request(fsb_config_write, true, "config open write", &config_string, &config_buffer, &size);

	stat_config_write_requests++;

	return(true);
}

bool config_close_write(void)
{
	int tail;
	string_t *config_string;
	char *config_buffer;
	unsigned int size;
	uint8_t sha_result1[SHA_DIGEST_LENGTH];
	uint8_t sha_result2[SHA_DIGEST_LENGTH];

	if(flash_buffer_using_1(fsb_config_write_dirty))
	{
		stat_config_write_saved++;

		flash_buffer_request(fsb_config_write_dirty, true, "config close write", &config_string, &config_buffer, &size);

		if(!config_string)
		{
			log("config close write: flash buffer request failed\n");
			flash_buffer_release(fsb_config_write_dirty, "config close write");
			return(false);
		}

		tail = config_tail(config_string, size);

		memset(config_buffer + tail + 1, '.', size - tail - 1);

		SHA1((const unsigned char *)config_buffer, SPI_FLASH_SEC_SIZE, sha_result1);

		if(spi_flash_erase_sector(USER_CONFIG_SECTOR) != SPI_FLASH_RESULT_OK)
		{
			log("config close write: write failed, erase failed\n");
			flash_buffer_release(fsb_config_write_dirty, "config close write");
			return(false);
		}

		if(spi_flash_write(USER_CONFIG_SECTOR * SPI_FLASH_SEC_SIZE, config_buffer, SPI_FLASH_SEC_SIZE) != SPI_FLASH_RESULT_OK)
		{
			log("config close write: write failed, write failed\n");
			flash_buffer_release(fsb_config_write_dirty, "config close write");
			return(false);
		}

		if(spi_flash_read(USER_CONFIG_SECTOR * size, config_buffer, size) != SPI_FLASH_RESULT_OK)
		{
			log("config close write: write failed, verify failed\n");
			flash_buffer_release(fsb_config_write_dirty, "config close write");
			return(false);
		}

		SHA1((const unsigned char *)config_buffer, SPI_FLASH_SEC_SIZE, sha_result2);

		if(memory_compare(SHA_DIGEST_LENGTH, sha_result1, sha_result2))
		{
			log("config close write: write failed, sha mismatch\n");
			flash_buffer_release(fsb_config_write_dirty, "config close write");
			return(false);
		}

		flash_buffer_release(fsb_config_write_dirty, "config close write");
		flash_buffer_request(fsb_config_write, true, "config close write", &config_string, &config_buffer, &size);

		if(!config_string)
		{
			log("config close write: write failed, buffer request failed\n");
			flash_buffer_release(fsb_config_write_dirty, "config close write");
			return(false);
		}
	}

	if(!flash_buffer_using_1(fsb_config_write))
	{
		log("config_close_write: sector buffer in use: %u\n", flash_buffer_using());
		return(false);
	}

	flash_buffer_release(fsb_config_write, "config close write");
	flash_buffer_request(fsb_config_cache, false, "config close write", &config_string, &config_buffer, &size);

	if(!config_string)
	{
		log("config close write: write failed, buffer request failed\n");
		flash_buffer_release(fsb_config_write, "config close write");
		return(false);
	}

	return(true);
}

void config_abort_write(void)
{
	string_t *config_string;

	stat_config_write_aborted++;

	if(flash_buffer_using_1(fsb_config_write))
	{
		flash_buffer_release(fsb_config_write, "config abort write");
		flash_buffer_request(fsb_config_cache, false, "config close write", &config_string, (char **)0, (unsigned int *)0);

		if(!config_string)
		{
			log("config abort write: write failed, buffer request failed\n");
			return;
		}
	}

	if(flash_buffer_using_1(fsb_config_write_dirty))
		flash_buffer_release(fsb_config_write_dirty, "config abort write");
}

static bool config_walk(string_t *id, string_t *value, const string_t *config_string, unsigned int size)
{
	int id_start_index, id_end_index, value_start_index, value_end_index;

	if((id_start_index = config_current_index) > (int)size)
	{
		log("config get entry: sector length overrun\n");
		return(false);
	}

	if(((value_start_index = string_sep(config_string, id_start_index, 1, '=')) > 0) &&
		((value_end_index = string_sep(config_string, value_start_index, 1, '\n')) > 0))
	{
		id_end_index = value_start_index - 1;

		string_splice(id, 0, config_string, id_start_index, id_end_index - id_start_index);
		string_splice(value, 0, config_string, value_start_index, value_end_index - value_start_index - 1);

		config_current_index = value_end_index;

		return(true);
	}

	return(false);
}

bool config_get_string_flashptr(const char *match_name_flash, string_t *return_value, int param1, int param2)
{
	string_new(, match_name, 64);
	string_new(, name, 64);
	string_new(, value, 64);
	string_t *config_string;
	char *config_buffer;
	unsigned int size;

	if(!config_open_read(&config_string, &config_buffer, &size))
		return(false);

	string_format_flash_ptr(&match_name, match_name_flash, param1, param2);

	while(config_walk(&name, &value, config_string, size))
	{
		if(string_match_string(&match_name, &name))
		{
			string_append_string(return_value, &value);
			config_close_read();
			return(true);
		}
	}

	config_close_read();
	return(false);
}

bool config_get_int_flashptr(const char *match_name_flash, int *return_value, int param1, int param2)
{
	string_new(, value, 16);

	if(!config_get_string_flashptr(match_name_flash, &value, param1, param2))
		return(false);

	return(parse_int(0, &value, return_value, 0, '\n') == parse_ok);
}

bool config_get_uint_flashptr(const char *match_name_flash, unsigned int *return_value, int param1, int param2)
{
	string_new(, value, 16);

	if(!config_get_string_flashptr(match_name_flash, &value, param1, param2))
		return(false);

	return(parse_uint(0, &value, return_value, 0, '\n') == parse_ok);
}

unsigned int config_delete_flashptr(const char *match_name_flash, bool wildcard, int param1, int param2)
{
	string_new(, name, 64);
	string_new(, match_name, 64);
	unsigned int deleted;
	int name_start_index, value_start_index, next_name_start_index;
	string_t *config_string;
	char *config_buffer;
	unsigned int size;
	flash_sector_buffer_use_t use;

	use = flash_buffer_using();

	if((use != fsb_config_write) && (use != fsb_config_write_dirty))
	{
		log("config delete: sector buffer in use: %u\n", flash_buffer_using());
		return(0);
	}

	if(use == fsb_config_write)
		flash_buffer_release(fsb_config_write, "config delete");
	else
		flash_buffer_release(fsb_config_write_dirty, "config delete");

	flash_buffer_request(fsb_config_write_dirty, true, "config delete", &config_string, &config_buffer, &size);

	if(!config_string)
	{
		log("config delete: cannot request buffer\n");
		return(0);
	}

	string_format_flash_ptr(&match_name, match_name_flash, param1, param2);

	deleted = 0;
	name_start_index = config_current_index;

	if(name_start_index > (int)size)
	{
		log("config delete: sector length overrun\n");
		return(0);
	}

	while(((value_start_index = string_sep(config_string, name_start_index, 1, '=')) > 0) &&
			((next_name_start_index = string_sep(config_string, value_start_index, 1, '\n')) > 0))
	{
		string_splice(&name, 0, config_string, name_start_index, value_start_index - name_start_index - 1);

		if((!wildcard && string_match_string(&match_name, &name)) ||
				(wildcard && string_nmatch_string(&match_name, &name, string_length(&match_name))))
		{
			memmove(&config_buffer[name_start_index], &config_buffer[next_name_start_index], size - next_name_start_index);
			deleted++;
		}
		else
			name_start_index = next_name_start_index;
	}

	if((deleted == 0) && (use != fsb_config_write_dirty))
	{
		flash_buffer_release(fsb_config_write_dirty, "config delete");
		flash_buffer_request(fsb_config_write, true, "config delete", &config_string, &config_buffer, &size);

		if(!config_string)
		{
			log("config delete: cannot request buffer\n");
			return(0);
		}
	}

	return(deleted);
}

bool config_set_string_flashptr(const char *match_name_flash, const char *value, int param1, int param2)
{
	int current;
	string_t *config_string;
	char *config_buffer;
	unsigned int size;

	if(!flash_buffer_using_2(fsb_config_write, fsb_config_write_dirty))
	{
		log("config set string: sector buffer in use: %u\n", flash_buffer_using());
		return(false);
	}

	config_delete_flashptr(match_name_flash, false, param1, param2);

	if(flash_buffer_using_1(fsb_config_write))
		flash_buffer_release(fsb_config_write, "config set string");
	else
		flash_buffer_release(fsb_config_write_dirty, "config set string");

	flash_buffer_request(fsb_config_write_dirty, true, "config set string", &config_string, &config_buffer, &size);

	if(!config_string)
	{
		log("config set string: cannot request buffer\n");
		return(false);
	}

	current = config_tail(config_string, size);

	string_setlength(config_string, current);
	string_format_flash_ptr(config_string, match_name_flash, param1, param2);
	string_format(config_string, "=%s\n\n", value);

	return(true);
}

bool config_set_int_flashptr(const char *match_name_flash, int value, int param1, int param2)
{
	string_new(, string_value, 16);

	string_format(&string_value, "%d", value);

	return(config_set_string_flashptr(match_name_flash, string_buffer(&string_value), param1, param2));
}

bool config_set_uint_flashptr(const char *match_name_flash, unsigned int value, int param1, int param2)
{
	string_new(, string_value, 16);

	string_format(&string_value, "%u", value);

	return(config_set_string_flashptr(match_name_flash, string_buffer(&string_value), param1, param2));
}

bool config_dump(string_t *dst)
{
	int amount;
	string_new(, name, 64);
	string_new(, value, 64);
	string_t *config_string;
	char *config_buffer;
	unsigned int size;

	if(!config_open_read(&config_string, &config_buffer, &size))
		return(app_action_error);

	amount = 0;

	while(config_walk(&name, &value, config_string, size))
	{
		string_format(dst, "%s=%s\n", string_to_cstr(&name), string_to_cstr(&value));
		amount++;
	}

	string_format(dst, "\ntotal config entries: %d, flags: %04x\n", amount, config_flags);

	return(config_close_read());
}

void config_flag_change_nosave(unsigned int flag, bool set)
{
	if(set)
		config_flags |= flag;
	else
		config_flags &= ~flag;
}

bool config_flag_change(unsigned int flag, bool set)
{
	config_flag_change_nosave(flag, set);

	if(config_open_write() &&
			config_set_uint("flags", config_flags, -1, -1) &&
			config_close_write())
		return(true);

	config_abort_write();
	return(false);
}

bool config_flag_change_from_string(const string_t *flag, bool set)
{
	const config_flag_name_t *entry;

	for(entry = config_flag_names; entry->value != flag_none; entry++)
		if(string_match_cstr(flag, entry->name))
			return(config_flag_change(entry->value, set));

	return(false);
}

void config_flags_to_string(bool nl, const char *prefix, string_t *dst)
{
	const config_flag_name_t *entry;

	for(entry = config_flag_names; entry->value != flag_none; entry++)
	{
		string_append_cstr(dst, prefix);
		string_append_cstr(dst, (config_flags & entry->value) ? "   " : "no ");
		string_append_cstr(dst, entry->name);
		string_append_cstr(dst, nl ? "\n" : " ");
	}
}
