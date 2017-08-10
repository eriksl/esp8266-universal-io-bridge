#include "io_gpio.h"
#include "io_aux.h"
#include "io_mcp.h"
#include "io_pcf.h"
#include "io.h"
#include "i2c.h"
#include "config.h"

io_config_pin_entry_t io_config[io_id_size][max_pins_per_io];

io_info_t io_info =
{
	{
		/* io_id_gpio = 0 */
		0x00,
		0,
		16,
		{
			.input_digital = 1,
			.counter = 1,
			.output_digital = 1,
			.input_analog = 0,
			.output_analog = 1,
			.i2c = 1,
			.uart = 1,
			.pullup = 1,
		},
		"Internal GPIO",
		io_gpio_init,
		io_gpio_periodic,
		io_gpio_init_pin_mode,
		io_gpio_get_pin_info,
		io_gpio_read_pin,
		io_gpio_write_pin,
	},
	{
		/* io_id_aux = 1 */
		0x01,
		0,
		2,
		{
			.input_digital = 1,
			.counter = 0,
			.output_digital = 1,
			.input_analog = 1,
			.output_analog = 0,
			.i2c = 0,
			.uart = 0,
			.pullup = 0,
		},
		"Auxilliary GPIO (RTC+ADC)",
		io_aux_init,
		(void *)0,
		io_aux_init_pin_mode,
		io_aux_get_pin_info,
		io_aux_read_pin,
		io_aux_write_pin,
	},
	{
		/* io_id_mcp_20 = 2 */
		0x20,
		io_mcp_instance_20,
		16,
		{
			.input_digital = 1,
			.counter = 1,
			.output_digital = 1,
			.input_analog = 0,
			.output_analog = 0,
			.i2c = 0,
			.uart = 0,
			.pullup = 1,
		},
		"MCP23017 I2C I/O expander",
		io_mcp_init,
		io_mcp_periodic,
		io_mcp_init_pin_mode,
		io_mcp_get_pin_info,
		io_mcp_read_pin,
		io_mcp_write_pin,
	},
	{
		/* io_id_pcf_3a = 3 */
		0x3a,
		io_pcf_instance_3a,
		8,
		{
			.input_digital = 1,
			.counter = 0,
			.output_digital = 1,
			.input_analog = 0,
			.output_analog = 0,
			.i2c = 0,
			.uart = 0,
			.pullup = 0,
		},
		"PCF8574A I2C I/O expander",
		io_pcf_init,
		0,
		io_pcf_init_pin_mode,
		0,
		io_pcf_read_pin,
		io_pcf_write_pin,
	}
};

static io_data_t io_data;

typedef struct
{
	io_pin_mode_t	mode;
	const char		*name;
} io_mode_trait_t;

static io_mode_trait_t io_mode_traits[io_pin_size] =
{
	{ io_pin_disabled,			"disabled"	},
	{ io_pin_input_digital,		"inputd"	},
	{ io_pin_counter,			"counter"	},
	{ io_pin_output_digital,	"outputd"	},
	{ io_pin_timer,				"timer"		},
	{ io_pin_input_analog,		"inputa"	},
	{ io_pin_output_analog,		"outputa"	},
	{ io_pin_i2c,				"i2c"		},
	{ io_pin_uart,				"uart"		},
	{ io_pin_lcd,				"lcd"		},
	{ io_pin_trigger,			"trigger"	},
};

irom static io_pin_mode_t io_mode_from_string(const string_t *src)
{
	int ix;
	const io_mode_trait_t *entry;

	for(ix = 0; ix < io_pin_size; ix++)
	{
		entry = &io_mode_traits[ix];

		if(string_match(src, entry->name))
			return(entry->mode);
	}

	return(io_pin_error);
}

irom static void io_string_from_mode(string_t *name, io_pin_mode_t mode)
{
	int ix;
	const io_mode_trait_t *entry;

	for(ix = 0; ix < io_pin_size; ix++)
	{
		entry = &io_mode_traits[ix];

		if(entry->mode == mode)
		{
			string_format(name, "%s", entry->name);
			return;
		}
	}

	string_cat(name, "error");
}

typedef struct
{
	io_pin_ll_mode_t	mode;
	const char			*name;
} io_ll_mode_trait_t;

static io_ll_mode_trait_t io_ll_mode_traits[io_pin_ll_size] =
{
	{ io_pin_ll_disabled,			"disabled"	},
	{ io_pin_ll_input_digital,		"d-input"	},
	{ io_pin_ll_counter,			"counter"	},
	{ io_pin_ll_output_digital,		"d-output"	},
	{ io_pin_ll_input_analog,		"a-input"	},
	{ io_pin_ll_output_analog,		"a-output"	},
	{ io_pin_ll_i2c,				"i2c"		},
	{ io_pin_ll_uart,				"uart"		},
};

irom void io_string_from_ll_mode(string_t *name, io_pin_ll_mode_t mode)
{
	int ix;
	const io_ll_mode_trait_t *entry;

	for(ix = 0; ix < io_pin_size; ix++)
	{
		entry = &io_ll_mode_traits[ix];

		if(entry->mode == mode)
		{
			string_format(name, "%s", entry->name);
			return;
		}
	}

	string_cat(name, "error");
}

irom static io_i2c_t io_i2c_pin_from_string(const string_t *pin)
{
	if(string_match(pin, "sda"))
		return(io_i2c_sda);
	else if(string_match(pin, "scl"))
		return(io_i2c_scl);
	else
		return(io_i2c_error);
}

irom static void io_string_from_i2c_type(string_t *name, io_i2c_t type)
{
	switch(type)
	{
		case(io_i2c_sda): { string_cat(name, "sda"); break; }
		case(io_i2c_scl): { string_cat(name, "scl"); break; }
		default: { string_cat(name, "error"); break; }
	}
}

typedef struct
{
	io_lcd_mode_t	mode;
	const char		*name;
} io_lcd_mode_trait_t;

static io_lcd_mode_trait_t io_lcd_mode_traits[io_lcd_size] =
{
	{ io_lcd_rs,	"rs"	},
	{ io_lcd_rw,	"rw"	},
	{ io_lcd_e,		"e"		},
	{ io_lcd_d0,	"d0"	},
	{ io_lcd_d1,	"d1"	},
	{ io_lcd_d2,	"d2"	},
	{ io_lcd_d3,	"d3"	},
	{ io_lcd_d4,	"d4"	},
	{ io_lcd_d5,	"d5"	},
	{ io_lcd_d6,	"d6"	},
	{ io_lcd_d7,	"d7"	},
	{ io_lcd_bl,	"bl"	}
};

irom static io_lcd_mode_t io_lcd_mode_from_string(const string_t *src)
{
	int ix;
	const io_lcd_mode_trait_t *entry;

	for(ix = 0; ix < io_lcd_size; ix++)
	{
		entry = &io_lcd_mode_traits[ix];

		if(string_match(src, entry->name))
			return(entry->mode);
	}

	return(io_lcd_error);
}

irom static void io_string_from_lcd_mode(string_t *name, io_lcd_mode_t mode)
{
	int ix;
	const io_lcd_mode_trait_t *entry;

	for(ix = 0; ix < io_lcd_size; ix++)
	{
		entry = &io_lcd_mode_traits[ix];

		if(entry->mode == mode)
		{
			string_format(name, "%s", entry->name);
			return;
		}
	}

	string_cat(name, "error");
}

typedef struct
{
	io_trigger_t	id;
	const char		*name;
} io_trigger_action_t;

static io_trigger_action_t io_trigger_action[io_trigger_size] =
{
	{ io_trigger_none,		"none"		},
	{ io_trigger_off,		"off"		},
	{ io_trigger_on,		"on"		},
	{ io_trigger_down,		"down"		},
	{ io_trigger_up,		"up"		},
	{ io_trigger_toggle,	"toggle"	},
	{ io_trigger_stop,		"stop"		},
	{ io_trigger_start,		"start"		},
};

