#include "io_gpio.h"
#include "io_aux.h"
#include "io_mcp.h"
#include "io_pcf.h"
#include "io_ledpixel.h"
#include "io.h"
#include "i2c.h"
#include "config.h"
#include "util.h"
#include "time.h"
#include "sequencer.h"
#include "dispatch.h"

io_config_pin_entry_t io_config[io_id_size][max_pins_per_io];

static const io_info_t io_info =
{
	{
		io_id_gpio, /* = 0 */
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
			.ledpixel = 1,
			.pullup = 1,
		},
		"Internal GPIO",
		io_gpio_init,
		(void *)0, // postinit
		(void *)0, // periodic slow
		io_gpio_periodic_fast,
		io_gpio_init_pin_mode,
		io_gpio_get_pin_info,
		io_gpio_read_pin,
		io_gpio_write_pin,
	},
	{
		io_id_aux,/* = 1 */
		0x01,
		0,
		2,
		{
			.input_digital = 1,
			.counter = 1,
			.output_digital = 1,
			.input_analog = 1,
			.output_analog = 0,
			.i2c = 0,
			.uart = 0,
			.ledpixel = 0,
			.pullup = 0,
		},
		"Auxilliary GPIO (RTC+ADC)",
		io_aux_init,
		(void *)0, // postinit
		(void *)0, // periodic slow
		io_aux_periodic_fast,
		io_aux_init_pin_mode,
		io_aux_get_pin_info,
		io_aux_read_pin,
		io_aux_write_pin,
	},
	{
		io_id_mcp_20, /* = 2 */
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
			.ledpixel = 0,
			.pullup = 1,
		},
		"MCP23017 I2C I/O expander #1",
		io_mcp_init,
		(void *)0, // postinit
		io_mcp_periodic_slow,
		(void *)0, // periodic fast
		io_mcp_init_pin_mode,
		io_mcp_get_pin_info,
		io_mcp_read_pin,
		io_mcp_write_pin,
	},
	{
		io_id_mcp_21, /* = 3 */
		0x21,
		io_mcp_instance_21,
		16,
		{
			.input_digital = 1,
			.counter = 1,
			.output_digital = 1,
			.input_analog = 0,
			.output_analog = 0,
			.i2c = 0,
			.uart = 0,
			.ledpixel = 0,
			.pullup = 1,
		},
		"MCP23017 I2C I/O expander #2",
		io_mcp_init,
		(void *)0, // postinit
		io_mcp_periodic_slow,
		(void *)0, // periodic fast
		io_mcp_init_pin_mode,
		io_mcp_get_pin_info,
		io_mcp_read_pin,
		io_mcp_write_pin,
	},
	{
		io_id_mcp_22, /* = 4 */
		0x22,
		io_mcp_instance_22,
		16,
		{
			.input_digital = 1,
			.counter = 1,
			.output_digital = 1,
			.input_analog = 0,
			.output_analog = 0,
			.i2c = 0,
			.uart = 0,
			.ledpixel = 0,
			.pullup = 1,
		},
		"MCP23017 I2C I/O expander #3",
		io_mcp_init,
		(void *)0, // postinit
		io_mcp_periodic_slow,
		(void *)0, // periodic fast
		io_mcp_init_pin_mode,
		io_mcp_get_pin_info,
		io_mcp_read_pin,
		io_mcp_write_pin,
	},
	{
		io_id_pcf_3a, /* = 5 */
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
			.ledpixel = 0,
			.pullup = 0,
		},
		"PCF8574A I2C I/O expander",
		io_pcf_init,
		(void *)0, // postinit
		(void *)0, // periodic slow
		(void *)0, // periodic fast
		io_pcf_init_pin_mode,
		(void *)0, // get pin info
		io_pcf_read_pin,
		io_pcf_write_pin,
	},
	{
		io_id_ledpixel, /* = 6 */
		0x00,
		0,
		16,
		{
			.input_digital = 0,
			.counter = 0,
			.output_digital = 0,
			.input_analog = 0,
			.output_analog = 1,
			.i2c = 0,
			.uart = 0,
			.ledpixel = 0,
			.pullup = 0,
		},
		"led string",
		io_ledpixel_init,
		io_ledpixel_post_init,
		(void *)0, // periodic slow
		(void *)0, // periodic fast
		io_ledpixel_init_pin_mode,
		(void *)0, // get pin info
		io_ledpixel_read_pin,
		io_ledpixel_write_pin,
	}
};

static io_data_t io_data;

typedef struct
{
	io_pin_mode_t	mode;
	const char		*short_name;
	const char		*long_name;
} io_mode_trait_t;

static const io_mode_trait_t io_mode_traits[io_pin_size] =
{
	{ io_pin_disabled,			"disabled",		"disabled"				},
	{ io_pin_input_digital,		"dinput",		"digital input"			},
	{ io_pin_counter,			"counter",		"counter"				},
	{ io_pin_output_digital,	"doutput",		"digital output"		},
	{ io_pin_timer,				"timer",		"timer (blink)"			},
	{ io_pin_input_analog,		"ainput",		"analog input (adc)"	},
	{ io_pin_output_analog,		"aoutput",		"analog output (pwm)"	},
	{ io_pin_i2c,				"i2c",			"i2c"					},
	{ io_pin_uart,				"uart",			"uart"					},
	{ io_pin_lcd,				"lcd",			"lcd"					},
	{ io_pin_trigger,			"trigger",		"trigger"				},
	{ io_pin_ledpixel,			"ledpixel",		"ledpixel control"		},
};

irom static io_pin_mode_t io_mode_from_string(const string_t *src)
{
	unsigned int ix;
	const io_mode_trait_t *entry;

	for(ix = 0; ix < io_pin_size; ix++)
	{
		entry = &io_mode_traits[ix];

		if(string_match_cstr(src, entry->short_name))
			return(entry->mode);
	}

	return(io_pin_error);
}

irom static void io_string_from_mode(string_t *name, io_pin_mode_t mode, int pad)
{
	unsigned int ix;
	const io_mode_trait_t *entry;

	for(ix = 0; ix < io_pin_size; ix++)
	{
		entry = &io_mode_traits[ix];

		if(entry->mode == mode)
		{
			if(pad > 0)
				string_format(name, "%20s", entry->long_name);
			else
				if(pad < 0)
					string_format(name, "%-20s", entry->long_name);
				else
					string_format(name, "%s", entry->long_name);

			return;
		}
	}

	string_append(name, "error");
}

irom static void io_strings_from_modes(string_t *dst)
{
	unsigned int ix;
	const io_mode_trait_t *entry;

	for(ix = 0; ix < io_pin_size; ix++)
	{
		entry = &io_mode_traits[ix];

		if(ix != 0)
			string_append(dst, "/");

		string_append_cstr(dst, entry->short_name);
	}
}

typedef struct
{
	io_pin_ll_mode_t	mode;
	const char			*name;
} io_ll_mode_trait_t;

static io_ll_mode_trait_t io_ll_mode_traits[io_pin_ll_size] =
{
	{ io_pin_ll_disabled,			"disabled"			},
	{ io_pin_ll_input_digital,		"digital input"		},
	{ io_pin_ll_counter,			"counter"			},
	{ io_pin_ll_output_digital,		"digital output"	},
	{ io_pin_ll_input_analog,		"analog input"		},
	{ io_pin_ll_output_analog,		"analog output"		},
	{ io_pin_ll_i2c,				"i2c"				},
	{ io_pin_ll_uart,				"uart"				},
};

irom void io_string_from_ll_mode(string_t *name, io_pin_ll_mode_t mode, int pad)
{
	unsigned int ix;
	const io_ll_mode_trait_t *entry;

	for(ix = 0; ix < io_pin_size; ix++)
	{
		entry = &io_ll_mode_traits[ix];

		if(entry->mode == mode)
		{
			if(pad > 0)
				string_format(name, "%14s", entry->name);
			else
				if(pad < 0)
					string_format(name, "%-14s", entry->name);
				else
					string_format(name, "%s", entry->name);
			return;
		}
	}

	string_append(name, "error");
}

