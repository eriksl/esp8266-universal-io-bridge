#include "config.h"

#include "util.h"
#include "io.h"
#include "i2c_sensor.h"
#include "ota.h"

#include <ets_sys.h>
#include <c_types.h>
#include <spi_flash.h>

#define CONFIG_MAGIC "%4afc0002%\n"

enum
{
	config_entries_size = 100,
	config_entry_id_size = 28,
	config_entry_string_size = 32
};

typedef struct
{
	char	id[config_entry_id_size];
	char	string_value[config_entry_string_size];
	int		int_value;
} config_entry_t;

assert_size(config_entry_t, 64);

static unsigned int config_entries_length = 0;
static config_entry_t config_entries[config_entries_size];

irom config_flags_t config_flags_get(void)
{
	config_flags_t flags;

	if(!config_get_int("flags", -1, -1, &flags.intval))
		flags.intval = 0;

	return(flags);
}

irom bool_t config_flags_set(config_flags_t flags)
{
	return(config_set_int("flags", -1, -1, flags.intval));
}

irom void config_flags_to_string(string_t *dst)
{
	config_flags_t flags = config_flags_get();

	if(flags.flag.strip_telnet)
		string_cat(dst, " strip-telnet");
	else
		string_cat(dst, " no-strip-telnet");

	if(flags.flag.print_debug)
		string_cat(dst, " print-debug");
	else
		string_cat(dst, " no-print-debug");

	if(flags.flag.tsl_high_sens)
		string_cat(dst, " tsl-high-sens");
	else
		string_cat(dst, " no-tsl-high-sens");

	if(flags.flag.bh_high_sens)
		string_cat(dst, " bh-high-sens");
	else
		string_cat(dst, " no-bh-high-sens");

	if(flags.flag.cpu_high_speed)
		string_cat(dst, " cpu-high-speed");
	else
		string_cat(dst, " no-cpu-high-speed");

	if(flags.flag.wlan_power_save)
		string_cat(dst, " wlan-power-save");
	else
		string_cat(dst, " no-wlan-power-save");

	if(flags.flag.enable_cfa634)
		string_cat(dst, " enable-cfa634");
	else
		string_cat(dst, " no-enable-cfa634");

	if(flags.flag.i2c_high_speed)
		string_cat(dst, " i2c-high_speed");
	else
		string_cat(dst, " no-i2c-high_speed");
}

irom bool_t config_flags_change(const string_t *flag, bool_t add)
{
	config_flags_t flags = config_flags_get();
	bool_t rv = false;

	if(string_match(flag, "strip-telnet") || string_match(flag, "st"))
	{
		flags.flag.strip_telnet = add ? 1 : 0;
		rv = true;
	}

	if(string_match(flag, "print-debug") || string_match(flag, " pd"))
	{
		flags.flag.print_debug = add ? 1 : 0;
		rv = true;
	}

	if(string_match(flag, "tsl-high-sens") || string_match(flag, "ths"))
	{
		flags.flag.tsl_high_sens = add ? 1 : 0;
		rv = true;
	}

	if(string_match(flag, "bh-high-sens") || string_match(flag, "bhv"))
	{
		flags.flag.bh_high_sens = add ? 1 : 0;
		rv = true;
	}

	if(string_match(flag, "cpu-high-speed") || string_match(flag, "chs"))
	{
		flags.flag.cpu_high_speed = add ? 1 : 0;
		rv = true;
	}

	if(string_match(flag, "wlan-power-save") || string_match(flag, "wps"))
	{
		flags.flag.wlan_power_save = add ? 1 : 0;
		rv = true;
	}

	if(string_match(flag, "enable-cfa634") || string_match(flag, "ec"))
	{
		flags.flag.enable_cfa634 = add ? 1 : 0;
		rv = true;
	}

	if(string_match(flag, "i2c-high-speed") || string_match(flag, "ih"))
	{
		flags.flag.i2c_high_speed = add ? 1 : 0;
		rv = true;
	}

	if(rv)
		rv = config_flags_set(flags);

	return(rv);
}

typedef enum
{
	state_parse_id,
	state_parse_value,
	state_parse_eol,
} state_parse_t;

irom static string_t *expand_varid(const char *id, int index1, int index2)
{
	string_new(static, varid, 64);

	string_clear(&varid);
	string_format_data(&varid, id, index1, index2);

	return(&varid);
}

irom static config_entry_t *find_config_entry(const char *id, int index1, int index2)
{
	config_entry_t *config_entry;
	const string_t *varid;
	unsigned int ix;

	varid = expand_varid(id, index1, index2);

	for(ix = 0; ix < config_entries_length; ix++)
	{
		config_entry = &config_entries[ix];

		if(string_match(varid, config_entry->id))
			return(config_entry);
	}

	return((config_entry_t *)0);
}

irom bool_t config_get_string(const char *id, int index1, int index2, string_t *value)
{
	config_entry_t *config_entry;

	if(!(config_entry = find_config_entry(id, index1, index2)))
		return(false);

	string_format(value, "%s", config_entry->string_value);

	return(true);
}

irom bool_t config_get_int(const char *id, int index1, int index2, int *value)
{
	config_entry_t *config_entry;

	if(!(config_entry = find_config_entry(id, index1, index2)))
		return(false);

	*value = config_entry->int_value;

	return(true);
}

