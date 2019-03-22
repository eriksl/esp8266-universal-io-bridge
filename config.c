#include "config.h"

#include "util.h"
#include "io.h"
#include "i2c_sensor.h"
#include "ota.h"
#include "dispatch.h"

#include <ets_sys.h>
#include <spi_flash.h>

#define CONFIG_MAGIC "%4afc0002%"

enum
{
	config_entries_size = 160,
	config_entry_id_size = 28,
	config_entry_string_size = 20,
};

typedef struct
{
	char		id[config_entry_id_size];
	char		string_value[config_entry_string_size];
	uint32_t	uint_value;
} config_entry_t;

assert_size(config_entry_t, 52);

typedef struct
{
	uint32_t value;
	const char *name;
} config_flag_name_t;

static const config_flag_name_t config_flag_names[] =
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
	{	flag_none,				""					},
};

uint32_t flags_cache;
static unsigned int config_entries_length = 0;
static config_entry_t config_entries[config_entries_size];

irom void config_flags_to_string(_Bool nl, const char *prefix, string_t *dst)
{
	const config_flag_name_t *entry;

	for(entry = config_flag_names; entry->value != flag_none; entry++)
	{
		string_append_cstr(dst, prefix);
		string_append_cstr(dst, (flags_cache & entry->value) ? "   " : "no ");
		string_append_cstr(dst, entry->name);
		string_append_cstr(dst, nl ? "\n" : " ");
	}
}

irom _Bool config_flags_change(const string_t *flag, _Bool set)
{
	const config_flag_name_t *entry;

	for(entry = config_flag_names; entry->value != flag_none; entry++)
	{
		if(string_match_cstr(flag, entry->name))
		{
			if(set)
				flags_cache |= entry->value;
			else
				flags_cache &= ~entry->value;

			string_init(varname, "flags");
			return(config_set_int(&varname, -1, -1, flags_cache));
		}
	}

	return(false);
}

typedef enum
{
	state_parse_id,
	state_parse_value,
	state_parse_eol,
} state_parse_t;

irom static string_t *expand_varid(const string_t *varid, int index1, int index2)
{
	string_new(static, varid_in, config_entry_id_size);
	string_new(static, varid_out, config_entry_id_size);

	string_clear(&varid_in);
	string_clear(&varid_out);

	string_append_string(&varid_in, varid);
	string_format_cstr(&varid_out, string_to_cstr(&varid_in), index1, index2);

	return(&varid_out);
}

irom static config_entry_t *find_config_entry(const string_t *id, int index1, int index2)
{
	config_entry_t *config_entry;
	const string_t *varid;
	unsigned int ix;

	varid = expand_varid(id, index1, index2);

	for(ix = 0; ix < config_entries_length; ix++)
	{
		config_entry = &config_entries[ix];

		if(string_match_cstr(varid, config_entry->id))
			return(config_entry);
	}

	return((config_entry_t *)0);
}

irom _Bool config_get_string(const string_t *id, int index1, int index2, string_t *value)
{
	config_entry_t *config_entry;

	if(!(config_entry = find_config_entry(id, index1, index2)))
		return(false);

	string_format(value, "%s", config_entry->string_value);

	return(true);
}

irom _Bool config_get_int(const string_t *id, int index1, int index2, uint32_t *value)
{
	config_entry_t *config_entry;

	if(!(config_entry = find_config_entry(id, index1, index2)))
		return(false);

	*value = config_entry->uint_value;

	return(true);
}

irom _Bool config_set_string(const string_t *id, int index1, int index2, const string_t *value, int value_offset, int value_length)
{
	string_t string;
	string_t *varid;
	config_entry_t *config_current;
	unsigned int ix;

	if(value_offset >= string_length(value))
		value_offset = string_length(value) - 1;

	if(value_offset < 0)
		value_offset = 0;

	if(value_length < 0)
		value_length = string_length(value) - value_offset;

	if((value_offset + value_length) > string_length(value))
		value_length = string_length(value) - value_offset;

	if(value_length >= config_entry_string_size)
		value_length = config_entry_string_size - 1;

	if(value_length < 0)
		value_length = 0;

	if(!(config_current = find_config_entry(id, index1, index2)))
	{
		for(ix = 0; ix < config_entries_length; ix++)
		{
			config_current = &config_entries[ix];

			if(!config_current->id[0])
				break;
		}

		if(ix >= config_entries_length)
		{
			if((config_entries_length + 1) >= config_entries_size)
				return(false);

			config_current = &config_entries[config_entries_length++];
		}

		varid = expand_varid(id, index1, index2);
		strecpy(config_current->id, string_to_cstr(varid), config_entry_id_size);
	}

	strecpy(config_current->string_value, string_buffer(value) + value_offset, value_length + 1);

	string = string_from_cstr(value_length + 1, config_current->string_value);

	if(parse_uint(0, &string, &config_current->uint_value, 0, ' ') != parse_ok)
		config_current->uint_value = 0;

	return(true);
}

irom _Bool config_set_int(const string_t *id, int index1, int index2, uint32_t value)
{
	string_new(, string, 16);

	string_format(&string, "%u", value);

	return(config_set_string(id, index1, index2, &string, 0, -1));
}

irom unsigned int config_delete(const string_t *id, int index1, int index2, _Bool wildcard)
{
	const char *varidptr;
	config_entry_t *config_current;
	unsigned int ix, amount, length;

	varidptr = string_to_cstr(expand_varid(id, index1, index2));
	length = strlen(varidptr);

	for(ix = 0, amount = 0; ix < config_entries_length; ix++)
	{
		config_current = &config_entries[ix];

		if((wildcard && !strncmp(config_current->id, varidptr, length)) ||
			(!wildcard && !strcmp(config_current->id, varidptr)))
		{
			amount++;
			config_current->id[0] = '\0';
			config_current->string_value[0] = '\0';
			config_current->uint_value = 0;
		}
	}

	return(amount);
}