irom static io_trigger_t string_to_trigger_action(const string_t *src)
{
	int ix;
	const io_trigger_action_t *entry;

	for(ix = 0; ix < io_trigger_size; ix++)
	{
		entry = &io_trigger_action[ix];

		if(string_match(src, entry->name))
			return(entry->id);
	}

	return(io_trigger_error);
}

irom static void trigger_actions_to_string(string_t *dst)

{
	int ix;
	const io_trigger_action_t *entry;

	for(ix = 0; ix < io_trigger_size; ix++)
	{
		entry = &io_trigger_action[ix];
		string_format(dst, "%s", entry->name);
	}
}

irom static void trigger_action_to_string(string_t *name, io_trigger_t id)
{
	int ix;
	const io_trigger_action_t *entry;

	for(ix = 0; ix < io_trigger_size; ix++)
	{
		entry = &io_trigger_action[ix];

		if(entry->id == id)
		{
			string_format(name, "%s", entry->name);
			return;
		}
	}

	string_cat(name, "error");
}

irom static void trigger_usage(string_t *dst)
{
	string_cat(dst, "usage: io-trigger <io> <pin> <action>\n");
	string_cat(dst, "action: ");
	trigger_actions_to_string(dst);
}

irom static void iomode_trigger_usage(string_t *dst)
{
	string_cat(dst, "usage: io-mode <io> <pin> trigger <debounce_ms> <action1> <io1> <pin1> [<action2> <io2> <pin2>]\n");
	string_cat(dst, "action: ");
	trigger_actions_to_string(dst);
}

irom static bool pin_flag_from_string(const string_t *flag, io_config_pin_entry_t *pin_config, int value)
{
	if(string_match(flag, "autostart"))
		pin_config->flags.autostart = value;
	else if(string_match(flag, "repeat"))
		pin_config->flags.repeat = value;
	else if(string_match(flag, "pullup"))
		pin_config->flags.pullup = value;
	else if(string_match(flag, "reset-on-read"))
		pin_config->flags.reset_on_read = value;
	else
		return(false);

	return(true);
}

irom static void pin_string_from_flags(string_t *flags, const io_config_pin_entry_t *pin_config)
{
	bool none = true;

	if(pin_config->flags.autostart)
	{
		none = false;
		string_cat(flags, " autostart");
	}

	if(pin_config->flags.repeat)
	{
		none = false;
		string_cat(flags, " repeat");
	}

	if(pin_config->flags.pullup)
	{
		none = false;
		string_cat(flags, " pullup");
	}

	if(pin_config->flags.reset_on_read)
	{
		none = false;
		string_cat(flags, " reset-on-read");
	}

	if(none)
		string_cat(flags, " <none>");
}

irom static io_error_t io_read_pin_x(string_t *errormsg, const io_info_entry_t *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin, int *value)
{
	io_error_t error;

	switch(pin_config->mode)
	{
		case(io_pin_disabled):
		case(io_pin_error):
		{
			if(errormsg)
				string_cat(errormsg, "cannot read from this pin");

			return(io_error);
		}

		case(io_pin_input_digital):
		case(io_pin_counter):
		case(io_pin_output_digital):
		case(io_pin_timer):
		case(io_pin_input_analog):
		case(io_pin_output_analog):
		case(io_pin_i2c):
		case(io_pin_uart):
		case(io_pin_lcd):
		case(io_pin_trigger):
		{
			if((error = info->read_pin_fn(errormsg, info, pin_data, pin_config, pin, value)) != io_ok)
				return(error);

			break;
		}
	}

	return(io_ok);
}

irom static io_error_t io_write_pin_x(string_t *errormsg, const io_info_entry_t *info, io_data_pin_entry_t *pin_data, io_config_pin_entry_t *pin_config, int pin, int value)
{
	io_error_t error;

	switch(pin_config->mode)
	{
		case(io_pin_disabled):
		case(io_pin_input_digital):
		case(io_pin_input_analog):
		case(io_pin_i2c):
		case(io_pin_uart):
		case(io_pin_error):
		case(io_pin_trigger):
		{
			if(errormsg)
				string_cat(errormsg, "cannot write to this pin");

			return(io_error);
		}

		case(io_pin_counter):
		case(io_pin_output_digital):
		case(io_pin_lcd):
		case(io_pin_timer):
		case(io_pin_output_analog):
		{
			if((error = info->write_pin_fn(errormsg, info, pin_data, pin_config, pin, value)) != io_ok)
				return(error);

			break;
		}
	}

	return(io_ok);
}

irom static io_error_t io_trigger_pin_x(string_t *errormsg, const io_info_entry_t *info, io_data_pin_entry_t *pin_data, io_config_pin_entry_t *pin_config, int pin, io_trigger_t trigger_type)
{
	io_error_t error;
	int value = 0, old_value, trigger;

	switch(pin_config->mode)
	{
		case(io_pin_disabled):
		case(io_pin_input_digital):
		case(io_pin_input_analog):
		case(io_pin_i2c):
		case(io_pin_uart):
		case(io_pin_error):
		{
			if(errormsg)
				string_cat(errormsg, "cannot trigger this pin");

			return(io_error);
		}

		case(io_pin_output_digital):
		case(io_pin_lcd):
		{
			switch(trigger_type)
			{
				case(io_trigger_down):
				{
					if((error = info->write_pin_fn(errormsg, info, pin_data, pin_config, pin, 0)) != io_ok)
						return(error);

					break;
				}

				case(io_trigger_up):
				{
					if((error = info->write_pin_fn(errormsg, info, pin_data, pin_config, pin, 1)) != io_ok)
						return(error);

					break;
				}

				default:
				{
					if(errormsg)
						string_cat(errormsg, "invalid trigger type");

					return(io_error);
				}
			}

			break;
		}

		case(io_pin_counter):
		{
			switch(trigger_type)
			{
				case(io_trigger_down):
				{
					if((error = info->read_pin_fn(errormsg, info, pin_data, pin_config, pin, &value)) != io_ok)
						return(error);

					value--;

					if((error = info->write_pin_fn(errormsg, info, pin_data, pin_config, pin, value)) != io_ok)
						return(error);

					break;
				}

				case(io_trigger_up):
				{
					if((error = info->read_pin_fn(errormsg, info, pin_data, pin_config, pin, &value)) != io_ok)
						return(error);

					value++;

					if((error = info->write_pin_fn(errormsg, info, pin_data, pin_config, pin, value)) != io_ok)
						return(error);

					break;
				}

				default:
				{
					if(errormsg)
						string_cat(errormsg, "invalid trigger type");

					return(io_error);
				}
			}

			break;
		}

		case(io_pin_timer):
		{
			switch(trigger_type)
			{
				case(io_trigger_off):
				{
					value = pin_config->direction == io_dir_up ? 1 : 0;

					if((error = info->write_pin_fn(errormsg, info, pin_data, pin_config, pin, value)) != io_ok)
						return(error);

					pin_data->speed = 0;
					pin_data->direction = io_dir_none;

					break;
				}

				case(io_trigger_on):
				{
					value = pin_config->direction == io_dir_up ? 0 : 1;

					if((error = info->write_pin_fn(errormsg, info, pin_data, pin_config, pin, value)) != io_ok)
						return(error);

					pin_data->speed = pin_config->speed;
					pin_data->direction = pin_config->direction;

					break;
				}

				default:
				{
					if(errormsg)
						string_cat(errormsg, "invalid trigger type");

					return(io_error);
				}
			}

			break;
		}

		case(io_pin_output_analog):
		{
			if((error = info->read_pin_fn(errormsg, info, pin_data, pin_config, pin, &value)) != io_ok)
				return(error);

			if(trigger_type == io_trigger_toggle)
			{
				if(value == 0)
					trigger_type = io_trigger_on;
				else
					trigger_type = io_trigger_off;
			}

			switch(trigger_type)
			{
				case(io_trigger_off):
				{
					pin_data->direction = io_dir_none;
					pin_data->speed = 0;
					pin_data->saved_value = value;
					value = 0;

					break;
				}

				case(io_trigger_on):
				{
					if(pin_data->saved_value > 0)
						value = pin_data->saved_value;
					else
						value = pin_config->shared.output_analog.upper_bound;

					pin_data->saved_value = 0;

					break;
				}

				case(io_trigger_stop):
				{
					pin_data->direction = io_dir_none;
					pin_data->speed = 0;

					break;
				}

				case(io_trigger_start):
				{
					pin_data->direction = io_dir_up;
					pin_data->speed = pin_config->speed;
					value = pin_data->saved_value;
					pin_data->saved_value = 0;

					break;
				}

				case(io_trigger_down):
				{
					old_value = value;

					value /= (pin_config->speed / 10000.0) + 1;

					if((old_value == value) && (value > 0))
						value--;

					if(value <= pin_config->shared.output_analog.lower_bound)
					{
						if(pin_config->flags.repeat && (pin_data->direction == io_dir_down))
							pin_data->direction = io_dir_up;
						else
						{
							value = 0;
							pin_data->direction = io_dir_none;
						}
					}

					break;
				}

				case(io_trigger_up):
				{
					if(value < pin_config->shared.output_analog.lower_bound)
						value = pin_config->shared.output_analog.lower_bound;
					else
					{
						old_value = value;

						value *= (pin_config->speed / 10000.0) + 1;

						if(old_value == value)
							value++;
					}

					if(value >= pin_config->shared.output_analog.upper_bound)
					{
						value = pin_config->shared.output_analog.upper_bound;

						if(pin_data->direction == io_dir_up)
							pin_data->direction = io_dir_down;
					}

					break;
				}

				default:
				{
					if(errormsg)
						string_cat(errormsg, "invalid trigger type");

					return(io_error);
				}
			}

			if((error = info->write_pin_fn(errormsg, info, pin_data, pin_config, pin, value)) != io_ok)
				return(error);

			break;
		}

		case(io_pin_trigger):
		{
			for(trigger = 0; trigger < 2; trigger++)
			{
				if((pin_config->shared.trigger[trigger].io.io >= 0) &&
						(pin_config->shared.trigger[trigger].io.pin >= 0) &&
						(pin_config->shared.trigger[trigger].action != io_trigger_off))
				{
					if(io_trigger_pin(errormsg,
							pin_config->shared.trigger[trigger].io.io,
							pin_config->shared.trigger[trigger].io.pin,
							pin_config->shared.trigger[trigger].action) != io_ok)
						return(io_error);
				}
			}

			break;
		}
	}

	return(io_ok);
}

