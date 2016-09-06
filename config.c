#include "config.h"

#include "util.h"
#include "io.h"
#include "i2c_sensor.h"
#include "ota.h"

#include <ets_sys.h>
#include <c_types.h>
#include <spi_flash.h>

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

config_t config;

static unsigned int config_entries_length = 0;
static config_entry_t config_entries[config_entries_size];

static config_flag_t config_flag[config_flag_size] =
{
	{
		config_flag_strip_telnet,
		"st", "strip-telnet"
	},
	{
		config_flag_print_debug,
		"pd", "print-debug"
	},
	{
		config_flag_tsl_high_sens,
		"th", "tsl-high-sens"
	},
	{
		config_flag_bh_high_sens,
		"th", "bh-high-sens"
	},
	{
		config_flag_cpu_high_speed,
		"chs", "cpu-high-speed"
	},
	{
		config_flag_wlan_power_save,
		"wps", "wlan-power-save"
	},
	{
		config_flag_enable_cfa634,
		"ec", "enable-cfa634"
	},
	{
		config_flag_i2c_highspeed,
		"ih", "i2c-highspeed",
	},
};

irom static attr_pure config_flag_enum_t config_flag_to_id(const string_t *flag_name)
{
	const config_flag_t *entry;
	config_flag_enum_t current;

	for(current = 0; current < config_flag_size; current++)
	{
		entry = &config_flag[current];

		if(string_match(flag_name, entry->short_name) || string_match(flag_name, entry->long_name))
			break;
	}

	return(current);
}

irom attr_pure bool_t config_get_flag(config_flag_enum_t flag)
{
	if(flag < config_flag_size)
		return(config.flags & (1 << flag));

	return(false);
}

irom bool_t config_set_flag(config_flag_enum_t flag, bool_t onoff)
{
	if(flag < config_flag_size)
	{
		if(onoff)
			config.flags |= (1 << flag);
		else
			config.flags &= ~(1 << flag);

		return(true);
	}

	return(false);
}

irom bool_t attr_pure config_get_flag_by_name(const string_t *flag_name)
{
	return(config_get_flag(config_flag_to_id(flag_name)));
}

irom bool_t config_set_flag_by_name(const string_t *flag_name, bool_t value)
{
	return(config_set_flag(config_flag_to_id(flag_name), value));
}

irom void config_flags_to_string(string_t *dst, const char *pre, const char *post, int flags)
{
	config_flag_enum_t current;

	if(pre)
		string_format(dst, "%s", pre);

	for(current = 0; current < config_flag_size; current++)
		string_format(dst, "%s%s:%s", string_length(dst) > 0 ? " " : "", config_flag[current].long_name, onoff(flags & 1 << current));

	if(post)
		string_format(dst, "%s", post);
}

