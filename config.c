#include "config.h"

#include "util.h"
#include "sys_string.h"
#include "io.h"
#include "i2c_sensor.h"
#include "ota.h"
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
	{	flag_strip_telnet,		"strip-telnet"		},
	{	flag_log_to_uart,		"log-to-uart"		},
	{	flag_tsl_high_sens,		"tsl-high-sens"		},
	{	flag_bh_high_sens,		"bh-high-sens"		},
	{	flag_cpu_high_speed,	"cpu-high-speed"	},
	{	flag_wlan_power_save,	"wlan-power-save"	},
	{	flag_uart0_tx_inv,		"uart0-tx-inv",		},
	{	flag_uart0_rx_inv,		"uart0-rx-inv",		},
	{	flag_log_to_buffer,		"log-to-buffer"		},
	{	flag_auto_sequencer,	"auto-sequencer"	},
	{	flag_pwm1_extend,		"pwm1-extend"		},
	{	flag_tmd_high_sens,		"tmd-high-sens"		},
	{	flag_apds3_high_sens,	"apds3-high-sens"	},
	{	flag_apds6_high_sens,	"apds6-high-sens"	},
	{	flag_uart1_tx_inv,		"uart1-tx-inv",		},
	{	flag_udp_term_empty,	"udp-term-empty",	},
	{	flag_enable_orbital,	"enable-orbital",	},
	{	flag_cmd_from_uart,		"cmd-from-uart",	},
	{	flag_log_to_display,	"log-to-display",	},
	{	flag_none,				""					},
};

unsigned int config_flags;

static unsigned int config_current_index;