irom _Bool config_read(void)
{
	string_new(, string, 64);
	unsigned int current_index, id_index, id_length, value_index, value_length;
	char current;
	state_parse_t parse_state;
	_Bool rv = false;

	string_clear(&flash_sector_buffer);

	if(string_size(&flash_sector_buffer) < SPI_FLASH_SEC_SIZE)
		goto done;

	if(spi_flash_read(USER_CONFIG_SECTOR * SPI_FLASH_SEC_SIZE, string_buffer_nonconst(&flash_sector_buffer), SPI_FLASH_SEC_SIZE) != SPI_FLASH_RESULT_OK)
		goto done;

	string_setlength(&flash_sector_buffer, SPI_FLASH_SEC_SIZE);

	string_append(&string, CONFIG_MAGIC);
	string_append(&string, "\n");

	current_index = string_length(&string);

	if(!string_nmatch_string(&flash_sector_buffer, &string, current_index))
		goto done;

	id_index = current_index;
	id_length = 0;
	value_index = 0;
	value_length = 0;

	config_entries_length = 0;

	for(parse_state = state_parse_id; current_index < SPI_FLASH_SEC_SIZE; current_index++)
	{
		current = string_at(&flash_sector_buffer, current_index);

		if(current == '\0')
		{
			rv = true;
			goto done;
		}

		if(current == '\r')
			continue;

		switch(parse_state)
		{
			case(state_parse_id):
			{
				if(current == '=')
				{
					id_length = current_index - id_index;

					value_index = current_index + 1;
					value_length = 0;

					parse_state = state_parse_value;
					continue;
				}

				if(current == '\n')
				{
					parse_state = state_parse_eol;
					continue;
				}

				break;
			}

			case(state_parse_value):
			{
				if(current == '\n')
				{
					if((id_index > 0) && (id_length > 0) && (value_index > 0))
					{
						value_length = current_index - value_index;
						string_splice(&string, 0, &flash_sector_buffer, id_index, id_length);
						config_set_string(&string, -1, -1, &flash_sector_buffer, value_index, value_length);
					}

					parse_state = state_parse_eol;
					continue;
				}

				break;
			}

			case(state_parse_eol):
			{
				if(current == '\n')
				{
					rv = true;
					goto done;
				}

				id_index = current_index;
				parse_state = state_parse_id;

				break;
			}

			default:
			{
				goto done;
			}
		}
	}

done:
	string_clear(&flash_sector_buffer);

	string_init(varname, "flags");

	if(!config_get_int(&varname, -1, -1, &flags_cache))
	{
		flags_cache = flag_log_to_uart | flag_log_to_buffer;
		config_set_int(&varname, -1, -1, flags_cache);
	}

	return(rv);
}

irom unsigned int config_write(void)
{
	config_entry_t *entry;
	unsigned int ix, length = 0;
	uint32_t crc1, crc2;

	if(string_size(&flash_sector_buffer) < SPI_FLASH_SEC_SIZE)
	{
		log("config_write: buffer too small\n");
		goto error;
	}

	string_clear(&flash_sector_buffer);
	string_append(&flash_sector_buffer, CONFIG_MAGIC);
	string_append(&flash_sector_buffer, "\n");

	for(ix = 0; ix < config_entries_length; ix++)
	{
		entry = &config_entries[ix];

		if(entry->id[0] == '\0')
			continue;

		string_format(&flash_sector_buffer, "%s=%s\n", entry->id, entry->string_value);
	}

	string_append(&flash_sector_buffer, "\n");

	length = string_length(&flash_sector_buffer);

	while(string_length(&flash_sector_buffer) < SPI_FLASH_SEC_SIZE)
		string_append_byte(&flash_sector_buffer, '.');

	string_crc32_init();
	crc1 = string_crc32(&flash_sector_buffer, 0, SPI_FLASH_SEC_SIZE);

	if(spi_flash_erase_sector(USER_CONFIG_SECTOR) != SPI_FLASH_RESULT_OK)
		goto error;

	if(spi_flash_write(USER_CONFIG_SECTOR * SPI_FLASH_SEC_SIZE, string_buffer(&flash_sector_buffer), SPI_FLASH_SEC_SIZE) != SPI_FLASH_RESULT_OK)
		goto error;

	if(spi_flash_read(USER_CONFIG_SECTOR * SPI_FLASH_SEC_SIZE, string_buffer_nonconst(&flash_sector_buffer), SPI_FLASH_SEC_SIZE) != SPI_FLASH_RESULT_OK)
		goto error;

	string_setlength(&flash_sector_buffer, SPI_FLASH_SEC_SIZE);

	crc2 = string_crc32(&flash_sector_buffer, 0, SPI_FLASH_SEC_SIZE);

	if(crc1 != crc2)
		goto error;

	string_clear(&flash_sector_buffer);
	return(length);

error:
	string_clear(&flash_sector_buffer);
	return(0);
}

irom void config_dump(string_t *dst)
{
	config_entry_t *config_current;
	unsigned int ix, in_use = 0;

	for(ix = 0; ix < config_entries_length; ix++)
	{
		config_current = &config_entries[ix];

		if(!config_current->id[0])
			continue;

		in_use++;

		string_format(dst, "%s=%s (%u/%d)\n", config_current->id, config_current->string_value, config_current->uint_value, (int)config_current->uint_value);
	}

	string_format(dst, "\nslots total: %u, config items: %u, free slots: %u\n", config_entries_size, in_use, config_entries_size - in_use);
}