irom void config_read(config_t *cfg)
{
	int io, pin;
	io_config_pin_entry_t *pin_config;

	spi_flash_read(USER_CONFIG_SECTOR * SPI_FLASH_SEC_SIZE, (void *)cfg, sizeof(*cfg));

	if((cfg->magic != config_magic) || (cfg->version != config_version)) // init config to default
	{
		memset(cfg, 0, sizeof(*cfg));

		cfg->magic = config_magic;
		cfg->version = config_version;

		strlcpy(cfg->client_wlan.ssid, DEFAULT_SSID, sizeof(cfg->client_wlan.ssid));
		strlcpy(cfg->client_wlan.passwd, DEFAULT_PASSWD, sizeof(cfg->client_wlan.passwd));

		strlcpy(cfg->ap_wlan.ssid, DEFAULT_SSID, sizeof(cfg->ap_wlan.ssid));
		strlcpy(cfg->ap_wlan.passwd, DEFAULT_PASSWD, sizeof(cfg->ap_wlan.passwd));
		cfg->ap_wlan.channel = 1;

		cfg->wlan_mode = config_wlan_mode_client;

		cfg->flags = 0;

		cfg->uart.baud_rate = 115200;
		cfg->uart.data_bits = 8;
		cfg->uart.parity = parity_none;
		cfg->uart.stop_bits = 1;

		config_set_flag(config_flag_print_debug, true);

		cfg->bridge.port = 23;
		cfg->bridge.timeout = 0;
		cfg->command.port = 24;
		cfg->command.timeout = 0;

		cfg->status_trigger.io = -1;
		cfg->status_trigger.pin = -1;
		cfg->assoc_trigger.io = -1;
		cfg->assoc_trigger.pin = -1;

		cfg->ntp.server = ip_addr("0.0.0.0");
		cfg->ntp.timezone = 0;

		cfg->display.flip_timeout = 4;
		strlcpy(cfg->display.default_msg, "%%%%", sizeof(cfg->display.default_msg));

		i2c_sensor_config_init(&cfg->i2c_sensors);

		for(io = 0; io < io_id_size; io++)
		{
			for(pin = 0; pin < max_pins_per_io; pin++)
			{
				pin_config = &config.io_config[io][pin];

				pin_config->mode = io_pin_disabled;
				pin_config->llmode = io_pin_ll_disabled;
				pin_config->flags.autostart = 0;
				pin_config->flags.repeat = 0;
				pin_config->flags.pullup = 0;
				pin_config->flags.reset_on_read = 0;

				// activate UART by default

				if((io == io_id_gpio) && ((pin == 1) || (pin == 3))) // shamefully hardcoded :-(
				{
					pin_config->mode = io_pin_uart;
					pin_config->llmode = io_pin_ll_uart;

					if(pin == 3)
						pin_config->flags.pullup = 1;
				}
			}
		}
	}

	cfg->client_wlan.ssid[sizeof(cfg->client_wlan.ssid) - 1] = '\0';
	cfg->client_wlan.passwd[sizeof(cfg->client_wlan.passwd) - 1] = '\0';
	cfg->ap_wlan.ssid[sizeof(cfg->ap_wlan.ssid) - 1] = '\0';
	cfg->ap_wlan.passwd[sizeof(cfg->ap_wlan.passwd) - 1] = '\0';
	cfg->display.default_msg[sizeof(cfg->display.default_msg) - 1] = '\0';

	// failsafe for corrupt / blank config

	if((strlen(cfg->client_wlan.ssid) < 2) || (strlen(cfg->client_wlan.passwd) < 8))
	{
		strlcpy(cfg->client_wlan.ssid, DEFAULT_SSID, sizeof(cfg->client_wlan.ssid));
		strlcpy(cfg->client_wlan.passwd, DEFAULT_PASSWD, sizeof(cfg->client_wlan.passwd));
	}

	if((strlen(cfg->ap_wlan.ssid) < 2) || (strlen(cfg->ap_wlan.passwd) < 8) ||
			(cfg->ap_wlan.channel < 1) || (cfg->ap_wlan.channel > 13))
	{
		strlcpy(cfg->ap_wlan.ssid, DEFAULT_SSID, sizeof(cfg->ap_wlan.ssid));
		strlcpy(cfg->ap_wlan.passwd, DEFAULT_PASSWD, sizeof(cfg->ap_wlan.passwd));
		cfg->ap_wlan.channel = 13;
	}

	if((cfg->pwm.period < 8) || (cfg->pwm.period > 16))
		cfg->pwm.period = 8;
}

irom void config_write(config_t *cfg)
{
	spi_flash_erase_sector(USER_CONFIG_SECTOR);
	spi_flash_write(USER_CONFIG_SECTOR * SPI_FLASH_SEC_SIZE, (void *)cfg, sizeof(*cfg));
}