irom io_error_t io_read_pin(string_t *error_msg, int io, int pin, int *value)
{
	const io_info_entry_t *info;
	io_data_entry_t *data;
	io_config_pin_entry_t *pin_config;
	io_data_pin_entry_t *pin_data;
	io_error_t error;

	if(io >= io_id_size)
	{
		if(error_msg)
			string_cat(error_msg, "io out of range\n");
		return(io_error);
	}

	info = &io_info[io];
	data = &io_data[io];

	if(pin >= info->pins)
	{
		if(error_msg)
			string_cat(error_msg, "pin out of range\n");
		return(io_error);
	}

	pin_config = &io_config[io][pin];
	pin_data = &data->pin[pin];

	if(((error = io_read_pin_x(error_msg, info, pin_data, pin_config, pin, value)) != io_ok) && error_msg)
		string_cat(error_msg, "\n");
	else
		if((pin_config->mode == io_pin_counter) && (pin_config->flags.reset_on_read))
			error = io_write_pin_x(error_msg, info, pin_data, pin_config, pin, 0);

	return(error);
}

irom io_error_t io_write_pin(string_t *error, int io, int pin, int value)
{
	const io_info_entry_t *info;
	io_data_entry_t *data;
	io_config_pin_entry_t *pin_config;
	io_data_pin_entry_t *pin_data;

	if(io >= io_id_size)
	{
		if(error)
			string_cat(error, "io out of range\n");
		return(io_error);
	}

	info = &io_info[io];
	data = &io_data[io];

	if(pin >= info->pins)
	{
		if(error)
			string_cat(error, "pin out of range\n");
		return(io_error);
	}

	pin_config = &io_config[io][pin];
	pin_data = &data->pin[pin];

	return(io_write_pin_x(error, info, pin_data, pin_config, pin, value));
}

irom io_error_t io_trigger_pin(string_t *error, int io, int pin, io_trigger_t trigger_type)
{
	const io_info_entry_t *info;
	io_data_entry_t *data;
	io_config_pin_entry_t *pin_config;
	io_data_pin_entry_t *pin_data;

	if(io >= io_id_size)
	{
		if(error)
			string_cat(error, "io out of range\n");
		return(io_error);
	}

	info = &io_info[io];
	data = &io_data[io];

	if(pin >= info->pins)
	{
		if(error)
			string_cat(error, "pin out of range\n");
		return(io_error);
	}

	pin_config = &io_config[io][pin];
	pin_data = &data->pin[pin];

	return(io_trigger_pin_x(error, info, pin_data, pin_config, pin, trigger_type));
}