irom bool_t config_set_string(const char *id, int index1, int index2, const string_t *value, int value_offset, int value_length)
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
		strlcpy(config_current->id, string_to_const_ptr(varid), config_entry_string_size);
	}

	strlcpy(config_current->string_value, string_to_const_ptr(value) + value_offset, value_length + 1);

	string = string_from_ptr(value_length + 1, config_current->string_value);

	if(parse_int(0, &string, &config_current->int_value, 0) != parse_ok)
		config_current->int_value = -1;

	return(true);
}

irom bool_t config_set_int(const char *id, int index1, int index2, int value)
{
	string_new(, string, 16);

	string_format(&string, "%d", value);

	return(config_set_string(id, index1, index2, &string, 0, -1));
}

irom unsigned int config_delete(const char *id, int index1, int index2, bool_t wildcard)
{
	const char *varidptr;
	config_entry_t *config_current;
	unsigned int ix;
	unsigned int amount, length;

	varidptr = string_to_const_ptr(expand_varid(id, index1, index2));
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
			config_current->int_value = '0';
		}
	}

	return(amount);
}

irom bool_t config_read(void)
{
	string_new(, string, 64);
	int current_index, id_index, id_length, value_index, value_length;
	char current;
	state_parse_t parse_state;

	if(ota_is_active())
		return(false);

	if(wlan_scan_active())
		return(false);

	if(string_size(&buffer_4k) < SPI_FLASH_SEC_SIZE)
		return(false);

	if(spi_flash_read(USER_CONFIG_SECTOR * SPI_FLASH_SEC_SIZE, string_to_ptr(&buffer_4k), SPI_FLASH_SEC_SIZE) != SPI_FLASH_RESULT_OK)
		return(false);

	string_setlength(&buffer_4k, SPI_FLASH_SEC_SIZE);

	string_clear(&string);
	string_cat(&string, CONFIG_MAGIC);
	string_cat(&string, "\n");

	current_index = string_length(&string);

	if(!string_match_string_raw(&buffer_4k, &string, current_index))
		return(false);

	id_index = current_index;
	id_length = 0;
	value_index = 0;
	value_length = 0;

	config_entries_length = 0;

	for(parse_state = state_parse_id; current_index < SPI_FLASH_SEC_SIZE; current_index++)
	{
		current = string_index(&buffer_4k, current_index);

		if(current == '\0')
			goto done;

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

						string_clear(&string);
						string_splice(&string, &buffer_4k, id_index, id_length);

						config_set_string(string_to_const_ptr(&string), -1, -1, &buffer_4k, value_index, value_length);
					}

					parse_state = state_parse_eol;
					continue;
				}

				break;
			}

			case(state_parse_eol):
			{
				if(current == '\n')
					goto done;

				id_index = current_index;
				parse_state = state_parse_id;

				break;
			}

			default:
			{
				return(false);
			}
		}
	}

done:
	return(true);
}

irom unsigned int config_write(void)
{
	config_entry_t *entry;
	unsigned int ix, length;
	uint32_t crc1, crc2;

	if(ota_is_active())
		return(0);

	if(wlan_scan_active())
		return(0);

	if(string_size(&buffer_4k) < SPI_FLASH_SEC_SIZE)
		return(0);

	string_clear(&buffer_4k);
	string_cat(&buffer_4k, CONFIG_MAGIC);
	string_cat(&buffer_4k, "\n");

	for(ix = 0; ix < config_entries_length; ix++)
	{
		entry = &config_entries[ix];

		if(!entry->id[0])
			continue;

		string_format(&buffer_4k, "%s=%s\n", entry->id, entry->string_value);
	}

	string_cat(&buffer_4k, "\n");

	length = string_length(&buffer_4k);

	if(length > (4096 - 32))
		return(0);

	while(string_length(&buffer_4k) < SPI_FLASH_SEC_SIZE)
		string_append(&buffer_4k, '.');

	string_crc32_init();
	crc1 = string_crc32(&buffer_4k, 0, SPI_FLASH_SEC_SIZE);

	if(spi_flash_erase_sector(USER_CONFIG_SECTOR) != SPI_FLASH_RESULT_OK)
		return(0);

	if(spi_flash_write(USER_CONFIG_SECTOR * SPI_FLASH_SEC_SIZE, string_to_const_ptr(&buffer_4k), SPI_FLASH_SEC_SIZE) != SPI_FLASH_RESULT_OK)
		return(0);

	if(spi_flash_read(USER_CONFIG_SECTOR * SPI_FLASH_SEC_SIZE, string_to_ptr(&buffer_4k), SPI_FLASH_SEC_SIZE) != SPI_FLASH_RESULT_OK)
		return(0);

	string_setlength(&buffer_4k, SPI_FLASH_SEC_SIZE);

	crc2 = string_crc32(&buffer_4k, 0, SPI_FLASH_SEC_SIZE);

	if(crc1 != crc2)
		return(0);

	return(length);
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

		string_format(dst, "%s=%s (%d)\n", config_current->id, config_current->string_value, config_current->int_value);
	}

	string_format(dst, "\nslots total: %u, config items: %u, free slots: %u\n", config_entries_size, in_use, config_entries_size - in_use);
}