static int config_tail(void)
{
	int current;

	for(current = sizeof(CONFIG_MAGIC) + 1; (current + 1) < string_size(&flash_sector_buffer); current++)
		if((string_at(&flash_sector_buffer, current) == '\n') &&
				(string_at(&flash_sector_buffer, current + 1) == '\n'))
			break;

	if((current + 1) >= string_size(&flash_sector_buffer))
		current = sizeof(CONFIG_MAGIC) + 1;
	else
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

bool config_open_read(void)
{
	string_new(, magic_string, 16);

	stat_config_read_requests++;

	if(flash_sector_buffer_use != fsb_config_cache)
	{
		if(flash_sector_buffer_use != fsb_free)
		{
			log("config_open_read: sector buffer in use: %u\n", flash_sector_buffer_use);
			return(false);
		}

		if(string_size(&flash_sector_buffer) < SPI_FLASH_SEC_SIZE)
		{
			log("config_open_read: sector buffer too small: %u\n", flash_sector_buffer_use);
			return(false);
		}

		flash_sector_buffer_use = fsb_config_read;

		if(spi_flash_read(USER_CONFIG_SECTOR * SPI_FLASH_SEC_SIZE, string_buffer_nonconst(&flash_sector_buffer), SPI_FLASH_SEC_SIZE) != SPI_FLASH_RESULT_OK)
		{
			log("config_open_read: failed to read config sector 0x%x\n", (unsigned int)USER_CONFIG_SECTOR);
			flash_sector_buffer_use = fsb_free;
			return(false);
		}

		string_setlength(&flash_sector_buffer, SPI_FLASH_SEC_SIZE);

		stat_config_read_loads++;
	}

	string_format(&magic_string, "%s\n", CONFIG_MAGIC);

	if(!string_nmatch_string(&flash_sector_buffer, &magic_string, string_length(&magic_string)))
	{
		log("config_open_read: magic mismatch\n");
		string_clear(&flash_sector_buffer);
		string_append_string(&flash_sector_buffer, &magic_string);
		string_append(&flash_sector_buffer, "\n");
	}

	config_current_index = string_length(&magic_string);
	flash_sector_buffer_use = fsb_config_read;

	return(true);
}

bool config_close_read(void)
{
	if(flash_sector_buffer_use != fsb_config_read)
	{
		log("config_close_read: sector buffer in use: %u\n", flash_sector_buffer_use);
		return(false);
	}

	flash_sector_buffer_use = fsb_config_cache;

	return(true);
}

bool config_open_write(void)
{
	if(!config_open_read())
		return(false);

	if(flash_sector_buffer_use != fsb_config_read)
		return(false);

	stat_config_write_requests++;
	flash_sector_buffer_use = fsb_config_write;

	return(true);
}

bool config_close_write(void)
{
	int tail;

	SHA_CTX sha_context;
	uint8_t sha_result1[SHA_DIGEST_LENGTH];
	uint8_t sha_result2[SHA_DIGEST_LENGTH];

	if(flash_sector_buffer_use == fsb_config_write_dirty)
	{
		stat_config_write_saved++;

		tail = config_tail();

		memset(string_buffer_nonconst(&flash_sector_buffer) + tail + 1, '.', string_size(&flash_sector_buffer) - tail - 1);

		SHA1Init(&sha_context);
		SHA1Update(&sha_context, string_buffer(&flash_sector_buffer), SPI_FLASH_SEC_SIZE);
		SHA1Final(sha_result1, &sha_context);

		if(spi_flash_erase_sector(USER_CONFIG_SECTOR) != SPI_FLASH_RESULT_OK)
		{
			log("config close write: write failed, erase failed\n");
			goto error;
		}

		if(spi_flash_write(USER_CONFIG_SECTOR * SPI_FLASH_SEC_SIZE, string_buffer(&flash_sector_buffer), SPI_FLASH_SEC_SIZE) != SPI_FLASH_RESULT_OK)
		{
			log("config close write: write failed, write failed\n");
			goto error;
		}

		if(spi_flash_read(USER_CONFIG_SECTOR * SPI_FLASH_SEC_SIZE, string_buffer_nonconst(&flash_sector_buffer), SPI_FLASH_SEC_SIZE) != SPI_FLASH_RESULT_OK)
		{
			log("config close write: write failed, verify failed\n");
			goto error;
		}

		SHA1Init(&sha_context);
		SHA1Update(&sha_context, string_buffer(&flash_sector_buffer), SPI_FLASH_SEC_SIZE);
		SHA1Final(sha_result2, &sha_context);

		if(memcmp(sha_result1, sha_result2, SHA_DIGEST_LENGTH))
		{
			log("config close write: write failed, sha mismatch\n");
			goto error;
		}

		flash_sector_buffer_use = fsb_config_write;
	}

	if(flash_sector_buffer_use != fsb_config_write)
	{
		log("config_close_write: sector buffer in use: %u\n", flash_sector_buffer_use);
		return(false);
	}

	flash_sector_buffer_use = fsb_config_cache;
	return(true);

error:
	flash_sector_buffer_use = fsb_free;
	return(false);
}

void config_abort_write(void)
{
	stat_config_write_aborted++;

	if(flash_sector_buffer_use == fsb_config_write)
		flash_sector_buffer_use = fsb_config_cache;

	if(flash_sector_buffer_use == fsb_config_write_dirty)
		flash_sector_buffer_use = fsb_free;
}

bool config_walk(string_t *id, string_t *value)
{
	int id_start_index, id_end_index, value_start_index, value_end_index;

	if(flash_sector_buffer_use != fsb_config_read)
	{
		log("config get entry: sector buffer in use: %u\n", flash_sector_buffer_use);
		return(false);
	}

	if((id_start_index = config_current_index) >= string_length(&flash_sector_buffer))
	{
		log("config get entry: sector length overrun\n");
		return(false);
	}

	if(((value_start_index = string_sep(&flash_sector_buffer, id_start_index, 1, '=')) > 0) &&
		((value_end_index = string_sep(&flash_sector_buffer, value_start_index, 1, '\n')) > 0))
	{
		id_end_index = value_start_index - 1;

		string_splice(id, 0, &flash_sector_buffer, id_start_index, id_end_index - id_start_index);
		string_splice(value, 0, &flash_sector_buffer, value_start_index, value_end_index - value_start_index - 1);

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

	if(!config_open_read())
		return(false);

	string_format_flash_ptr(&match_name, match_name_flash, param1, param2);

	while(config_walk(&name, &value))
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
	char *flash_sector_buffer_buffer;

	if((flash_sector_buffer_use != fsb_config_write) && (flash_sector_buffer_use != fsb_config_write_dirty))
	{
		log("config delete: sector buffer in use: %u\n", flash_sector_buffer_use);
		return(0);
	}

	flash_sector_buffer_buffer = string_buffer_nonconst(&flash_sector_buffer);
	string_format_flash_ptr(&match_name, match_name_flash, param1, param2);

	deleted = 0;
	name_start_index = config_current_index;

	if(name_start_index  >= string_length(&flash_sector_buffer))
	{
		log("config delete: sector length overrun\n");
		return(0);
	}

	while(((value_start_index = string_sep(&flash_sector_buffer, name_start_index, 1, '=')) > 0) &&
			((next_name_start_index = string_sep(&flash_sector_buffer, value_start_index, 1, '\n')) > 0))
	{
		string_splice(&name, 0, &flash_sector_buffer, name_start_index, value_start_index - name_start_index - 1);

		if((!wildcard && string_match_string(&match_name, &name)) ||
				(wildcard && string_nmatch_string(&match_name, &name, string_length(&match_name))))
		{
			memmove(&flash_sector_buffer_buffer[name_start_index], &flash_sector_buffer_buffer[next_name_start_index], SPI_FLASH_SEC_SIZE - next_name_start_index);
			deleted++;
		}
		else
			name_start_index = next_name_start_index;
	}

	if(deleted > 0)
		flash_sector_buffer_use = fsb_config_write_dirty;

	return(deleted);
}

bool config_set_string_flashptr(const char *match_name_flash, const char *value, int param1, int param2)
{
	int current;

	if((flash_sector_buffer_use != fsb_config_write) && (flash_sector_buffer_use != fsb_config_write_dirty))
	{
		log("config set string: sector buffer in use: %u\n", flash_sector_buffer_use);
		return(false);
	}

	config_delete_flashptr(match_name_flash, false, param1, param2);

	current = config_tail();

	string_setlength(&flash_sector_buffer, current);
	string_format_flash_ptr(&flash_sector_buffer, match_name_flash, param1, param2);
	string_format(&flash_sector_buffer, "=%s\n\n", value);

	flash_sector_buffer_use = fsb_config_write_dirty;

	return(true);
}

bool config_set_int_flashptr(const char *match_name_flash, int value, int param1, int param2)
{
	string_new(, string_value, 16);

	if((flash_sector_buffer_use != fsb_config_write) && (flash_sector_buffer_use != fsb_config_write_dirty))
	{
		log("config set int: sector buffer in use: %u\n", flash_sector_buffer_use);
		return(false);
	}

	string_format(&string_value, "%d", value);

	return(config_set_string_flashptr(match_name_flash, string_buffer(&string_value), param1, param2));
}

bool config_set_uint_flashptr(const char *match_name_flash, unsigned int value, int param1, int param2)
{
	string_new(, string_value, 16);

	if((flash_sector_buffer_use != fsb_config_write) && (flash_sector_buffer_use != fsb_config_write_dirty))
	{
		log("config set uint: sector buffer in use: %u\n", flash_sector_buffer_use);
		return(false);
	}

	string_format(&string_value, "%u", value);

	return(config_set_string_flashptr(match_name_flash, string_buffer(&string_value), param1, param2));
}

bool config_dump(string_t *dst)
{
	int int_value, amount;
	unsigned int uint_value;
	string_new(, name, 64);
	string_new(, value, 64);

	if(!config_open_read())
		return(app_action_error);

	amount = 0;

	while(config_walk(&name, &value))
	{
		string_format(dst, "%s=%s", string_to_cstr(&name), string_to_cstr(&value));

		if((parse_int(0, &value, &int_value, 0, 0) == parse_ok) && parse_uint(0, &value, &uint_value, 0, 0) == parse_ok)
			string_format(dst, " (%d/%u/%#x)", int_value, uint_value, uint_value);

		string_append(dst, "\n");

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