irom void io_init(void)
{
	const io_info_entry_t *info;
	io_data_entry_t *data;
	io_config_pin_entry_t *pin_config;
	io_data_pin_entry_t *pin_data;
	io_pin_flag_to_int_t flags;
	int io, pin, mode, llmode;
	int i2c_sda = -1;
	int i2c_scl = -1;
	int trigger;

	for(io = 0; io < io_id_size; io++)
	{
		info = &io_info[io];
		data = &io_data[io];

		for(pin = 0; pin < info->pins; pin++)
		{
			pin_data = &data->pin[pin];
			pin_data->direction = io_dir_none;
			pin_data->speed = 0;
			pin_data->saved_value = 0;

			pin_config = &io_config[io][pin];

			if(!config_get_int("io.%u.%u.mode", io, pin, &mode))
			{
				pin_config->mode = io_pin_disabled;
				pin_config->llmode = io_pin_ll_disabled;
				continue;
			}

			if(!config_get_int("io.%u.%u.llmode", io, pin, &llmode))
			{
				pin_config->mode = io_pin_disabled;
				pin_config->llmode = io_pin_ll_disabled;
				continue;
			}

			if(!config_get_int("io.%u.%u.flags", io, pin, &flags.intvalue))
				flags.intvalue = 0;

			pin_config->flags = flags.io_pin_flags;

			pin_config->mode = mode;
			pin_config->llmode = llmode;

			switch(mode)
			{
				case(io_pin_disabled):
				case(io_pin_error):
				case(io_pin_input_digital):
				case(io_pin_output_digital):
				case(io_pin_input_analog):
				case(io_pin_uart):
				{
					break;
				}

				case(io_pin_counter):
				{
					int debounce;

					if(!config_get_int("io.%u.%u.counter.debounce", io, pin, &debounce))
					{
						pin_config->mode = io_pin_disabled;
						pin_config->llmode = io_pin_ll_disabled;
						continue;
					}

					pin_config->speed = debounce;

					break;
				}

				case(io_pin_trigger):
				{
					int debounce, trigger_io, trigger_pin, trigger_type;

					if(!info->caps.counter)
					{
						pin_config->mode = io_pin_disabled;
						pin_config->llmode = io_pin_ll_disabled;
						continue;
					}

					if(!config_get_int("io.%u.%u.trigger.debounce", io, pin, &debounce))
					{
						pin_config->mode = io_pin_disabled;
						pin_config->llmode = io_pin_ll_disabled;
						continue;
					}

					pin_config->speed = debounce;

					for(trigger = 0; trigger < max_triggers_per_pin; trigger++)
					{
						pin_config->shared.trigger[trigger].io.io = -1;
						pin_config->shared.trigger[trigger].io.pin = -1;
						pin_config->shared.trigger[trigger].action = io_trigger_none;
					}

					if(config_get_int("io.%u.%u.trigger.io", io, pin, &trigger_io) &&
						config_get_int("io.%u.%u.trigger.pin", io, pin, &trigger_pin) &&
						config_get_int("io.%u.%u.trigger.type", io, pin, &trigger_type))
					{
						pin_config->shared.trigger[0].io.io = trigger_io;
						pin_config->shared.trigger[0].io.pin = trigger_pin;
						pin_config->shared.trigger[0].action = trigger_type;
					}

					if(config_get_int("io.%u.%u.trigger.0.io", io, pin, &trigger_io) &&
						config_get_int("io.%u.%u.trigger.0.pin", io, pin, &trigger_pin) &&
						config_get_int("io.%u.%u.trigger.0.type", io, pin, &trigger_type))
					{
						pin_config->shared.trigger[0].io.io = trigger_io;
						pin_config->shared.trigger[0].io.pin = trigger_pin;
						pin_config->shared.trigger[0].action = trigger_type;
					}

					if(config_get_int("io.%u.%u.trigger.1.io", io, pin, &trigger_io) &&
						config_get_int("io.%u.%u.trigger.1.pin", io, pin, &trigger_pin) &&
						config_get_int("io.%u.%u.trigger.1.type", io, pin, &trigger_type))
					{
						pin_config->shared.trigger[1].io.io = trigger_io;
						pin_config->shared.trigger[1].io.pin = trigger_pin;
						pin_config->shared.trigger[1].action = trigger_type;
					}

					break;
				}

				case(io_pin_timer):
				{
					int direction, speed;

					if(!info->caps.output_digital)
					{
						pin_config->mode = io_pin_disabled;
						pin_config->llmode = io_pin_ll_disabled;
						continue;
					}

					if(!config_get_int("io.%u.%u.timer.delay", io, pin, &speed))
					{
						pin_config->mode = io_pin_disabled;
						pin_config->llmode = io_pin_ll_disabled;
						continue;
					}

					if(!config_get_int("io.%u.%u.timer.direction", io, pin, &direction))
					{
						pin_config->mode = io_pin_disabled;
						pin_config->llmode = io_pin_ll_disabled;
						continue;
					}

					pin_config->speed = speed;
					pin_config->direction = direction;

					break;
				}


				case(io_pin_output_analog):
				{
					int speed, lower_bound, upper_bound;

					if(!info->caps.output_analog)
					{
						pin_config->mode = io_pin_disabled;
						pin_config->llmode = io_pin_ll_disabled;
						continue;
					}

					if(!config_get_int("io.%u.%u.outputa.speed", io, pin, &speed))
					{
						pin_config->mode = io_pin_disabled;
						pin_config->llmode = io_pin_ll_disabled;
						continue;
					}

					if(!config_get_int("io.%u.%u.outputa.lower", io, pin, &lower_bound))
					{
						pin_config->mode = io_pin_disabled;
						pin_config->llmode = io_pin_ll_disabled;
						continue;
					}

					if(!config_get_int("io.%u.%u.outputa.upper", io, pin, &upper_bound))
					{
						pin_config->mode = io_pin_disabled;
						pin_config->llmode = io_pin_ll_disabled;
						continue;
					}

					pin_config->shared.output_analog.lower_bound = lower_bound;
					pin_config->shared.output_analog.upper_bound = upper_bound;
					pin_config->speed = speed;

					llmode = io_pin_ll_output_analog;

					break;
				}

				case(io_pin_i2c):
				{
					int pin_mode;

					if(!info->caps.i2c)
					{
						pin_config->mode = io_pin_disabled;
						pin_config->llmode = io_pin_ll_disabled;
						continue;
					}

					if(!config_get_int("io.%u.%u.i2c.pinmode", io, pin, &pin_mode))
					{
						pin_config->mode = io_pin_disabled;
						pin_config->llmode = io_pin_ll_disabled;
						continue;
					}

					pin_config->shared.i2c.pin_mode = pin_mode;

					break;
				}


				case(io_pin_lcd):
				{
					int pin_mode;

					if(!config_get_int("io.%u.%u.lcd.pin", io, pin, &pin_mode))
					{
						pin_config->mode = io_pin_disabled;
						pin_config->llmode = io_pin_ll_disabled;
						continue;
					}

					pin_config->shared.lcd.pin_use = pin_mode;

					break;
				}
			}
		}

		if(info->init_fn(info) == io_ok)
		{
			data->detected = true;

			for(pin = 0; pin < info->pins; pin++)
			{
				pin_config = &io_config[io][pin];
				pin_data = &data->pin[pin];

				if(info->init_pin_mode_fn((string_t *)0, info, pin_data, pin_config, pin) == io_ok)
				{
					switch(pin_config->mode)
					{
						case(io_pin_disabled):
						case(io_pin_input_digital):
						case(io_pin_counter):
						case(io_pin_input_analog):
						case(io_pin_uart):
						case(io_pin_trigger):
						case(io_pin_error):
						{
							break;
						}

						case(io_pin_output_digital):
						case(io_pin_lcd):
						case(io_pin_timer):
						case(io_pin_output_analog):
						{
							io_trigger_pin_x((string_t *)0, info, pin_data, pin_config, pin,
									pin_config->flags.autostart ? io_trigger_on : io_trigger_off);
							break;
						}

						case(io_pin_i2c):
						{
							if(pin_config->shared.i2c.pin_mode == io_i2c_sda)
								i2c_sda = pin;

							if(pin_config->shared.i2c.pin_mode == io_i2c_scl)
								i2c_scl = pin;

							if((i2c_sda >= 0) && (i2c_scl >= 0))
								i2c_init(i2c_sda, i2c_scl);

							break;
						}
					}
				}
			}
		}
	}
}

iram void io_periodic(void)
{
	const io_info_entry_t *info;
	io_data_entry_t *data;
	io_config_pin_entry_t *pin_config;
	io_data_pin_entry_t *pin_data;
	int io, pin;
	int trigger_status_io, trigger_status_pin;
	io_flags_t flags = { .counter_triggered = 0 };
	int value;
	int trigger;

	for(io = 0; io < io_id_size; io++)
	{
		info = &io_info[io];
		data = &io_data[io];

		if(!data->detected)
			continue;

		if(info->periodic_fn)
			info->periodic_fn(io, info, data, &flags);

		for(pin = 0; pin < info->pins; pin++)
		{
			pin_config = &io_config[io][pin];
			pin_data = &data->pin[pin];

			switch(pin_config->mode)
			{
				case(io_pin_disabled):
				case(io_pin_input_digital):
				case(io_pin_counter):
				case(io_pin_output_digital):
				case(io_pin_input_analog):
				case(io_pin_i2c):
				case(io_pin_uart):
				case(io_pin_lcd):
				case(io_pin_error):
				{
					break;
				}

				case(io_pin_timer):
				{
					if((pin_data->direction != io_dir_none) && (pin_data->speed >= 10) && ((pin_data->speed -= 10) <= 0))
					{
						switch(pin_data->direction)
						{
							case(io_dir_none):
							{
								break;
							}

							case(io_dir_up):
							{
								info->write_pin_fn((string_t *)0, info, pin_data, pin_config, pin, 1);
								pin_data->direction = io_dir_down;
								break;
							}

							case(io_dir_down):
							{
								info->write_pin_fn((string_t *)0, info, pin_data, pin_config, pin, 0);
								pin_data->direction = io_dir_up;
								break;
							}
						}

						if(pin_config->flags.repeat)
							pin_data->speed = pin_config->speed;
						else
						{
							pin_data->speed = 0;
							pin_data->direction = io_dir_none;
						}
					}

					break;
				}

				case(io_pin_trigger):
				{
					if((info->read_pin_fn((string_t *)0, info, pin_data, pin_config, pin, &value) == io_ok) && (value != 0))
					{
						for(trigger = 0; trigger < max_triggers_per_pin; trigger++)
							io_trigger_pin((string_t *)0,
									pin_config->shared.trigger[trigger].io.io,
									pin_config->shared.trigger[trigger].io.pin,
									pin_config->shared.trigger[trigger].action);

						info->write_pin_fn((string_t *)0, info, pin_data, pin_config, pin, 0);
					}

					break;
				}

				case(io_pin_output_analog):
				{
					if((pin_config->shared.output_analog.upper_bound > pin_config->shared.output_analog.lower_bound) &&
							(pin_config->speed > 0) && (pin_data->direction != io_dir_none))
						io_trigger_pin_x((string_t *)0, info, pin_data, pin_config, pin,
								(pin_data->direction == io_dir_up) ? io_trigger_up : io_trigger_down);

					break;
				}
			}
		}
	}

	if(flags.counter_triggered &&
			config_get_int("trigger.status.io", -1, -1, &trigger_status_io) &&
			config_get_int("trigger.status.pin", -1, -1, &trigger_status_pin) &&
			(trigger_status_io >= 0) && (trigger_status_pin >= 0))
	{
		io_trigger_pin((string_t *)0, trigger_status_io, trigger_status_pin, io_trigger_on);
	}
}