irom void config_dump(string_t *dst, const config_t *cfg)
{
	string_new(, ntp_server, 32);

	string_clear(&ntp_server);
	string_ip(&ntp_server, cfg->ntp.server);

	string_format(dst, 
			"> config magic: %04x\n"
			"> config version: %d\n"
			"> wlan client ssid: %s\n"
			"> wlan client passwd: %s\n"
			"> wlan ap ssid: %s\n"
			"> wlan ap passwd: %s\n"
			"> wlan ap channel: %d\n"
			"> wlan mode: %s mode\n"
			"> bridge tcp port: %u\n"
			"> bridge tcp timeout: %u\n"
			"> command tcp port: %u\n"
			"> command tcp timeout: %u\n"
			"> ntp server: %s\n"
			"> ntp time zone: GMT%c%u\n"
			"> display flip timeout: %u\n"
			"> display default message: %s\n"
			"> status trigger gpio (-1 is disabled): %d/%d\n"
			"> wlan association trigger gpio (-1 is disabled): %d/%d\n"
			"> flags: ", 
		cfg->magic,
		cfg->version,
		cfg->client_wlan.ssid,
		cfg->client_wlan.passwd,
		cfg->ap_wlan.ssid,
		cfg->ap_wlan.passwd,
		cfg->ap_wlan.channel,
		cfg->wlan_mode == config_wlan_mode_client ? "client" : "ap",
		cfg->bridge.port,
		cfg->bridge.timeout,
		cfg->command.port,
		cfg->command.timeout,
		string_to_ptr(&ntp_server),
		cfg->ntp.timezone >= 0 ? '+' : '-',
		cfg->ntp.timezone >= 0 ? cfg->ntp.timezone : 0 - cfg->ntp.timezone,
		cfg->display.flip_timeout,
		cfg->display.default_msg,
		cfg->status_trigger.io,
		cfg->status_trigger.pin,
		cfg->assoc_trigger.io,
		cfg->assoc_trigger.pin);

	config_flags_to_string(dst, 0, 0, cfg->flags);

	string_cat(dst, "\n> uart: ");
	uart_parameters_to_string(dst, &cfg->uart);
	string_cat(dst, "\n> gpios:\n");
	io_config_dump(dst, cfg, -1, -1, false);
}

// NEW config

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

	string = string_from_ptr(value_length, config_current->string_value);
	if(parse_int(0, &string, &config_current->int_value, 0) != parse_ok)
		config_current->int_value = 0;

	return(true);
}

irom bool_t config_set_int(const char *id, int index1, int index2, int value)
{
	string_new(, string, 16);

	string_clear(&string);
	string_format(&string, "%d", value);

	return(config_set_string(id, index1, index2, &string, 0, -1));
}

irom bool_t config_delete(const char *id, int index1, int index2)
{
	const char *varidptr;
	config_entry_t *config_current;
	unsigned int ix;
	unsigned int amount;

	varidptr = string_to_const_ptr(expand_varid(id, index1, index2));

	for(ix = 0, amount = 0; ix < config_entries_length; ix++)
	{
		config_current = &config_entries[ix];

		if(!ets_strcmp(config_current->id, varidptr))
		{
			amount++;
			config_current->id[0] = '\0';
			config_current->string_value[0] = '\0';
			config_current->int_value = '0';
		}
	}

	return(amount > 0);
}

irom bool_t config_export(const config_t *cfg, string_t *sector)
{
	const ip_addr_to_bytes_t *ntp_server = (const ip_addr_to_bytes_t *)&cfg->ntp.server;

	string_format(sector, 
			"%%%04x%%\n"
			"version=%d\n"
			"wlan.client.ssid=%s\n"
			"wlan.client.passwd=%s\n"
			"wlan.ap.ssid=%s\n"
			"wlan.ap.passwd=%s\n"
			"wlan.ap.channel=%d\n"
			"wlan.mode=%d\n"
			"bridge.tcp.port=%u\n"
			"bridge.tcp.timeout=%u\n"
			"cmd.tcp.port=%u\n"
			"cmd.tcp.timeout=%u\n"
			"ntp.srv.0=%u\n"
			"ntp.srv.1=%u\n"
			"ntp.srv.2=%u\n"
			"ntp.srv.3=%u\n"
			"ntp.tz=%u\n"
			"display.flip.timeout=%u\n"
			"display.default.message=%s\n"
			"trigger.status.io=%d\n"
			"trigger.status.pin=%d\n"
			"trigger.assoc.io=%d\n"
			"trigger.assoc.pin=%d\n"
			"flags=0x%04x\n"
			"uart.baud=%u\n"
			"uart.databits=%u\n"
			"uart.parity=%u\n"
			"uart.stopbits=%u\n"
			"pwm.period=%u\n",
		cfg->magic,
		cfg->version,
		cfg->client_wlan.ssid,
		cfg->client_wlan.passwd,
		cfg->ap_wlan.ssid,
		cfg->ap_wlan.passwd,
		cfg->ap_wlan.channel,
		cfg->wlan_mode,
		cfg->bridge.port,
		cfg->bridge.timeout,
		cfg->command.port,
		cfg->command.timeout,
		ntp_server->byte[0],
		ntp_server->byte[1],
		ntp_server->byte[2],
		ntp_server->byte[3],
		cfg->ntp.timezone,
		cfg->display.flip_timeout,
		cfg->display.default_msg,
		cfg->status_trigger.io,
		cfg->status_trigger.pin,
		cfg->assoc_trigger.io,
		cfg->assoc_trigger.pin,
		cfg->flags,
		cfg->uart.baud_rate,
		cfg->uart.data_bits,
		cfg->uart.parity,
		cfg->uart.stop_bits,
		cfg->pwm.period);

	i2c_sensor_export(cfg, sector);

	io_config_export(cfg, sector);

	string_append(sector, '\n');

	if(string_length(sector) > (4096 - 32))
		return(false);

	while(string_length(sector) < SPI_FLASH_SEC_SIZE)
		string_append(sector, '.');

	return(true);
}