irom static io_i2c_t io_i2c_pin_from_string(const string_t *pin)
{
	if(string_match_cstr(pin, "sda"))
		return(io_i2c_sda);
	else if(string_match_cstr(pin, "scl"))
		return(io_i2c_scl);
	else
		return(io_i2c_error);
}

irom static void io_string_from_i2c_type(string_t *name, io_i2c_t type)
{
	switch(type)
	{
		case(io_i2c_sda): { string_append(name, "sda"); break; }
		case(io_i2c_scl): { string_append(name, "scl"); break; }
		default: { string_append(name, "error"); break; }
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
	unsigned int ix;
	const io_lcd_mode_trait_t *entry;

	for(ix = 0; ix < io_lcd_size; ix++)
	{
		entry = &io_lcd_mode_traits[ix];

		if(string_match_cstr(src, entry->name))
			return(entry->mode);
	}

	return(io_lcd_error);
}

irom static void io_string_from_lcd_mode(string_t *name, io_lcd_mode_t mode)
{
	unsigned int ix;
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

	string_append(name, "error");
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
	unsigned int ix;
	const io_trigger_action_t *entry;

	for(ix = 0; ix < io_trigger_size; ix++)
	{
		entry = &io_trigger_action[ix];

		if(string_match_cstr(src, entry->name))
			return(entry->id);
	}

	return(io_trigger_error);
}

irom static void trigger_actions_to_string(string_t *dst)

{
	unsigned int ix;
	const io_trigger_action_t *entry;

	for(ix = 0; ix < io_trigger_size; ix++)
	{
		entry = &io_trigger_action[ix];

		if(ix > 0)
			string_append(dst, ", ");

		string_format(dst, "%s", entry->name);
	}
}

irom static void trigger_action_to_string(string_t *name, io_trigger_t id)
{
	unsigned int ix;
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

	string_append(name, "error");
}

irom static void trigger_usage(string_t *dst)
{
	string_append(dst, "usage: io-trigger <io> <pin> <action>\n");
	string_append(dst, "    action: ");
	trigger_actions_to_string(dst);
}

irom static void iomode_trigger_usage(string_t *dst, const char *info)
{
	string_append(dst, "usage: io-mode <io> <pin> trigger <debounce_ms> <action1> <io1> <pin1> [<action2> <io2> <pin2>]\n");
	string_append(dst, "    action: ");
	trigger_actions_to_string(dst);
	string_format(dst, "\nerror in <%s>\n", info);
}

irom static bool pin_flag_from_string(const string_t *flag, io_config_pin_entry_t *pin_config, bool_t value)
{
	if(string_match_cstr(flag, "autostart"))
		pin_config->flags.autostart = value ? 1: 0;
	else if(string_match_cstr(flag, "repeat"))
		pin_config->flags.repeat = value ? 1: 0;
	else if(string_match_cstr(flag, "pullup"))
		pin_config->flags.pullup = value ? 1: 0;
	else if(string_match_cstr(flag, "reset-on-read"))
		pin_config->flags.reset_on_read = value ? 1: 0;
	else if(string_match_cstr(flag, "extended"))
		pin_config->flags.extended = value ? 1: 0;
	else if(string_match_cstr(flag, "grb"))
		pin_config->flags.grb = value ? 1: 0;
	else if(string_match_cstr(flag, "linear"))
		pin_config->flags.linear = value ? 1: 0;
	else
		return(false);

	return(true);
}

irom static void pin_string_from_flags(string_t *flags, const io_config_pin_entry_t *pin_config)
{
	bool none = true;

	if(pin_config->flags.autostart)
	{
		if(!none)
			string_append(flags, "/");
		none = false;
		string_append(flags, "autostart");
	}

	if(pin_config->flags.repeat)
	{
		if(!none)
			string_append(flags, "/");
		none = false;
		string_append(flags, "repeat");
	}

	if(pin_config->flags.pullup)
	{
		if(!none)
			string_append(flags, "/");
		none = false;
		string_append(flags, "pullup");
	}

	if(pin_config->flags.reset_on_read)
	{
		if(!none)
			string_append(flags, "/");
		none = false;
		string_append(flags, "reset-on-read");
	}

	if(pin_config->flags.extended)
	{
		if(!none)
			string_append(flags, "/");
		none = false;
		string_append(flags, "extended");
	}

	if(pin_config->flags.grb)
	{
		if(!none)
			string_append(flags, "/");
		none = false;
		string_append(flags, "grb");
	}

	if(pin_config->flags.linear)
	{
		if(!none)
			string_append(flags, "/");
		none = false;
		string_append(flags, "linear");
	}

	if(none)
		string_append(flags, "none");
}

irom static io_error_t io_read_pin_x(string_t *errormsg, const io_info_entry_t *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin, uint32_t *value)
{
	io_error_t error;

	switch(pin_config->mode)
	{
		case(io_pin_disabled):
		case(io_pin_error):
		case(io_pin_ledpixel):
		{
			if(errormsg)
				string_append(errormsg, "cannot read from this pin");

			return(io_error);
		}

		default:
		{
			if((error = info->read_pin_fn(errormsg, info, pin_data, pin_config, pin, value)) != io_ok)
				return(error);

			break;
		}
	}

	return(io_ok);
}

irom static io_error_t io_write_pin_x(string_t *errormsg, const io_info_entry_t *info, io_data_pin_entry_t *pin_data, io_config_pin_entry_t *pin_config, int pin, uint32_t value)
{
	io_error_t error;

	switch(pin_config->mode)
	{
		case(io_pin_disabled):
		case(io_pin_error):
		case(io_pin_ledpixel):
		{
			if(errormsg)
				string_append(errormsg, "cannot write to this pin");

			return(io_error);
		}

		default:
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
	uint32_t value = 0, old_value, trigger;

	switch(pin_config->mode)
	{
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
						string_append(errormsg, "invalid trigger type");

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

					if(value > 0)
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
						string_append(errormsg, "invalid trigger type");

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
						string_append(errormsg, "invalid trigger type");

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

					if(value > 0)
						pin_data->saved_value = value;

					value = 0;

					break;
				}

				case(io_trigger_on):
				{
					if(value == 0)
					{
						if(pin_data->saved_value > 0)
							value = pin_data->saved_value;
						else
							value = pin_config->shared.output_analog.upper_bound;

						pin_data->saved_value = 0;
					}

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

					if(pin_config->flags.linear)
					{
						if(value >= pin_config->speed)
							value -= pin_config->speed;
					}
					else
					{
						value /= (pin_config->speed / 10000.0) + 1;

						if((old_value == value) && (value > 0))
							value--;
					}

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

						if(pin_config->flags.linear)
							value += pin_config->speed;
						else
						{
							value *= (pin_config->speed / 10000.0) + 1;

							if(old_value == value)
								value++;
						}
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
						string_append(errormsg, "invalid trigger type");

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

		default:
		{
			if(errormsg)
				string_append(errormsg, "cannot trigger this pin");

			return(io_error);
		}

	}

	return(io_ok);
}

irom io_error_t io_read_pin(string_t *error_msg, int io, int pin, uint32_t *value)
{
	const io_info_entry_t *info;
	io_data_entry_t *data;
	io_config_pin_entry_t *pin_config;
	io_data_pin_entry_t *pin_data;
	io_error_t error;

	if(io >= io_id_size)
	{
		if(error_msg)
			string_append(error_msg, "io out of range\n");
		return(io_error);
	}

	info = &io_info[io];
	data = &io_data[io];

	if(pin >= info->pins)
	{
		if(error_msg)
			string_append(error_msg, "pin out of range\n");
		return(io_error);
	}

	pin_config = &io_config[io][pin];
	pin_data = &data->pin[pin];

	if(((error = io_read_pin_x(error_msg, info, pin_data, pin_config, pin, value)) != io_ok) && error_msg)
		string_append(error_msg, "\n");
	else
		if((pin_config->mode == io_pin_counter) && (pin_config->flags.reset_on_read))
			error = io_write_pin_x(error_msg, info, pin_data, pin_config, pin, 0);

	return(error);
}

irom io_error_t io_write_pin(string_t *error, int io, int pin, uint32_t value)
{
	const io_info_entry_t *info;
	io_data_entry_t *data;
	io_config_pin_entry_t *pin_config;
	io_data_pin_entry_t *pin_data;

	if(io >= io_id_size)
	{
		if(error)
			string_append(error, "io out of range\n");
		return(io_error);
	}

	info = &io_info[io];
	data = &io_data[io];

	if(pin >= info->pins)
	{
		if(error)
			string_append(error, "pin out of range\n");
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
			string_append(error, "io out of range\n");
		return(io_error);
	}

	info = &io_info[io];
	data = &io_data[io];

	if(pin >= info->pins)
	{
		if(error)
			string_append(error, "pin out of range\n");
		return(io_error);
	}

	pin_config = &io_config[io][pin];
	pin_data = &data->pin[pin];

	return(io_trigger_pin_x(error, info, pin_data, pin_config, pin, trigger_type));
}

irom io_error_t io_traits(string_t *errormsg, int io, int pin, io_pin_mode_t *pinmode, uint32_t *lower_bound, uint32_t *upper_bound, int *step, uint32_t *value)
{
	io_error_t error;
	const io_info_entry_t *info;
	io_data_entry_t *data;
	io_config_pin_entry_t *pin_config;
	io_data_pin_entry_t *pin_data;
	unsigned int pwm_period;
	string_init(varname_pwmperiod, "pwm.period");

	if(!config_get_int(&varname_pwmperiod, -1, -1, &pwm_period))
		pwm_period = 65536;

	if(io >= io_id_size)
	{
		if(errormsg)
			string_append(errormsg, "io out of range\n");
		return(io_error);
	}

	info = &io_info[io];
	data = &io_data[io];

	if(pin >= info->pins)
	{
		if(errormsg)
			string_append(errormsg, "pin out of range\n");
		return(io_error);
	}

	pin_config = &io_config[io][pin];
	pin_data = &data->pin[pin];

	*pinmode = pin_config->mode;

	switch(pin_config->mode)
	{
		case(io_pin_disabled):
		case(io_pin_error):
		case(io_pin_trigger):
		{
			if(errormsg)
				string_append(errormsg, "pin disabled");
			return(io_error);
		}

		case(io_pin_output_analog):
		{
			*lower_bound	= pin_config->shared.output_analog.lower_bound;
			*upper_bound	= pin_config->shared.output_analog.upper_bound;
			*step			= pin_config->speed;

			if(*lower_bound > pwm_period)
				*lower_bound = 0;

			if(*upper_bound > pwm_period)
				*upper_bound = pwm_period - 1;

			if((error = io_read_pin_x(errormsg, info, pin_data, pin_config, pin, value)) != io_ok)
				return(error);

			break;
		}

		default:
		{
			if(errormsg)
				string_append(errormsg, "no info for this type");
			return(io_error);
		}
	}

	return(io_ok);
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
	string_init(varname_iomode, "io.%u.%u.mode");
	string_init(varname_llmode, "io.%u.%u.llmode");
	string_init(varname_flags, "io.%u.%u.flags");
	string_init(varname_iocounter_debounce, "io.%u.%u.counter.debounce");
	string_init(varname_iotrigger_debounce, "io.%u.%u.trigger.debounce");
	string_init(varname_iotrigger_io, "io.%u.%u.trigger.io");
	string_init(varname_iotrigger_pin, "io.%u.%u.trigger.pin");
	string_init(varname_iotrigger_type, "io.%u.%u.trigger.type");
	string_init(varname_iotrigger_0_io, "io.%u.%u.trigger.0.io");
	string_init(varname_iotrigger_0_pin, "io.%u.%u.trigger.0.pin");
	string_init(varname_iotrigger_0_type, "io.%u.%u.trigger.0.type");
	string_init(varname_iotrigger_1_io, "io.%u.%u.trigger.1.io");
	string_init(varname_iotrigger_1_pin, "io.%u.%u.trigger.1.pin");
	string_init(varname_iotrigger_1_type, "io.%u.%u.trigger.1.type");
	string_init(varname_iotimer_delay, "io.%u.%u.timer.delay");
	string_init(varname_iotimer_direction, "io.%u.%u.timer.direction");
	string_init(varname_iooutputa_speed, "io.%u.%u.outputa.speed");
	string_init(varname_iooutputa_lower, "io.%u.%u.outputa.lower");
	string_init(varname_iooutputa_upper, "io.%u.%u.outputa.upper");
	string_init(varname_i2c_pinmode, "io.%u.%u.i2c.pinmode");
	string_init(varname_lcd_pin, "io.%u.%u.lcd.pin");

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

			if(!config_get_int(&varname_iomode, io, pin, &mode))
			{
				pin_config->mode = io_pin_disabled;
				pin_config->llmode = io_pin_ll_disabled;
				continue;
			}

			if(!config_get_int(&varname_llmode, io, pin, &llmode))
			{
				pin_config->mode = io_pin_disabled;
				pin_config->llmode = io_pin_ll_disabled;
				continue;
			}

			if(!config_get_int(&varname_flags, io, pin, &flags.intvalue))
				flags.intvalue = 0;

			pin_config->flags = flags.io_pin_flags;

			pin_config->mode = mode;
			pin_config->llmode = llmode;

			switch(mode)
			{
				case(io_pin_counter):
				{
					int debounce;

					if(!config_get_int(&varname_iocounter_debounce, io, pin, &debounce))
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

					if(!config_get_int(&varname_iotrigger_debounce, io, pin, &debounce))
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

					if(config_get_int(&varname_iotrigger_io, io, pin, &trigger_io) &&
						config_get_int(&varname_iotrigger_pin, io, pin, &trigger_pin) &&
						config_get_int(&varname_iotrigger_type, io, pin, &trigger_type))
					{
						pin_config->shared.trigger[0].io.io = trigger_io;
						pin_config->shared.trigger[0].io.pin = trigger_pin;
						pin_config->shared.trigger[0].action = trigger_type;
					}

					if(config_get_int(&varname_iotrigger_0_io, io, pin, &trigger_io) &&
						config_get_int(&varname_iotrigger_0_pin, io, pin, &trigger_pin) &&
						config_get_int(&varname_iotrigger_0_type, io, pin, &trigger_type))
					{
						pin_config->shared.trigger[0].io.io = trigger_io;
						pin_config->shared.trigger[0].io.pin = trigger_pin;
						pin_config->shared.trigger[0].action = trigger_type;
					}

					if(config_get_int(&varname_iotrigger_1_io, io, pin, &trigger_io) &&
						config_get_int(&varname_iotrigger_1_pin, io, pin, &trigger_pin) &&
						config_get_int(&varname_iotrigger_1_type, io, pin, &trigger_type))
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

					if(!config_get_int(&varname_iotimer_delay, io, pin, &speed))
					{
						pin_config->mode = io_pin_disabled;
						pin_config->llmode = io_pin_ll_disabled;
						continue;
					}

					if(!config_get_int(&varname_iotimer_direction, io, pin, &direction))
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
					int speed;
					uint32_t lower_bound, upper_bound;

					if(!info->caps.output_analog)
					{
						pin_config->mode = io_pin_disabled;
						pin_config->llmode = io_pin_ll_disabled;
						continue;
					}

					if(!config_get_int(&varname_iooutputa_speed, io, pin, &speed))
					{
						pin_config->mode = io_pin_disabled;
						pin_config->llmode = io_pin_ll_disabled;
						continue;
					}

					if(!config_get_int(&varname_iooutputa_lower, io, pin, &lower_bound))
					{
						pin_config->mode = io_pin_disabled;
						pin_config->llmode = io_pin_ll_disabled;
						continue;
					}

					if(!config_get_int(&varname_iooutputa_upper, io, pin, &upper_bound))
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

					if(!config_get_int(&varname_i2c_pinmode, io, pin, &pin_mode))
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

					if(!config_get_int(&varname_lcd_pin, io, pin, &pin_mode))
					{
						pin_config->mode = io_pin_disabled;
						pin_config->llmode = io_pin_ll_disabled;
						continue;
					}

					pin_config->shared.lcd.pin_use = pin_mode;

					break;
				}

				case(io_pin_ledpixel):
				{
					if(!info->caps.ledpixel)
					{
						pin_config->mode = io_pin_disabled;
						pin_config->llmode = io_pin_ll_disabled;
						continue;
					}

					break;
				}

				default:
				{
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

				if(!info->init_pin_mode_fn || (info->init_pin_mode_fn((string_t *)0, info, pin_data, pin_config, pin) == io_ok))
				{
					switch(pin_config->mode)
					{
						case(io_pin_output_digital):
						case(io_pin_lcd):
						case(io_pin_timer):
						{
							// FIXME: add auto-on flag
							io_trigger_pin_x((string_t *)0, info, pin_data, pin_config, pin,
									pin_config->flags.autostart ? io_trigger_on : io_trigger_off);

							break;
						}

						case(io_pin_output_analog):
						{
							// FIXME: add auto-on flag
							io_trigger_pin_x((string_t *)0, info, pin_data, pin_config, pin,
									pin_config->flags.autostart ? io_trigger_start : io_trigger_stop);
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

						case(io_pin_ledpixel):
						{
							if(io == 0)
								io_ledpixel_setup(pin, 0);

							break;
						}

						default:
						{
							break;
						}
					}
				}
			}
		}
	}

	sequencer_init();
}

iram void io_periodic_fast(void)
{
	const io_info_entry_t *info;
	io_data_entry_t *data;
	io_config_pin_entry_t *pin_config;
	io_data_pin_entry_t *pin_data;
	int io, pin, trigger;
	uint32_t value;
	int trigger_status_io, trigger_status_pin;
	io_flags_t flags = { .counter_triggered = 0 };
	string_init(varname_trigger_io, "trigger.status.io");
	string_init(varname_trigger_pin, "trigger.status.pin");

	for(io = 0; io < io_id_size; io++)
	{
		info = &io_info[io];
		data = &io_data[io];

		if(!data->detected)
			continue;

		if(info->periodic_fast_fn)
			info->periodic_fast_fn(io, info, data, &flags);

		for(pin = 0; pin < info->pins; pin++)
		{
			pin_config = &io_config[io][pin];
			pin_data = &data->pin[pin];

			if((pin_config->mode == io_pin_timer) && (pin_data->direction != io_dir_none))
			{
				if(pin_data->speed > ms_per_fast_tick)
					pin_data->speed -= ms_per_fast_tick;
				else
					pin_data->speed = 0;

				if(pin_data->speed == 0)
				{
					if(pin_data->direction == io_dir_up)
					{
						info->write_pin_fn((string_t *)0, info, pin_data, pin_config, pin, 1);
						pin_data->direction = io_dir_down;
					}
					else
					{
						if(pin_data->direction == io_dir_down)
						{
							info->write_pin_fn((string_t *)0, info, pin_data, pin_config, pin, 0);
							pin_data->direction = io_dir_up;
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
			}

			if((pin_config->mode == io_pin_trigger) && (info->read_pin_fn((string_t *)0, info, pin_data, pin_config, pin, &value) == io_ok) && (value != 0))
			{
				for(trigger = 0; trigger < max_triggers_per_pin; trigger++)
				{
					if(pin_config->shared.trigger[trigger].action != io_trigger_none)
					{
						io_trigger_pin((string_t *)0,
								pin_config->shared.trigger[trigger].io.io,
								pin_config->shared.trigger[trigger].io.pin,
								pin_config->shared.trigger[trigger].action);
					}
				}

				info->write_pin_fn((string_t *)0, info, pin_data, pin_config, pin, 0);
			}
		}
	}

	if(flags.counter_triggered &&
			config_get_int(&varname_trigger_io, -1, -1, &trigger_status_io) &&
			config_get_int(&varname_trigger_pin, -1, -1, &trigger_status_pin) &&
			(trigger_status_io >= 0) && (trigger_status_pin >= 0))
		io_trigger_pin((string_t *)0, trigger_status_io, trigger_status_pin, io_trigger_on);

	if((sequencer_get_repeats() > 0) && ((time_get_us() / 1000) > sequencer_get_current_end_time()))
		dispatch_post_command(command_task_command_run_sequencer);
}

irom void io_periodic_slow(void)
{
	const io_info_entry_t *info;
	io_data_entry_t *data;
	io_config_pin_entry_t *pin_config;
	io_data_pin_entry_t *pin_data;
	int io, pin;
	int trigger_status_io, trigger_status_pin;
	io_flags_t flags = { .counter_triggered = 0 };
	string_init(varname_trigger_io, "trigger.status.io");
	string_init(varname_trigger_pin, "trigger.status.pin");
	static bool_t post_init_run = false;

	for(io = 0; io < io_id_size; io++)
	{
		info = &io_info[io];
		data = &io_data[io];

		if(!data->detected)
			continue;

		if(!post_init_run && info->post_init_fn)
			info->post_init_fn(info);

		if(info->periodic_slow_fn)
			info->periodic_slow_fn(io, info, data, &flags);

		for(pin = 0; pin < info->pins; pin++)
		{
			pin_config = &io_config[io][pin];
			pin_data = &data->pin[pin];

			if((pin_config->mode == io_pin_output_analog) &&
					(pin_config->shared.output_analog.upper_bound > pin_config->shared.output_analog.lower_bound) &&
					(pin_config->speed > 0) &&
					(pin_data->direction != io_dir_none))
				io_trigger_pin_x((string_t *)0, info, pin_data, pin_config, pin, (pin_data->direction == io_dir_up) ? io_trigger_up : io_trigger_down);
		}
	}

	post_init_run = true;

	if(flags.counter_triggered &&
			config_get_int(&varname_trigger_io, -1, -1, &trigger_status_io) &&
			config_get_int(&varname_trigger_pin, -1, -1, &trigger_status_pin) &&
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
	unsigned int io, pin;
	string_init(varname_io, "io.%u.%u.");
	string_init(varname_io_mode, "io.%u.%u.mode");
	string_init(varname_io_llmode, "io.%u.%u.llmode");
	string_init(varname_io_counter_debounce, "io.%u.%u.counter.debounce");
	string_init(varname_io_trigger_debounce, "io.%u.%u.trigger.debounce");
	string_init(varname_io_trigger_0_io, "io.%u.%u.trigger.0.io");
	string_init(varname_io_trigger_0_pin, "io.%u.%u.trigger.0.pin");
	string_init(varname_io_trigger_0_type, "io.%u.%u.trigger.0.type");
	string_init(varname_io_trigger_1_io, "io.%u.%u.trigger.1.io");
	string_init(varname_io_trigger_1_pin, "io.%u.%u.trigger.1.pin");
	string_init(varname_io_trigger_1_type, "io.%u.%u.trigger.1.type");
	string_init(varname_io_timer_direction, "io.%u.%u.timer.direction");
	string_init(varname_io_timer_delay, "io.%u.%u.timer.delay");
	string_init(varname_io_outputa_lower, "io.%u.%u.outputa.lower");
	string_init(varname_io_outputa_upper, "io.%u.%u.outputa.upper");
	string_init(varname_io_outputa_speed, "io.%u.%u.outputa.speed");
	string_init(varname_io_i2c_pinmode, "io.%u.%u.i2c.pinmode");
	string_init(varname_io_lcd_pin, "io.%u.%u.lcd.pin");

	if(parse_uint(1, src, &io, 0, ' ') != parse_ok)
	{
		io_config_dump(dst, -1, -1, false);
		return(app_action_normal);
	}

	if(io >= io_id_size)
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

	if(parse_uint(2, src, &pin, 0, ' ') != parse_ok)
	{
		io_config_dump(dst, io, -1, false);
		return(app_action_normal);
	}

	if(pin >= info->pins)
	{
		string_append(dst, "io pin out of range\n");
		return(app_action_error);
	}

	pin_config = &io_config[io][pin];
	pin_data = &data->pin[pin];

	if(parse_string(3, src, dst, ' ') != parse_ok)
	{
		string_clear(dst);
		io_config_dump(dst, io, pin, false);
		return(app_action_normal);
	}

	if((mode = io_mode_from_string(dst)) == io_pin_error)
	{
		string_clear(dst);
		string_append(dst, "invalid mode, available modes: ");
		io_strings_from_modes(dst);
		string_append(dst, "\n");
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
				string_append(dst, "digital input mode invalid for this io\n");
				return(app_action_error);
			}

			llmode = io_pin_ll_input_digital;

			config_delete(&varname_io, io, pin, true);
			config_set_int(&varname_io_mode, io, pin, mode);
			config_set_int(&varname_io_llmode, io, pin, io_pin_ll_input_digital);

			break;
		}

		case(io_pin_counter):
		{
			if(!info->caps.counter)
			{
				string_append(dst, "counter mode invalid for this io\n");
				return(app_action_error);
			}

			unsigned int debounce;

			if((parse_uint(4, src, &debounce, 0, ' ') != parse_ok))
			{
				string_append(dst, "counter: <debounce ms>\n");
				return(app_action_error);
			}

			pin_config->speed = debounce;
			llmode = io_pin_ll_counter;

			config_delete(&varname_io, io, pin, true);
			config_set_int(&varname_io_mode, io, pin, mode);
			config_set_int(&varname_io_llmode, io, pin, io_pin_ll_counter);
			config_set_int(&varname_io_counter_debounce, io, pin, debounce);

			break;
		}

		case(io_pin_trigger):
		{
			unsigned int debounce, trigger_io, trigger_pin;
			io_trigger_t trigger_type;

			if(!info->caps.counter)
			{
				string_append(dst, "trigger mode invalid for this io\n");
				return(app_action_error);
			}

			if((parse_uint(4, src, &debounce, 0, ' ') != parse_ok))
			{
				iomode_trigger_usage(dst, "debounce");
				return(app_action_error);
			}

			pin_config->speed = debounce;

			if((parse_string(5, src, dst, ' ') != parse_ok))
			{
				string_clear(dst);
				iomode_trigger_usage(dst, "action 1");
				return(app_action_error);
			}

			if((trigger_type = string_to_trigger_action(dst)) == io_trigger_error)
			{
				string_clear(dst);
				iomode_trigger_usage(dst, "action 2");
				return(app_action_error);
			}

			if((parse_uint(6, src, &trigger_io, 0, ' ') != parse_ok))
			{
				iomode_trigger_usage(dst, "io");
				return(app_action_error);
			}

			if((parse_uint(7, src, &trigger_pin, 0, ' ') != parse_ok))
			{
				iomode_trigger_usage(dst, "pin");
				return(app_action_error);
			}

			pin_config->shared.trigger[0].io.io = trigger_io;
			pin_config->shared.trigger[0].io.pin = trigger_pin;
			pin_config->shared.trigger[0].action = trigger_type;

			pin_config->shared.trigger[1].io.io = -1;
			pin_config->shared.trigger[1].io.pin = -1;
			pin_config->shared.trigger[1].action = io_trigger_none;

			if((parse_string(8, src, dst, ' ') != parse_ok))
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

			if((parse_uint(9, src, &trigger_io, 0, ' ') != parse_ok))
				goto skip;

			if((parse_int(10, src, &trigger_pin, 0, ' ') != parse_ok))
				goto skip;

			pin_config->shared.trigger[1].io.io = trigger_io;
			pin_config->shared.trigger[1].io.pin = trigger_pin;
			pin_config->shared.trigger[1].action = trigger_type;

skip:
			llmode = io_pin_ll_counter;

			config_delete(&varname_io, io, pin, true);
			config_set_int(&varname_io_mode, io, pin, mode);
			config_set_int(&varname_io_llmode, io, pin, io_pin_ll_counter);
			config_set_int(&varname_io_trigger_debounce, io, pin, debounce);

			config_set_int(&varname_io_trigger_0_io, io, pin, pin_config->shared.trigger[0].io.io);
			config_set_int(&varname_io_trigger_0_pin, io, pin, pin_config->shared.trigger[0].io.pin);
			config_set_int(&varname_io_trigger_0_type, io, pin, pin_config->shared.trigger[0].action);

			if((pin_config->shared.trigger[1].io.io >= 0) &&
				(pin_config->shared.trigger[1].io.pin >= 0) &&
				(pin_config->shared.trigger[1].action != io_trigger_none))
			{
				config_set_int(&varname_io_trigger_1_io, io, pin, pin_config->shared.trigger[1].io.io);
				config_set_int(&varname_io_trigger_1_pin, io, pin, pin_config->shared.trigger[1].io.pin);
				config_set_int(&varname_io_trigger_1_type, io, pin, pin_config->shared.trigger[1].action);
			}

			break;
		}

		case(io_pin_output_digital):
		{
			if(!info->caps.output_digital)
			{
				string_append(dst, "digital output mode invalid for this io\n");
				return(app_action_error);
			}

			llmode = io_pin_ll_output_digital;

			config_delete(&varname_io, io, pin, true);
			config_set_int(&varname_io_mode, io, pin, mode);
			config_set_int(&varname_io_llmode, io, pin, io_pin_ll_output_digital);

			break;
		}

		case(io_pin_timer):
		{
			io_direction_t direction;
			uint32_t speed;

			if(!info->caps.output_digital)
			{
				string_append(dst, "timer mode invalid for this io\n");
				return(app_action_error);
			}

			if(parse_string(4, src, dst, ' ') != parse_ok)
			{
				string_clear(dst);
				string_append(dst, "timer: <direction>:up/down <speed>:ms\n");
				return(app_action_error);
			}

			if(string_match_cstr(dst, "up"))
				direction = io_dir_up;
			else if(string_match_cstr(dst, "down"))
				direction = io_dir_down;
			else
			{
				string_append(dst, ": timer direction invalid\n");
				return(app_action_error);
			}

			string_clear(dst);

			if((parse_uint(5, src, &speed, 0, ' ') != parse_ok))
			{
				string_clear(dst);
				string_append(dst, "timer: <direction>:up/down <speed>:ms\n");
				return(app_action_error);
			}

			if(speed < ms_per_slow_tick)
			{
				string_format(dst, "timer: speed too small: must be >= %u ms\n", ms_per_slow_tick);
				return(app_action_error);
			}

			pin_config->direction = direction;
			pin_config->speed = speed;

			llmode = io_pin_ll_output_digital;

			config_delete(&varname_io, io, pin, true);
			config_set_int(&varname_io_mode, io, pin, mode);
			config_set_int(&varname_io_llmode, io, pin, io_pin_ll_output_digital);
			config_set_int(&varname_io_timer_direction, io, pin, direction);
			config_set_int(&varname_io_timer_delay, io, pin, speed);

			break;
		}

		case(io_pin_input_analog):
		{
			if(!info->caps.input_analog)
			{
				string_append(dst, "analog input mode invalid for this io\n");
				return(app_action_error);
			}

			llmode = io_pin_ll_input_analog;

			config_delete(&varname_io, io, pin, true);
			config_set_int(&varname_io_mode, io, pin, mode);
			config_set_int(&varname_io_llmode, io, pin, io_pin_ll_input_analog);

			break;
		}

		case(io_pin_output_analog):
		{
			uint32_t lower_bound = 0;
			uint32_t upper_bound = 0;
			uint32_t speed = 0;

			if(!info->caps.output_analog)
			{
				string_append(dst, "analog output mode invalid for this io\n");
				return(app_action_error);
			}

			parse_uint(4, src, &lower_bound, 0, ' ');
			parse_uint(5, src, &upper_bound, 0, ' ');
			parse_uint(6, src, &speed, 0, ' ');

			if(upper_bound == 0)
				upper_bound = ~0;

			if(upper_bound < lower_bound)
			{
				string_append(dst, "upper bound below lower bound\n");
				return(app_action_error);
			}

			pin_config->shared.output_analog.lower_bound = lower_bound;
			pin_config->shared.output_analog.upper_bound = upper_bound;
			pin_config->speed = speed;

			llmode = io_pin_ll_output_analog;

			config_delete(&varname_io, io, pin, true);
			config_set_int(&varname_io_mode, io, pin, mode);
			config_set_int(&varname_io_llmode, io, pin, io_pin_ll_output_analog);
			config_set_int(&varname_io_outputa_lower, io, pin, lower_bound);
			config_set_int(&varname_io_outputa_upper, io, pin, upper_bound);
			config_set_int(&varname_io_outputa_speed, io, pin, speed);

			break;
		}

		case(io_pin_i2c):
		{
			io_i2c_t pin_mode;

			if(!info->caps.i2c)
			{
				string_append(dst, "i2c mode invalid for this io\n");
				return(app_action_error);
			}

			if(parse_string(4, src, dst, ' ') != parse_ok)
			{
				string_clear(dst);
				string_append(dst, "i2c: <pin mode>=sda|scl\n");
				return(app_action_error);
			}

			if((pin_mode = io_i2c_pin_from_string(dst)) == io_i2c_error)
			{
				string_clear(dst);
				string_append(dst, "i2c: <pin mode>=sda|scl\n");
				return(app_action_error);
			}

			string_clear(dst);

			pin_config->shared.i2c.pin_mode = pin_mode;

			llmode = io_pin_ll_i2c;

			config_delete(&varname_io, io, pin, true);
			config_set_int(&varname_io_mode, io, pin, mode);
			config_set_int(&varname_io_llmode, io, pin, io_pin_ll_i2c);
			config_set_int(&varname_io_i2c_pinmode, io, pin, pin_mode);

			break;
		}

		case(io_pin_uart):
		{
			if(!info->caps.uart)
			{
				string_append(dst, "uart mode invalid for this io\n");
				return(app_action_error);
			}

			llmode = io_pin_ll_uart;

			config_delete(&varname_io, io, pin, true);
			config_set_int(&varname_io_mode, io, pin, mode);
			config_set_int(&varname_io_llmode, io, pin, io_pin_ll_uart);

			break;
		}

		case(io_pin_lcd):
		{
			io_lcd_mode_t pin_mode;

			if(parse_string(4, src, dst, ' ') != parse_ok)
			{
				string_clear(dst);
				string_append(dst, "lcd: <pin use>=rs|rw|e|d0|d1|d2|d3|d4|d5|d6|d7|bl\n");
				return(app_action_error);
			}

			if((pin_mode = io_lcd_mode_from_string(dst)) == io_lcd_error)
			{
				string_clear(dst);
				string_append(dst, "lcd: <pin use>=rs|rw|e|d0|d1|d2|d3|d4|d5|d6|d7|bl\n");
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
						string_append(dst, "analog/digital output mode invalid for this io\n");
						return(app_action_error);
					}
			}
			else
			{
				if(!info->caps.output_digital)
				{
					string_append(dst, "digital output mode invalid for this io\n");
					return(app_action_error);
				}

				llmode = io_pin_ll_output_digital;
			}

			pin_config->shared.lcd.pin_use = pin_mode;

			config_delete(&varname_io, io, pin, true);
			config_set_int(&varname_io_mode, io, pin, mode);
			config_set_int(&varname_io_llmode, io, pin, llmode);
			config_set_int(&varname_io_lcd_pin, io, pin, pin_mode);

			break;
		}

		case(io_pin_ledpixel):
		{
			if(!info->caps.ledpixel)
			{
				string_append(dst, "ledpixel mode invalid for this io\n");
				return(app_action_error);
			}

			llmode = io_pin_ll_uart;

			config_delete(&varname_io, io, pin, true);
			config_set_int(&varname_io_mode, io, pin, mode);
			config_set_int(&varname_io_llmode, io, pin, io_pin_ll_uart);

			break;
		}

		case(io_pin_disabled):
		{
			llmode = io_pin_ll_disabled;

			config_delete(&varname_io, io, pin, true);

			break;
		}

		case(io_pin_error):
		{
			llmode = io_pin_ll_error;

			string_append(dst, "unsupported io mode\n");
			return(app_action_error);
		}
	}

	if((mode == io_pin_error) || (llmode == io_pin_ll_error))
	{
		string_append(dst, "error\n");
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
	unsigned int io, pin;
	uint32_t value;

	if(parse_uint(1, src, &io, 0, ' ') != parse_ok)
	{
		string_append(dst, "io-read: <io> <pin>\n");
		return(app_action_error);
	}

	if(io >= io_id_size)
	{
		string_format(dst, "invalid io %d\n", io);
		return(app_action_error);
	}

	info = &io_info[io];

	if(parse_uint(2, src, &pin, 0, ' ') != parse_ok)
	{
		string_append(dst, "get: <io> <pin>\n");
		return(app_action_error);
	}

	if(pin >= info->pins)
	{
		string_append(dst, "io pin out of range\n");
		return(app_action_error);
	}

	pin_config = &io_config[io][pin];

	io_string_from_mode(dst, pin_config->mode, 0);

	if(pin_config->mode == io_pin_i2c)
	{
		string_append(dst, "/");
		io_string_from_i2c_type(dst, pin_config->shared.i2c.pin_mode);
	}

	if(pin_config->mode == io_pin_lcd)
	{
		string_append(dst, "/");
		io_string_from_lcd_mode(dst, pin_config->shared.lcd.pin_use);
	}

	string_append(dst, ": ");

	if(io_read_pin(dst, io, pin, &value) != io_ok)
		return(app_action_error);

	string_format(dst, "[%u]\n", value);

	return(app_action_normal);
}

irom app_action_t application_function_io_write(const string_t *src, string_t *dst)
{
	const io_info_entry_t *info;
	io_config_pin_entry_t *pin_config;
	unsigned int io, pin;
	uint32_t value;

	if(parse_uint(1, src, &io, 0, ' ') != parse_ok)
	{
		string_append(dst, "io-write <io> <pin> <value>\n");
		return(app_action_error);
	}

	if(io >= io_id_size)
	{
		string_format(dst, "invalid io %d\n", io);
		return(app_action_error);
	}

	info = &io_info[io];

	if(parse_uint(2, src, &pin, 0, ' ') != parse_ok)
	{
		string_append(dst, "io-write <io> <pin> <value>\n");
		return(app_action_error);
	}

	if(pin >= info->pins)
	{
		string_append(dst, "invalid pin\n");
		return(app_action_error);
	}

	pin_config = &io_config[io][pin];

	value = 0;
	parse_uint(3, src, &value, 0, ' ');

	io_string_from_mode(dst, pin_config->mode, 0);

	if(pin_config->mode == io_pin_lcd)
	{
		string_append(dst, "/");
		io_string_from_lcd_mode(dst, pin_config->shared.lcd.pin_use);
	}

	string_append(dst, ": ");

	if(io_write_pin(dst, io, pin, value) != io_ok)
	{
		string_append(dst, "\n");
		return(app_action_error);
	}

	if(io_read_pin(dst, io, pin, &value) != io_ok)
	{
		string_append(dst, "\n");
		return(app_action_error);
	}

	string_format(dst, "[%d]\n", value);

	return(app_action_normal);
}

irom app_action_t application_function_io_trigger(const string_t *src, string_t *dst)
{
	const io_info_entry_t *info;
	unsigned int io, pin;
	io_trigger_t trigger_type;

	if(parse_uint(1, src, &io, 0, ' ') != parse_ok)
	{
		string_clear(dst);
		trigger_usage(dst);
		return(app_action_normal);
	}

	if(io >= io_id_size)
	{
		string_format(dst, "invalid io %d\n", io);
		return(app_action_error);
	}

	info = &io_info[io];

	if(parse_uint(2, src, &pin, 0, ' ') != parse_ok)
	{
		string_clear(dst);
		trigger_usage(dst);
		return(app_action_normal);
	}

	if(pin >= info->pins)
	{
		string_append(dst, "invalid pin\n");
		return(app_action_error);
	}

	if(parse_string(3, src, dst, ' ') != parse_ok)
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

	string_append(dst, "trigger ");
	trigger_action_to_string(dst, trigger_type);
	string_format(dst, " %u/%u: ", io, pin);

	if(io_trigger_pin(dst, io, pin, trigger_type) != io_ok)
	{
		string_append(dst, "\n");
		return(app_action_error);
	}

	string_append(dst, "ok\n");

	return(app_action_normal);
}

irom static app_action_t application_function_io_clear_set_flag(const string_t *src, string_t *dst, uint32_t value)
{
	const io_info_entry_t *info;
	io_data_entry_t *data;
	io_data_pin_entry_t *pin_data;
	io_config_pin_entry_t *pin_config;
	unsigned int io, pin;
	io_pin_flag_t saved_flags;
	io_pin_flag_to_int_t io_pin_flag_to_int;
	string_init(varname_io_flags, "io.%u.%u.flags");

	if(parse_uint(1, src, &io, 0, ' ') != parse_ok)
	{
		string_append(dst, "io-flag <io> <pin> <flag>\n");
		return(app_action_error);
	}

	if(io >= io_id_size)
	{
		string_format(dst, "invalid io %d\n", io);
		return(app_action_error);
	}

	info = &io_info[io];
	data = &io_data[io];

	if(parse_uint(2, src, &pin, 0, ' ') != parse_ok)
	{
		string_append(dst, "io-flag <io> <pin> <flag>\n");
		return(app_action_error);
	}

	if(pin >= info->pins)
	{
		string_append(dst, "invalid pin\n");
		return(app_action_error);
	}

	pin_data = &data->pin[pin];
	pin_config = &io_config[io][pin];

	saved_flags = pin_config->flags;

	if((parse_string(3, src, dst, ' ') == parse_ok) && !pin_flag_from_string(dst, pin_config, !!value))
	{
		string_clear(dst);
		string_append(dst, "io-flag <io> <pin> <flag>\n");
		return(app_action_error);
	}

	if(pin_config->flags.pullup && !info->caps.pullup)
	{
		pin_config->flags = saved_flags;
		string_clear(dst);
		string_append(dst, "io does not support pullup\n");
		return(app_action_error);
	}

	if(info->init_pin_mode_fn && (info->init_pin_mode_fn(dst, info, pin_data, pin_config, pin) != io_ok))
	{
		pin_config->flags = saved_flags;
		string_clear(dst);
		string_append(dst, "cannot enable this flag\n");
		return(app_action_error);
	}

	io_pin_flag_to_int.io_pin_flags = pin_config->flags;
	config_set_int(&varname_io_flags, io, pin, io_pin_flag_to_int.intvalue);

	string_clear(dst);
	string_format(dst, "flags for pin %d/%d:", io, pin);

	pin_string_from_flags(dst, pin_config);

	string_append(dst, "\n");

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
	ds_id_pin_1,
	ds_id_pin_2,
	ds_id_flags_1,
	ds_id_flags_2,
	ds_id_mode_1,
	ds_id_mode_2,
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
	ds_id_ledpixel,
	ds_id_lcd,
	ds_id_unknown,
	ds_id_not_detected,
	ds_id_info_1,
	ds_id_info_2,
	ds_id_pins_header,
	ds_id_hw_1,
	ds_id_hw_2,
	ds_id_table_start,
	ds_id_table_end,
	ds_id_error,

	ds_id_length,
	ds_id_invalid = ds_id_length
} dump_string_id_t;

typedef const char string_array_t[ds_id_length][256];

typedef struct {
	const string_array_t plain;
	const string_array_t html;
} dump_string_t;

static const roflash dump_string_t roflash_dump_strings =
{
	.plain =
	{
		/* ds_id_io */				"io[%d]: %s@%02x\n",
		/* ds_id_pin_1 */			"  pin: %2d, ",
		/* ds_id_pin_2 */			"\n",
		/* ds_id_flags_1 */			" flags: [",
		/* ds_id_flags_2 */			"], ",
		/* ds_id_mode_1 */			"mode: ",
		/* ds_id_mode_2 */			"",
		/* ds_id_disabled */		"",
		/* ds_id_input */			"state: %s",
		/* ds_id_counter */			"counter: %d, debounce: %d",
		/* ds_id_trigger_1 */		"trigger, counter: %d, debounce: %d\n",
		/* ds_id_trigger_2 */		"             action #%d: io: %d, pin: %d, action: ",
		/* ds_id_trigger_3 */		"",
		/* ds_id_output */			"output, state: %s",
		/* ds_id_timer */			"config direction: %s, speed: %d ms, current direction: %s, delay: %d ms, state: %s",
		/* ds_id_analog_output */	"analog output, min/static: %u, max: %u, current speed: %d, direction: %s, value: %u, saved value: %u",
		/* ds_id_i2c_sda */			"sda",
		/* ds_id_i2c_scl */			"scl",
		/* ds_id_uart */			"uart",
		/* ds_id_ledpixel */		"ledpixel",
		/* ds_id_lcd */				"lcd",
		/* ds_id_unknown */			"unknown",
		/* ds_id_not_detected */	"  not found\n",
		/* ds_id_info_1 */			", info: ",
		/* ds_id_info_2 */			"",
		/* ds_id_pins_header */		"",
		/* ds_id_hw_1 */			" [hw: ",
		/* ds_id_hw_2 */			"]",
		/* ds_id_table_start */		"",
		/* ds_id_table_end */		"",
		/* ds_id_error */			"error",
	},

	.html =
	{
		/* ds_id_io */				"<tr><th colspan=\"6\" align=\"center\">IO %d: %s@%02x</th></tr>\n",
		/* ds_id_pin_1 */			"<tr><td align=\"center\">%d</td>",
		/* ds_id_pin_2 */			"</tr>\n",
		/* ds_id_flags_1 */			"<td>",
		/* ds_id_flags_2 */			"</td>",
		/* ds_id_mode_1 */			"<td>",
		/* ds_id_mode_2 */			"</td>",
		/* ds_id_disabled */		"<td></td>",
		/* ds_id_input */			"<td>state: %s</td>",
		/* ds_id_counter */			"<td><td>counter: %d</td><td>debounce: %d</td>",
		/* ds_id_trigger_1 */		"<td>counter: %d, debounce: %d, ",
		/* ds_id_trigger_2 */		"action: #%d, io: %d, pin: %d, trigger action: ",
		/* ds_id_trigger_3 */		"</td>",
		/* ds_id_output */			"<td>output</td><td>state: %s</td>",
		/* ds_id_timer */			"<td>config direction: %s, speed: %d ms, current direction %s, delay: %d ms, state: %s</td>",
		/* ds_id_analog_output */	"<td>min/static: %u, max: %u, speed: %d, current direction: %s, value: %u, saved value: %u",
		/* ds_id_i2c_sda */			"<td>sda</td>",
		/* ds_id_i2c_scl */			"<td>scl</td>",
		/* ds_id_uart */			"<td>uart</td>",
		/* ds_id_ledpixel */		"<td>ledpixel</td>",
		/* ds_id_lcd */				"<td>lcd</td>",
		/* ds_id_unknown */			"<td>unknown</td>",
		/* ds_id_not_detected */	"<tr><td colspan=\"6\">not connected</td></tr>\n",
		/* ds_id_info_1 */			"<td>",
		/* ds_id_info 2 */			"</td>",
		/* ds_id_pins_header */		"<tr><th>pin</th><th>mode</th><th>hw mode</th><th>flags</th><th>pin info</th><th>extra info</th></tr>\n",
		/* ds_id_hw_1 */			"<td>",
		/* ds_id_hw_2 */			"</td>",
		/* ds_id_table_start */		"<table border=\"1\" cellpadding=\"1\" cellspacing=\"1\">\n",
		/* ds_id_table_end */		"</table>\n",
		/* ds_id_error */			"<td>error</td>",
	}
};

irom void io_config_dump(string_t *dst, int io_id, int pin_id, bool_t html)
{
	const io_info_entry_t *info;
	io_data_entry_t *data;
	io_data_pin_entry_t *pin_data;
	const io_config_pin_entry_t *pin_config;
	const string_array_t *roflash_strings;
	int io, pin;
	uint32_t value;
	io_error_t error;

	if(html)
		roflash_strings = &roflash_dump_strings.html;
	else
		roflash_strings = &roflash_dump_strings.plain;

	string_append_cstr_flash(dst, (*roflash_strings)[ds_id_table_start]);

	for(io = 0; io < io_id_size; io++)
	{
		if((io_id >= 0) && (io_id != io))
			continue;

		info = &io_info[io];
		data = &io_data[io];

		string_format_flash_ptr(dst, (*roflash_strings)[ds_id_io], io, info->name, info->address);

		if(!data->detected)
		{
			string_append_cstr_flash(dst, (*roflash_strings)[ds_id_not_detected]);
			continue;
		}

		string_append_cstr_flash(dst, (*roflash_strings)[ds_id_pins_header]);

		for(pin = 0; pin < info->pins; pin++)
		{
			if((pin_id >= 0) && (pin_id != pin))
				continue;

			pin_config = &io_config[io][pin];
			pin_data = &data->pin[pin];

			string_format_flash_ptr(dst, (*roflash_strings)[ds_id_pin_1], pin);

			string_append_cstr_flash(dst, (*roflash_strings)[ds_id_mode_1]);
			io_string_from_mode(dst, pin_config->mode, -1);
			string_append_cstr_flash(dst, (*roflash_strings)[ds_id_mode_2]);

			string_append_cstr_flash(dst, (*roflash_strings)[ds_id_hw_1]);
			io_string_from_ll_mode(dst, pin_config->llmode, -1);
			string_append_cstr_flash(dst, (*roflash_strings)[ds_id_hw_2]);

			string_append_cstr_flash(dst, (*roflash_strings)[ds_id_flags_1]);
			pin_string_from_flags(dst, pin_config);
			string_append_cstr_flash(dst, (*roflash_strings)[ds_id_flags_2]);

			if(pin_config->mode != io_pin_disabled)
				if((error = io_read_pin_x(dst, info, pin_data, pin_config, pin, &value)) != io_ok)
					string_append(dst, "\n");
				else
					(void)0;
			else
				error = io_ok;

			switch(pin_config->mode)
			{
				case(io_pin_disabled):
				{
					string_append_cstr_flash(dst, (*roflash_strings)[ds_id_disabled]);

					break;
				}

				case(io_pin_input_digital):
				{
					if(error == io_ok)
						string_format_flash_ptr(dst, (*roflash_strings)[ds_id_input], onoff(value));
					else
						string_append_cstr_flash(dst, (*roflash_strings)[ds_id_error]);

					break;
				}

				case(io_pin_counter):
				{
					if(error == io_ok)
						string_format_flash_ptr(dst, (*roflash_strings)[ds_id_counter], value, pin_config->speed);
					else
						string_append_cstr_flash(dst, (*roflash_strings)[ds_id_error]);

					break;
				}

				case(io_pin_trigger):
				{
					if(error == io_ok)
					{
						string_format_flash_ptr(dst, (*roflash_strings)[ds_id_trigger_1], value, pin_config->speed);
						string_format_flash_ptr(dst, (*roflash_strings)[ds_id_trigger_2], 0,
								pin_config->shared.trigger[0].io.io,
								pin_config->shared.trigger[0].io.pin);
						trigger_action_to_string(dst, pin_config->shared.trigger[0].action);

						if(pin_config->shared.trigger[1].action != io_trigger_none)
						{
							string_append(dst, "\n");
							string_format_flash_ptr(dst, (*roflash_strings)[ds_id_trigger_2], 1,
									pin_config->shared.trigger[1].io.io,
									pin_config->shared.trigger[1].io.pin);
							trigger_action_to_string(dst, pin_config->shared.trigger[1].action);
						}
						string_append_cstr_flash(dst, (*roflash_strings)[ds_id_trigger_3]);
					}
					else
						string_append_cstr_flash(dst, (*roflash_strings)[ds_id_error]);

					break;
				}

				case(io_pin_output_digital):
				{
					if(error == io_ok)
						string_format_flash_ptr(dst, (*roflash_strings)[ds_id_output], onoff(value));
					else
						string_append_cstr_flash(dst, (*roflash_strings)[ds_id_error]);

					break;
				}

				case(io_pin_timer):
				{
					if(error == io_ok)
						string_format_flash_ptr(dst, (*roflash_strings)[ds_id_timer],
								pin_config->direction == io_dir_up ? "up" : (pin_config->direction == io_dir_down ? "down" : "none"),
								pin_config->speed,
								pin_data->direction == io_dir_up ? "up" : (pin_data->direction == io_dir_down ? "down" : "none"),
								pin_data->speed,
								onoff(value));
					else
						string_append_cstr_flash(dst, (*roflash_strings)[ds_id_error]);

					break;
				}

				case(io_pin_output_analog):
				{
					if(error == io_ok)
						string_format_flash_ptr(dst, (*roflash_strings)[ds_id_analog_output],
								pin_config->shared.output_analog.lower_bound,
								pin_config->shared.output_analog.upper_bound,
								pin_config->speed,
								pin_data->direction == io_dir_up ? "up" : (pin_data->direction == io_dir_down ? "down" : "none"),
								value, pin_data->saved_value);
					else
						string_append_cstr_flash(dst, (*roflash_strings)[ds_id_error]);

					break;
				}

				case(io_pin_i2c):
				{
					if(pin_config->shared.i2c.pin_mode == io_i2c_sda)
						string_append_cstr_flash(dst, (*roflash_strings)[ds_id_i2c_sda]);
					else
						string_append_cstr_flash(dst, (*roflash_strings)[ds_id_i2c_scl]);

					break;
				}

				case(io_pin_uart):
				{
					string_append_cstr_flash(dst, (*roflash_strings)[ds_id_uart]);

					break;
				}

				case(io_pin_lcd):
				{
					string_append_cstr_flash(dst, (*roflash_strings)[ds_id_lcd]);
					string_append(dst, "/");
					io_string_from_lcd_mode(dst, pin_config->shared.lcd.pin_use);

					break;
				}

				case(io_pin_ledpixel):
				{
					string_append_cstr_flash(dst, (*roflash_strings)[ds_id_ledpixel]);

					break;
				}

				default:
				{
					string_append_cstr_flash(dst, (*roflash_strings)[ds_id_unknown]);

					break;
				}
			}

			string_append_cstr_flash(dst, (*roflash_strings)[ds_id_info_1]);
			if(info->get_pin_info_fn)
				info->get_pin_info_fn(dst, info, pin_data, pin_config, pin);
			string_append_cstr_flash(dst, (*roflash_strings)[ds_id_info_2]);

			string_format_flash_ptr(dst, (*roflash_strings)[ds_id_pin_2], pin);
		}
	}

	string_append_cstr_flash(dst, (*roflash_strings)[ds_id_table_end]);
}