/* app commands */

irom app_action_t application_function_io_mode(const string_t *src, string_t *dst)
{
	const io_info_entry_t	*info;
	io_data_entry_t			*data;
	io_config_pin_entry_t	*pin_config;
	io_data_pin_entry_t		*pin_data;
	io_pin_mode_t			mode;
	io_pin_ll_mode_t		llmode;
	int io, pin;

	if(parse_int(1, src, &io, 0) != parse_ok)
	{
		io_config_dump(dst, -1, -1, false);
		return(app_action_normal);
	}

	if((io < 0) || (io >= io_id_size))
	{
		string_format(dst, "invalid io %d\n", io);
		return(app_action_error);
	}

	info = &io_info[io];
	data = &io_data[io];

	if(!data->detected)
	{
		string_format(dst, "io %d not detected\n", io);
		return(app_action_error);
	}

	if(parse_int(2, src, &pin, 0) != parse_ok)
	{
		io_config_dump(dst, io, -1, false);
		return(app_action_normal);
	}

	if((pin < 0) || (pin >= info->pins))
	{
		string_cat(dst, "io pin out of range\n");
		return(app_action_error);
	}

	pin_config = &io_config[io][pin];
	pin_data = &data->pin[pin];

	if(parse_string(3, src, dst) != parse_ok)
	{
		string_clear(dst);
		io_config_dump(dst, io, pin, false);
		return(app_action_normal);
	}

	if((mode = io_mode_from_string(dst)) == io_pin_error)
	{
		string_copy(dst, "invalid mode\n");
		return(app_action_error);
	}

	string_clear(dst);

	llmode = io_pin_ll_error;

	switch(mode)
	{
		case(io_pin_input_digital):
		{
			if(!info->caps.input_digital)
			{
				string_cat(dst, "digital input mode invalid for this io\n");
				return(app_action_error);
			}

			llmode = io_pin_ll_input_digital;

			config_delete("io.%u.%u.", io, pin, true);
			config_set_int("io.%u.%u.mode", io, pin, mode);
			config_set_int("io.%u.%u.llmode", io, pin, io_pin_ll_input_digital);

			break;
		}

		case(io_pin_counter):
		{
			if(!info->caps.counter)
			{
				string_cat(dst, "counter mode invalid for this io\n");
				return(app_action_error);
			}

			int debounce;

			if((parse_int(4, src, &debounce, 0) != parse_ok))
			{
				string_cat(dst, "counter: <debounce ms>\n");
				return(app_action_error);
			}

			pin_config->speed = debounce;
			llmode = io_pin_ll_counter;

			config_delete("io.%u.%u.", io, pin, true);
			config_set_int("io.%u.%u.mode", io, pin, mode);
			config_set_int("io.%u.%u.llmode", io, pin, io_pin_ll_counter);
			config_set_int("io.%u.%u.counter.debounce", io, pin, debounce);

			break;
		}

		case(io_pin_trigger):
		{
			int debounce, trigger_io, trigger_pin;
			io_trigger_t trigger_type;

			if(!info->caps.counter)
			{
				string_cat(dst, "trigger mode invalid for this io\n");
				return(app_action_error);
			}

			if((parse_int(4, src, &debounce, 0) != parse_ok))
			{
				iomode_trigger_usage(dst);
				return(app_action_error);
			}

			pin_config->speed = debounce;

			if((parse_string(5, src, dst) != parse_ok))
			{
				string_clear(dst);
				iomode_trigger_usage(dst);
				return(app_action_error);
			}

			string_clear(dst);

			if((trigger_type = string_to_trigger_action(dst)) == io_trigger_error)
			{
				string_clear(dst);
				iomode_trigger_usage(dst);
				return(app_action_error);
			}

			if((parse_int(6, src, &trigger_io, 0) != parse_ok))
			{
				iomode_trigger_usage(dst);
				return(app_action_error);
			}

			if((parse_int(7, src, &trigger_pin, 0) != parse_ok))
			{
				iomode_trigger_usage(dst);
				return(app_action_error);
			}

			pin_config->shared.trigger[0].io.io = trigger_io;
			pin_config->shared.trigger[0].io.pin = trigger_pin;
			pin_config->shared.trigger[0].action = trigger_type;

			pin_config->shared.trigger[1].io.io = -1;
			pin_config->shared.trigger[1].io.pin = -1;
			pin_config->shared.trigger[1].action = io_trigger_none;

			if((parse_string(8, src, dst) != parse_ok))
			{
				string_clear(dst);
				goto skip;
			}

			string_clear(dst);

			if((trigger_type = string_to_trigger_action(dst)) == io_trigger_error)
			{
				string_clear(dst);
				goto skip;
			}

			if((parse_int(9, src, &trigger_io, 0) != parse_ok))
				goto skip;

			if((parse_int(10, src, &trigger_pin, 0) != parse_ok))
				goto skip;

			pin_config->shared.trigger[1].io.io = trigger_io;
			pin_config->shared.trigger[1].io.pin = trigger_pin;
			pin_config->shared.trigger[1].action = trigger_type;

skip:
			llmode = io_pin_ll_counter;

			config_delete("io.%u.%u.", io, pin, true);
			config_set_int("io.%u.%u.mode", io, pin, mode);
			config_set_int("io.%u.%u.llmode", io, pin, io_pin_ll_counter);
			config_set_int("io.%u.%u.trigger.debounce", io, pin, debounce);

			config_set_int("io.%u.%u.trigger.0.io", io, pin, pin_config->shared.trigger[0].io.io);
			config_set_int("io.%u.%u.trigger.0.pin", io, pin, pin_config->shared.trigger[0].io.pin);
			config_set_int("io.%u.%u.trigger.0.type", io, pin, pin_config->shared.trigger[0].action);

			if((pin_config->shared.trigger[1].io.io >= 0) &&
				(pin_config->shared.trigger[1].io.pin >= 0) &&
				(pin_config->shared.trigger[1].action != io_trigger_none))
			{
				config_set_int("io.%u.%u.trigger.1.io", io, pin, pin_config->shared.trigger[1].io.io);
				config_set_int("io.%u.%u.trigger.1.pin", io, pin, pin_config->shared.trigger[1].io.pin);
				config_set_int("io.%u.%u.trigger.1.type", io, pin, pin_config->shared.trigger[1].action);
			}

			break;
		}

		case(io_pin_output_digital):
		{
			if(!info->caps.output_digital)
			{
				string_cat(dst, "digital output mode invalid for this io\n");
				return(app_action_error);
			}

			llmode = io_pin_ll_output_digital;

			config_delete("io.%u.%u.", io, pin, true);
			config_set_int("io.%u.%u.mode", io, pin, mode);
			config_set_int("io.%u.%u.llmode", io, pin, io_pin_ll_output_digital);

			break;
		}

		case(io_pin_timer):
		{
			io_direction_t direction;
			int speed;

			if(!info->caps.output_digital)
			{
				string_cat(dst, "timer mode invalid for this io\n");
				return(app_action_error);
			}

			if(parse_string(4, src, dst) != parse_ok)
			{
				string_copy(dst, "timer: <direction>:up/down <speed>:ms\n");
				return(app_action_error);
			}

			if(string_match(dst, "up"))
				direction = io_dir_up;
			else if(string_match(dst, "down"))
				direction = io_dir_down;
			else
			{
				string_cat(dst, ": timer direction invalid\n");
				return(app_action_error);
			}

			string_clear(dst);

			if((parse_int(5, src, &speed, 0) != parse_ok))
			{
				string_copy(dst, "timer: <direction>:up/down <speed>:ms\n");
				return(app_action_error);
			}

			if(speed < 10)
			{
				string_cat(dst, "timer: speed too small: must be >= 10 ms\n");
				return(app_action_error);
			}

			pin_config->direction = direction;
			pin_config->speed = speed;

			llmode = io_pin_ll_output_digital;

			config_delete("io.%u.%u.", io, pin, true);
			config_set_int("io.%u.%u.mode", io, pin, mode);
			config_set_int("io.%u.%u.llmode", io, pin, io_pin_ll_output_digital);
			config_set_int("io.%u.%u.timer.direction", io, pin, direction);
			config_set_int("io.%u.%u.timer.delay", io, pin, speed);

			break;
		}

		case(io_pin_input_analog):
		{
			if(!info->caps.input_analog)
			{
				string_cat(dst, "analog input mode invalid for this io\n");
				return(app_action_error);
			}

			llmode = io_pin_ll_input_analog;

			config_delete("io.%u.%u.", io, pin, true);
			config_set_int("io.%u.%u.mode", io, pin, mode);
			config_set_int("io.%u.%u.llmode", io, pin, io_pin_ll_input_analog);

			break;
		}

		case(io_pin_output_analog):
		{
			int lower_bound = 0;
			int upper_bound = 0;
			int speed = 0;

			if(!info->caps.output_analog)
			{
				string_cat(dst, "analog output mode invalid for this io\n");
				return(app_action_error);
			}

			parse_int(4, src, &lower_bound, 0);
			parse_int(5, src, &upper_bound, 0);
			parse_int(6, src, &speed, 0);

			if((lower_bound < 0) || (lower_bound > 65535))
			{
				string_format(dst, "outputa: lower bound out of range: %d\n", lower_bound);
				return(app_action_error);
			}

			if(upper_bound == 0)
				upper_bound = lower_bound;

			if((upper_bound < 0) || (upper_bound > 65535))
			{
				string_format(dst, "outputa: upper bound out of range: %d\n", upper_bound);
				return(app_action_error);
			}

			if(upper_bound < lower_bound)
			{
				string_cat(dst, "upper bound below lower bound\n");
				return(app_action_error);
			}

			if((speed < 0) || (speed > 65535))
			{
				string_format(dst, "outputa: speed out of range: %d\n", speed);
				return(app_action_error);
			}

			pin_config->shared.output_analog.lower_bound = lower_bound;
			pin_config->shared.output_analog.upper_bound = upper_bound;
			pin_config->speed = speed;

			llmode = io_pin_ll_output_analog;

			config_delete("io.%u.%u.", io, pin, true);
			config_set_int("io.%u.%u.mode", io, pin, mode);
			config_set_int("io.%u.%u.llmode", io, pin, io_pin_ll_output_analog);
			config_set_int("io.%u.%u.outputa.lower", io, pin, lower_bound);
			config_set_int("io.%u.%u.outputa.upper", io, pin, upper_bound);
			config_set_int("io.%u.%u.outputa.speed", io, pin, speed);

			break;
		}

		case(io_pin_i2c):
		{
			io_i2c_t pin_mode;

			if(!info->caps.i2c)
			{
				string_cat(dst, "i2c mode invalid for this io\n");
				return(app_action_error);
			}

			if(parse_string(4, src, dst) != parse_ok)
			{
				string_copy(dst, "i2c: <pin mode>=sda|scl\n");
				return(app_action_error);
			}

			if((pin_mode = io_i2c_pin_from_string(dst)) == io_i2c_error)
			{
				string_copy(dst, "i2c: <pin mode>=sda|scl\n");
				return(app_action_error);
			}

			string_clear(dst);

			pin_config->shared.i2c.pin_mode = pin_mode;

			llmode = io_pin_ll_i2c;

			config_delete("io.%u.%u.", io, pin, true);
			config_set_int("io.%u.%u.mode", io, pin, mode);
			config_set_int("io.%u.%u.llmode", io, pin, io_pin_ll_i2c);
			config_set_int("io.%u.%u.i2c.pinmode", io, pin, pin_mode);

			break;
		}

		case(io_pin_uart):
		{
			if(!info->caps.uart)
			{
				string_cat(dst, "uart mode invalid for this io\n");
				return(app_action_error);
			}

			llmode = io_pin_ll_uart;

			config_delete("io.%u.%u.", io, pin, true);
			config_set_int("io.%u.%u.mode", io, pin, mode);
			config_set_int("io.%u.%u.llmode", io, pin, io_pin_ll_uart);

			break;
		}

		case(io_pin_lcd):
		{
			io_lcd_mode_t pin_mode;

			if(parse_string(4, src, dst) != parse_ok)
			{
				string_copy(dst, "lcd: <pin use>=rs|rw|e|d0|d1|d2|d3|d4|d5|d6|d7|bl\n");
				return(app_action_error);
			}

			if((pin_mode = io_lcd_mode_from_string(dst)) == io_lcd_error)
			{
				string_copy(dst, "lcd: <pin use>=rs|rw|e|d0|d1|d2|d3|d4|d5|d6|d7|bl\n");
				return(app_action_error);
			}

			string_clear(dst);

			if(pin_mode == io_lcd_bl) // backlight
			{
				if(info->caps.output_analog)
					llmode = io_pin_ll_output_analog;
				else
					if(info->caps.output_digital)
						llmode = io_pin_ll_output_digital;
					else
					{
						string_cat(dst, "analog/digital output mode invalid for this io\n");
						return(app_action_error);
					}
			}
			else
			{
				if(!info->caps.output_digital)
				{
					string_cat(dst, "digital output mode invalid for this io\n");
					return(app_action_error);
				}

				llmode = io_pin_ll_output_digital;
			}

			pin_config->shared.lcd.pin_use = pin_mode;

			config_delete("io.%u.%u.", io, pin, true);
			config_set_int("io.%u.%u.mode", io, pin, mode);
			config_set_int("io.%u.%u.llmode", io, pin, llmode);
			config_set_int("io.%u.%u.lcd.pin", io, pin, pin_mode);

			break;
		}

		case(io_pin_disabled):
		{
			llmode = io_pin_ll_disabled;

			config_delete("io.%u.%u.", io, pin, true);

			break;
		}

		case(io_pin_error):
		{
			llmode = io_pin_ll_error;

			string_cat(dst, "unsupported io mode\n");
			return(app_action_error);
		}
	}

	if((mode == io_pin_error) || (llmode == io_pin_ll_error))
	{
		string_cat(dst, "error\n");
		return(app_action_error);
	}

	pin_config->mode = mode;
	pin_config->llmode = llmode;

	if(info->init_pin_mode_fn && (info->init_pin_mode_fn(dst, info, pin_data, pin_config, pin) != io_ok))
	{
		pin_config->mode = io_pin_disabled;
		pin_config->llmode = io_pin_ll_disabled;
		return(app_action_error);
	}

	io_config_dump(dst, io, pin, false);

	return(app_action_normal);
}