irom bool_t config_write_text(const string_t *sector)
{
	if(ota_is_active())
		return(false);

	if(wlan_scan_active())
		return(false);

	if(spi_flash_erase_sector(0xfd /* FIXME */) != SPI_FLASH_RESULT_OK)
		return(false);

	if(spi_flash_write(0xfd /* FIXME */ * SPI_FLASH_SEC_SIZE, string_to_const_ptr(sector), SPI_FLASH_SEC_SIZE) != SPI_FLASH_RESULT_OK)
		return(false);

	if(spi_flash_read(0xfd /* FIXME */ * SPI_FLASH_SEC_SIZE, string_to_ptr(&buffer_4k), SPI_FLASH_SEC_SIZE) != SPI_FLASH_RESULT_OK)
		return(false);

	string_setlength(&buffer_4k, SPI_FLASH_SEC_SIZE);

	if(!string_match_string_raw(sector, &buffer_4k, SPI_FLASH_SEC_SIZE))
		return(false);

	return(true);
}

irom bool_t config_read_text(string_t *sector)
{
	if(spi_flash_read(0xfd /* FIXME */ * SPI_FLASH_SEC_SIZE, string_to_ptr(sector), SPI_FLASH_SEC_SIZE) != SPI_FLASH_RESULT_OK)
		return(false);

	string_setlength(sector, SPI_FLASH_SEC_SIZE);

	return(true);
}

typedef enum
{
	state_parse_id,
	state_parse_value,
	state_parse_eol,
} state_parse_t;

irom bool_t config_import(const string_t *sector)
{
	string_new(, string, 64);
	int current_index, id_index, id_length, value_index, value_length;
	char current;
	state_parse_t parse_state;

	string_clear(&string);
	string_format(&string, "%%%04x%%\n", config_magic);

	current_index = string_length(&string);

	if(!string_match_string_raw(sector, &string, current_index))
		return(false);

	id_index = current_index;
	id_length = 0;
	value_index = 0;
	value_length = 0;

	config_entries_length = 0;

	for(parse_state = state_parse_id; current_index < SPI_FLASH_SEC_SIZE; current_index++)
	{
		current = string_index(sector, current_index);

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
						string_splice(&string, sector, id_index, id_length);

						config_set_string(string_to_const_ptr(&string), -1, -1, sector, value_index, value_length);
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

irom void config_dump_text(string_t *dst)
{
	config_entry_t *config_current;
	unsigned int ix;

	string_format(dst, "items: %u, free slots: %u\n", config_entries_length, config_entries_size - config_entries_length);

	for(ix = 0; ix < config_entries_length; ix++)
	{
		config_current = &config_entries[ix];

		if(!config_current->id[0])
			continue;

		string_format(dst, "%s=%s (%d)\n", config_current->id, config_current->string_value, config_current->int_value);
	}
}