irom app_action_t application_function_io_read(const string_t *src, string_t *dst)
{
	const io_info_entry_t *info;
	io_config_pin_entry_t *pin_config;
	int io, pin, value;

	if(parse_int(1, src, &io, 0) != parse_ok)
	{
		string_cat(dst, "io-read: <io> <pin>\n");
		return(app_action_error);
	}

	if((io < 0) || (io >= io_id_size))
	{
		string_format(dst, "invalid io %d\n", io);
		return(app_action_error);
	}

	info = &io_info[io];

	if(parse_int(2, src, &pin, 0) != parse_ok)
	{
		string_cat(dst, "get: <io> <pin>\n");
		return(app_action_error);
	}

	if((pin < 0) || (pin >= info->pins))
	{
		string_cat(dst, "io pin out of range\n");
		return(app_action_error);
	}

	pin_config = &io_config[io][pin];

	io_string_from_mode(dst, pin_config->mode);

	if(pin_config->mode == io_pin_i2c)
	{
		string_cat(dst, "/");
		io_string_from_i2c_type(dst, pin_config->shared.i2c.pin_mode);
	}

	if(pin_config->mode == io_pin_lcd)
	{
		string_cat(dst, "/");
		io_string_from_lcd_mode(dst, pin_config->shared.lcd.pin_use);
	}

	string_cat(dst, ": ");

	if(io_read_pin(dst, io, pin, &value) != io_ok)
		return(app_action_error);

	string_format(dst, "[%d]\n", value);

	return(app_action_normal);
}

irom app_action_t application_function_io_write(const string_t *src, string_t *dst)
{
	const io_info_entry_t *info;
	io_config_pin_entry_t *pin_config;
	int io, pin, value;

	if(parse_int(1, src, &io, 0) != parse_ok)
	{
		string_cat(dst, "io-write <io> <pin> <value>\n");
		return(app_action_error);
	}

	if((io < 0) || (io >= io_id_size))
	{
		string_format(dst, "invalid io %d\n", io);
		return(app_action_error);
	}

	info = &io_info[io];

	if(parse_int(2, src, &pin, 0) != parse_ok)
	{
		string_cat(dst, "io-write <io> <pin> <value>\n");
		return(app_action_error);
	}

	if((pin < 0) || (pin >= info->pins))
	{
		string_cat(dst, "invalid pin\n");
		return(app_action_error);
	}

	pin_config = &io_config[io][pin];

	value = 0;
	parse_int(3, src, &value, 0);

	io_string_from_mode(dst, pin_config->mode);

	if(pin_config->mode == io_pin_lcd)
	{
		string_cat(dst, "/");
		io_string_from_lcd_mode(dst, pin_config->shared.lcd.pin_use);
	}

	string_cat(dst, ": ");

	if(io_write_pin(dst, io, pin, value) != io_ok)
	{
		string_cat(dst, "\n");
		return(app_action_error);
	}

	if(io_read_pin(dst, io, pin, &value) != io_ok)
	{
		string_cat(dst, "\n");
		return(app_action_error);
	}

	string_format(dst, "[%d]\n", value);

	return(app_action_normal);
}

irom app_action_t application_function_io_trigger(const string_t *src, string_t *dst)
{
	const io_info_entry_t *info;
	int io, pin;
	io_trigger_t trigger_type;

	if(parse_int(1, src, &io, 0) != parse_ok)
	{
		string_clear(dst);
		trigger_usage(dst);
		return(app_action_normal);
	}

	if((io < 0) || (io >= io_id_size))
	{
		string_format(dst, "invalid io %d\n", io);
		return(app_action_error);
	}

	info = &io_info[io];

	if(parse_int(2, src, &pin, 0) != parse_ok)
	{
		string_clear(dst);
		trigger_usage(dst);
		return(app_action_normal);
	}

	if((pin < 0) || (pin >= info->pins))
	{
		string_cat(dst, "invalid pin\n");
		return(app_action_error);
	}

	if(parse_string(3, src, dst) != parse_ok)
	{
		string_clear(dst);
		trigger_usage(dst);
		return(app_action_normal);
	}

	if((trigger_type = string_to_trigger_action(dst)) == io_trigger_error)
	{
		string_clear(dst);
		trigger_usage(dst);
		return(app_action_normal);
	}

	string_clear(dst);

	string_cat(dst, "trigger ");
	trigger_action_to_string(dst, trigger_type);
	string_format(dst, " %u/%u: ", io, pin);

	if(io_trigger_pin(dst, io, pin, trigger_type) != io_ok)
	{
		string_cat(dst, "\n");
		return(app_action_error);
	}

	string_cat(dst, "ok\n");

	return(app_action_normal);
}

irom static app_action_t application_function_io_clear_set_flag(const string_t *src, string_t *dst, int value)
{
	const io_info_entry_t *info;
	io_data_entry_t *data;
	io_data_pin_entry_t *pin_data;
	io_config_pin_entry_t *pin_config;
	int io, pin;
	io_pin_flag_t saved_flags;
	io_pin_flag_to_int_t io_pin_flag_to_int;

	if(parse_int(1, src, &io, 0) != parse_ok)
	{
		string_cat(dst, "io-flag <io> <pin> <flag>\n");
		return(app_action_error);
	}

	if((io < 0) || (io >= io_id_size))
	{
		string_format(dst, "invalid io %d\n", io);
		return(app_action_error);
	}

	info = &io_info[io];
	data = &io_data[io];

	if(parse_int(2, src, &pin, 0) != parse_ok)
	{
		string_cat(dst, "io-flag <io> <pin> <flag>\n");
		return(app_action_error);
	}

	if((pin < 0) || (pin >= info->pins))
	{
		string_cat(dst, "invalid pin\n");
		return(app_action_error);
	}

	pin_data = &data->pin[pin];
	pin_config = &io_config[io][pin];

	saved_flags = pin_config->flags;

	if((parse_string(3, src, dst) == parse_ok) && !pin_flag_from_string(dst, pin_config, value))
	{
		string_copy(dst, "io-flag <io> <pin> <flag>\n");
		return(app_action_error);
	}

	if(pin_config->flags.pullup && !info->caps.pullup)
	{
		pin_config->flags = saved_flags;
		string_copy(dst, "io does not support pullup\n");
		return(app_action_error);
	}

	if(info->init_pin_mode_fn && (info->init_pin_mode_fn(dst, info, pin_data, pin_config, pin) != io_ok))
	{
		pin_config->flags = saved_flags;
		string_copy(dst, "cannot enable this flag\n");
		return(app_action_error);
	}

	io_pin_flag_to_int.io_pin_flags = pin_config->flags;
	config_set_int("io.%u.%u.flags", io, pin, io_pin_flag_to_int.intvalue);

	string_clear(dst);
	string_format(dst, "flags for pin %d/%d:", io, pin);

	pin_string_from_flags(dst, pin_config);

	string_cat(dst, "\n");

	return(app_action_normal);
}

irom app_action_t application_function_io_set_flag(const string_t *src, string_t *dst)
{
	return(application_function_io_clear_set_flag(src, dst, 1));
}

irom app_action_t application_function_io_clear_flag(const string_t *src, string_t *dst)
{
	return(application_function_io_clear_set_flag(src, dst, 0));
}

/* dump */

typedef enum
{
	ds_id_io,
	ds_id_pin,
	ds_id_flags_1,
	ds_id_flags_2,
	ds_id_mode,
	ds_id_disabled,
	ds_id_input,
	ds_id_counter,
	ds_id_trigger_1,
	ds_id_trigger_2,
	ds_id_trigger_3,
	ds_id_output,
	ds_id_timer,
	ds_id_analog_output,
	ds_id_i2c_sda,
	ds_id_i2c_scl,
	ds_id_uart,
	ds_id_lcd,
	ds_id_unknown,
	ds_id_not_detected,
	ds_id_info_1,
	ds_id_info_2,
	ds_id_header,
	ds_id_footer,
	ds_id_preline,
	ds_id_postline,
	ds_id_error,

	ds_id_length,
	ds_id_invalid = ds_id_length
} dump_string_id_t;

typedef const char string_array_t[ds_id_length][256];

typedef struct {
	const string_array_t plain;
	const string_array_t html;
} dump_string_t;

static const roflash dump_string_t dump_strings =
{
	.plain =
	{
		"io[%d]: %s@%x",
		"  pin: %2d",
		" flags:",
		", ",
		"mode: ",
		"disabled",
		"input, state: %s",
		"counter, counter: %d, debounce: %d",
		"trigger, counter: %d, debounce: %d\n",
		"            action #%d: %d, io: %d, pin: %d, action: ",
		"\n           ",
		"output, state: %s",
		"timer, config direction: %s, speed: %d ms, current direction: %s, delay: %d ms, state: %s",
		"analog output, min/static: %d, max: %d, current speed: %d, direction: %s, value: %d, saved value: %d",
		"i2c/sda",
		"i2c/scl",
		"uart",
		"lcd",
		"unknown",
		"  not found\n",
		", info: ",
		"",
		"",
		"",
		"",
		"\n",
		"error",
	},

	.html =
	{
		"<td>io[%d]</td><td>%s@%x</td>",
		"<td></td><td>%d</td>",
		"<td>",
		"</td>",
		"",
		"<td>disabled</td>",
		"<td>input</td><td>state: %s</td>",
		"<td>counter</td><td><td>counter: %d</td><td>debounce: %d</td>",
		"<td>trigger</td><td>counter: %d</td><td>debounce: %d</td>",
		"<td>action #%d</td><td>io: %d</td><td>pin: %d</td><td>trigger action: ",
		"</td>",
		"<td>output</td><td>state: %s</td>",
		"<td>timer</td><td>config direction: %s, speed: %d ms</td><<td>current direction %s, delay: %d ms, state: %s</td>",
		"<td>analog output</td><td>min/static: %d, max: %d, speed: %d, current direction: %s, value: %d, saved value: %d",
		"<td>i2c</td><td>sda</td>",
		"<td>i2c</td><td>scl, speed: %d</td>",
		"<td>uart</td>",
		"<td>lcd</td>",
		"<td>unknown</td>",
		"<td>not found</td>",
		"<td>",
		"</td>",
		"<table border=\"1\"><tr><th>index</th><th>name</th><th>mode</th><th colspan=\"8\"></th></tr>",
		"</table>\n",
		"<tr>",
		"</tr>\n",
		"<td>error</td>",
	}
};

irom void io_config_dump(string_t *dst, int io_id, int pin_id, bool html)
{
	const io_info_entry_t *info;
	io_data_entry_t *data;
	io_data_pin_entry_t *pin_data;
	const io_config_pin_entry_t *pin_config;
	const string_array_t *strings;
	int io, pin, value;
	io_error_t error;

	if(html)
		strings = &dump_strings.html;
	else
		strings = &dump_strings.plain;

	string_cat_ptr(dst, (*strings)[ds_id_header]);

	for(io = 0; io < io_id_size; io++)
	{
		if((io_id >= 0) && (io_id != io))
			continue;

		info = &io_info[io];
		data = &io_data[io];

		string_cat_ptr(dst, (*strings)[ds_id_preline]);
		string_format_ptr(dst, (*strings)[ds_id_io], io, info->name, info->address);
		string_cat_ptr(dst, (*strings)[ds_id_postline]);

		if(!data->detected)
		{
			string_cat_ptr(dst, (*strings)[ds_id_preline]);
			string_cat_ptr(dst, (*strings)[ds_id_not_detected]);
			string_cat_ptr(dst, (*strings)[ds_id_postline]);
			continue;
		}

		for(pin = 0; pin < info->pins; pin++)
		{
			if((pin_id >= 0) && (pin_id != pin))
				continue;

			pin_config = &io_config[io][pin];
			pin_data = &data->pin[pin];

			string_cat_ptr(dst, (*strings)[ds_id_preline]);
			string_format_ptr(dst, (*strings)[ds_id_pin], pin);

			string_cat_ptr(dst, (*strings)[ds_id_flags_1]);
			pin_string_from_flags(dst, pin_config);
			string_cat_ptr(dst, (*strings)[ds_id_flags_2]);

			string_cat_ptr(dst, (*strings)[ds_id_mode]);

			if((pin_config->mode != io_pin_disabled) && (pin_config->mode != io_pin_i2c) && (pin_config->mode != io_pin_uart))
				if((error = io_read_pin_x(dst, info, pin_data, pin_config, pin, &value)) != io_ok)
					string_cat(dst, "\n");
				else
					(void)0;
			else
				error = io_ok;

			switch(pin_config->mode)
			{
				case(io_pin_disabled):
				{
					string_cat_ptr(dst, (*strings)[ds_id_disabled]);

					break;
				}

				case(io_pin_input_digital):
				{
					if(error == io_ok)
						string_format_ptr(dst, (*strings)[ds_id_input], onoff(value));
					else
						string_cat_ptr(dst, (*strings)[ds_id_error]);

					break;
				}

				case(io_pin_counter):
				{
					if(error == io_ok)
						string_format_ptr(dst, (*strings)[ds_id_counter], value, pin_config->speed);
					else
						string_cat_ptr(dst, (*strings)[ds_id_error]);

					break;
				}

				case(io_pin_trigger):
				{
					if(error == io_ok)
					{
						string_format_ptr(dst, (*strings)[ds_id_trigger_1], value, pin_config->speed);
						string_format_ptr(dst, (*strings)[ds_id_trigger_2], 0,
								pin_config->shared.trigger[0].io.io,
								pin_config->shared.trigger[0].io.pin);
						trigger_action_to_string(dst, pin_config->shared.trigger[0].action);

						if(pin_config->shared.trigger[1].action != io_trigger_none)
						{
							string_cat(dst, "\n");
							string_format_ptr(dst, (*strings)[ds_id_trigger_2], 1,
									pin_config->shared.trigger[1].io.io,
									pin_config->shared.trigger[1].io.pin);
							trigger_action_to_string(dst, pin_config->shared.trigger[1].action);
						}
						string_cat_ptr(dst, (*strings)[ds_id_trigger_3]);
					}
					else
						string_cat_ptr(dst, (*strings)[ds_id_error]);

					break;
				}

				case(io_pin_output_digital):
				{
					if(error == io_ok)
						string_format_ptr(dst, (*strings)[ds_id_output], onoff(value));
					else
						string_cat_ptr(dst, (*strings)[ds_id_error]);

					break;
				}

				case(io_pin_timer):
				{
					if(error == io_ok)
						string_format_ptr(dst, (*strings)[ds_id_timer],
								pin_config->direction == io_dir_up ? "up" : (pin_config->direction == io_dir_down ? "down" : "none"),
								pin_config->speed,
								pin_data->direction == io_dir_up ? "up" : (pin_data->direction == io_dir_down ? "down" : "none"),
								pin_data->speed,
								onoff(value));
					else
						string_cat_ptr(dst, (*strings)[ds_id_error]);

					break;
				}

				case(io_pin_output_analog):
				{
					if(error == io_ok)
						string_format_ptr(dst, (*strings)[ds_id_analog_output],
								pin_config->shared.output_analog.lower_bound,
								pin_config->shared.output_analog.upper_bound,
								pin_config->speed,
								pin_data->direction == io_dir_up ? "up" : (pin_data->direction == io_dir_down ? "down" : "none"),
								value, pin_data->saved_value);
					else
						string_cat_ptr(dst, (*strings)[ds_id_error]);

					break;
				}

				case(io_pin_i2c):
				{
					if(pin_config->shared.i2c.pin_mode == io_i2c_sda)
						string_cat_ptr(dst, (*strings)[ds_id_i2c_sda]);
					else
						string_cat_ptr(dst, (*strings)[ds_id_i2c_scl]);

					break;
				}

				case(io_pin_uart):
				{
					string_cat_ptr(dst, (*strings)[ds_id_uart]);

					break;
				}

				case(io_pin_lcd):
				{
					string_cat_ptr(dst, (*strings)[ds_id_lcd]);
					string_cat(dst, "/");
					io_string_from_lcd_mode(dst, pin_config->shared.lcd.pin_use);

					break;
				}


				default:
				{
					string_cat_ptr(dst, (*strings)[ds_id_unknown]);

					break;
				}
			}

			string_cat(dst, " [hw: ");
			io_string_from_ll_mode(dst, pin_config->llmode);
			string_cat(dst, "]");

			if(info->get_pin_info_fn)
			{
				string_cat_ptr(dst, (*strings)[ds_id_info_1]);
				info->get_pin_info_fn(dst, info, pin_data, pin_config, pin);
				string_cat_ptr(dst, (*strings)[ds_id_info_2]);
			}

			string_cat_ptr(dst, (*strings)[ds_id_postline]);
		}
	}

	string_cat_ptr(dst, (*strings)[ds_id_footer]);
}
