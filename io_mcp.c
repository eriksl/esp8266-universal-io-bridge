#include "io_mcp.h"
#include "i2c.h"
#include "spi.h"
#include "dispatch.h"
#include "util.h"

#include <stdlib.h>
#include <stdint.h>

typedef enum
{
	iocon_intpol_low =			0 << 1,
	iocon_intpol_high =			1 << 1,
	iocon_int_no_open_drain =	0 << 2,
	iocon_int_open_drain =		1 << 2,
	iocon_no_haen =				0 << 3,
	iocon_haen =				1 << 3,
	iocon_no_disslw =			0 << 4,
	iocon_disslw =				1 << 4,
	iocon_no_seqop =			1 << 5,
	iocon_seqop =				0 << 5,
	iocon_no_int_mirror =		0 << 6,
	iocon_int_mirror =			1 << 6,
	iocon_nobank =				0 << 7,
	iocon_bank =				1 << 7,
} iocon_bit_t;

typedef struct attr_packed
{
	uint16_t	counters;
	uint8_t		pin_output_cache[2];
} mcp_data_instance_t;

assert_size(mcp_data_instance_t, 4);

typedef struct attr_packed
{
	spi_clock_t			speed:8;
	mcp_data_instance_t	instance[io_mcp_instance_size];
} mcp_data_t;

assert_size(mcp_data_t, (4 * io_mcp_instance_size) + 1);

static mcp_data_t mcp_data;

assert_size(mcp_data, (4 * io_mcp_instance_size) + 1);

attr_inline int IODIR(int s)		{ return(0x00 + s);	}
attr_inline int IPOL(int s)			{ return(0x02 + s);	}
attr_inline int GPINTEN(int s)		{ return(0x04 + s);	}
attr_inline int DEFVAL(int s)		{ return(0x06 + s);	}
attr_inline int INTCON(int s)		{ return(0x08 + s);	}
attr_inline int IOCON_linear(int s)	{ return(0x0a + s);	}
attr_inline int IOCON_banked(int s)	{ return(s ? 0x05 : 0x15); }
attr_inline int GPPU(int s)			{ return(0x0c + s);	}
attr_inline int INTF(int s)			{ return(0x0e + s);	}
attr_inline int INTCAP(int s)		{ return(0x10 + s);	}
attr_inline int GPIO(int s)			{ return(0x12 + s);	}
attr_inline int OLAT(int s)			{ return(0x14 + s);	}

static io_error_t read_register(string_t *error_message, const io_info_entry_t *info, unsigned int reg, unsigned int *value)
{
	if(info->address)
	{
		uint8_t i2c_buffer[1];
		i2c_error_t error;

		if((error = i2c_send1_receive(info->address, reg, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		{
			if(error_message)
				i2c_error_format_string(error_message, error);

			return(io_error);
		}

		*value = i2c_buffer[0];
	}
	else
	{
		uint8_t spi_buffer[1];

		if(!spi_start(error_message))
			return(io_error);

		if(!spi_write(8, 0b01000001))
			return(io_error);

		if(!spi_write(8, reg))
			return(io_error);

		if(!spi_transmit(error_message, mcp_data.speed, 0, 0, 0, 0, 0, 1))
			return(io_error);

		if(!spi_receive(error_message, 1, spi_buffer))
			return(io_error);

		if(!spi_finish(error_message))
			return(io_error);

		*value = spi_buffer[0];
	}

	return(io_ok);
}

static io_error_t read_register_4(string_t *error_message, const io_info_entry_t *info, unsigned int reg,
		unsigned int *value_0, unsigned int *value_1, unsigned int *value_2, unsigned int *value_3)
{
	if(info->address)
	{
		uint8_t i2c_buffer[4];
		i2c_error_t error;

		if((error = i2c_send1_receive(info->address, reg, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		{
			if(error_message)
				i2c_error_format_string(error_message, error);

			return(io_error);
		}

		*value_0 = i2c_buffer[0];
		*value_1 = i2c_buffer[1];
		*value_2 = i2c_buffer[2];
		*value_3 = i2c_buffer[3];
	}
	else
	{
		uint8_t spi_buffer[4];

		if(!spi_start(error_message))
			return(io_error);

		if(!spi_write(8, 0b01000001))
			return(io_error);

		if(!spi_write(8, reg))
			return(io_error);

		if(!spi_transmit(error_message, mcp_data.speed, 0, 0, 0, 0, 0, 4))
			return(io_error);

		if(!spi_receive(error_message, 4, spi_buffer))
			return(io_error);

		if(!spi_finish(error_message))
			return(io_error);

		*value_0 = spi_buffer[0];
		*value_1 = spi_buffer[1];
		*value_2 = spi_buffer[2];
		*value_3 = spi_buffer[3];
	}

	return(io_ok);
}

static io_error_t write_register(string_t *error_message, const io_info_entry_t *info, unsigned int reg, unsigned int value)
{
	if(info->address)
	{
		i2c_error_t error;

		if((error = i2c_send2(info->address, reg, value)) != i2c_error_ok)
		{
			if(error_message)
				i2c_error_format_string(error_message, error);

			return(io_error);
		}
	}
	else
	{
		if(!spi_start(error_message))
			return(io_error);

		if(!spi_write(8, 0b01000000))
			return(io_error);

		if(!spi_write(8, reg))
			return(io_error);

		if(!spi_write(8, value))
			return(io_error);

		if(!spi_transmit(error_message, mcp_data.speed, 0, 0, 0, 0, 0, 0))
			return(io_error);

		if(!spi_finish(error_message))
			return(io_error);
	}

	return(io_ok);
}

static io_error_t write_register_2(string_t *error_message, const io_info_entry_t *info, unsigned int reg, unsigned int value_0, unsigned int value_1)
{
	if(info->address)
	{
		i2c_error_t error;

		if((error = i2c_send3(info->address, reg, value_0, value_1)) != i2c_error_ok)
		{
			if(error_message)
				i2c_error_format_string(error_message, error);

			return(io_error);
		}
	}
	else
	{
		if(!spi_start(error_message))
			return(io_error);

		if(!spi_write(8, 0b01000000))
			return(io_error);

		if(!spi_write(8, reg))
			return(io_error);

		if(!spi_write(8, value_0))
			return(io_error);

		if(!spi_write(8, value_1))
			return(io_error);

		if(!spi_transmit(error_message, mcp_data.speed, 0, 0, 0, 0, 0, 0))
			return(io_error);

		if(!spi_finish(error_message))
			return(io_error);
	}

	return(io_ok);
}

static io_error_t clear_set_register(string_t *error_message, const io_info_entry_t *info, unsigned int reg, unsigned int clearmask, unsigned int setmask)
{
	io_error_t error;
	unsigned int value;

	if((error = read_register(error_message, info, reg, &value)) != io_ok)
		return(error);

	value &= ~clearmask;
	value |= setmask;

	if((error = write_register(error_message, info, reg, value)) != io_ok)
		return(error);

	return(io_ok);
}

static io_error_t init(const io_info_entry_t *info)
{
	static const unsigned int iocon_value = iocon_intpol_high | iocon_int_no_open_drain | iocon_no_haen | iocon_disslw | iocon_seqop | iocon_int_mirror | iocon_nobank;
	unsigned int value;

	mcp_data.instance[info->instance].counters = 0;

	// switch to linear mode, assuming config is in banked mode
	// if config was in linear mode already, GPINTEN(1) will be written instead of IOCON
	// fix this after the config is in lineair mode

	if(write_register((string_t *)0, info, IOCON_banked(0), iocon_value) != io_ok)
		return(io_error);

	// config should be in linear mode now

	if(write_register((string_t *)0, info, IOCON_linear(0), iocon_value) != io_ok)
		return(io_error);

	if(read_register((string_t *)0, info, IOCON_linear(1), &value) != io_ok)
		return(io_error);

	if(value != iocon_value)
		return(io_error);

	if(write_register((string_t *)0, info, GPINTEN(0), 0x00) != io_ok)
		return(io_error);

	if(write_register((string_t *)0, info, GPINTEN(1), 0x00) != io_ok)
		return(io_error);

	mcp_data.instance[info->instance].pin_output_cache[0] = 0;
	mcp_data.instance[info->instance].pin_output_cache[1] = 0;

	return(io_ok);
}

static io_error_t init_i2c(const io_info_entry_t *info)
{
	unsigned int value;

	if(info->address == 0)
	{
		log("init mcp i2c: no i2c address\n");
		return(io_error);
	}

	// TEST i2c bus and device

	if(read_register((string_t *)0, info, IOCON_banked(0), &value) != io_ok)
		return(io_error);

	return(init(info));
}

static io_error_t init_spi(const io_info_entry_t *info)
{
	unsigned int enabled;
	int cs_io, cs_pin, speed;
	string_new(, error, 64);

	if(info->address != 0)
	{
		log("init mcp spi: i2c address set\n");
		return(io_error);
	}

	if(!config_get_uint("mcpspi.enabled", &enabled, -1, -1) || !enabled)
		return(io_error);

	if(!config_get_int("mcpspi.cs.io", &cs_io, -1, -1))
		cs_io = -1;

	if(!config_get_int("mcpspi.cs.pin", &cs_pin, -1, -1))
		cs_pin = -1;

	if(!config_get_int("mcpspi.speed", &speed, -1, -1))
		speed = spi_clock_5M;

	if(!spi_configure(&error, spi_mode_0, false, cs_io, cs_pin))
	{
		log("init mcp spi: spi_configure: %s\n", string_to_cstr(&error));
		return(io_error);
	}

	mcp_data.speed = speed;

	return(init(info));
}

static attr_pure unsigned int pin_max_value(const io_info_entry_t *info, io_data_pin_entry_t *data, const io_config_pin_entry_t *pin_config, unsigned int pin)
{
	unsigned int value = 0;

	switch(pin_config->llmode)
	{
		case(io_pin_ll_input_digital):
		case(io_pin_ll_output_digital):
		{
			value = 0x01;
			break;
		}

		case(io_pin_ll_counter):
		{
			value = ~0;
			break;
		}

		default:
			break;
	}

	return(value);
}

static void periodic_slow(int io, const io_info_entry_t *info, io_data_entry_t *data, unsigned int rate_ms)
{
	unsigned int intf[2];
	unsigned int intcap[2];

	if(!mcp_data.instance[info->instance].counters)
		return;

	if(read_register_4((string_t *)0, info, INTF(0), &intf[0], &intf[1], &intcap[0], &intcap[1]) != io_ok) // INTFA, INTFB, INTCAPA, INTCAPB
		return;

	if((intf[0] != 0) || (intf[1] != 0))
		dispatch_post_task(task_prio_low, task_pins_changed_mcp,
				(intf[1] << 8) | (intf[0] << 0),  (intcap[1] << 8) | (intcap[0] << 0), info->id);
}

static void pin_change_handler(int io, io_info_entry_t *info, io_data_entry_t *data)
{
	periodic_slow(io, info, data, 0);
}

void io_mcp_pins_changed(uint32_t pin_status_mask, uint16_t pin_value_mask, uint8_t io)
{
	io_config_pin_entry_t *pin_config;
	unsigned int pin;

	if(io >= io_id_size)
	{
		log("[mcp] pin change: invalid io\n");
		return;
	}

	for(pin = 0; pin < io_mcp_pin_size; pin++)
	{
		if(!(pin_status_mask & (1 << pin)))
			continue;

		pin_config = &io_config[io][pin];

		if(pin_config->llmode != io_pin_ll_counter)
			continue;

		io_pin_changed(io, pin, pin_value_mask);
	}
}

static io_error_t pin_mode(string_t *error_message, const io_info_entry_t *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin)
{
	int bank, bankpin;

	bank = (pin & 0x08) >> 3;
	bankpin = pin & 0x07;
	mcp_data.instance[info->instance].counters &= ~(1 << pin);

	if((pin_config->llmode == io_pin_ll_input_digital) && (pin_config->flags & io_flag_invert))
	{
		if(clear_set_register(error_message, info, IPOL(bank), 0, 1 << bankpin) != io_ok) // input polarity inversion = 1
			return(io_error);
	}
	else
	{
		if(clear_set_register(error_message, info, IPOL(bank), 1 << bankpin, 0) != io_ok) // input polarity inversion = 0
			return(io_error);
	}

	if(clear_set_register(error_message, info, GPINTEN(bank), 1 << bankpin, 0) != io_ok) // pc int enable = 0
		return(io_error);

	if(clear_set_register(error_message, info, DEFVAL(bank), 1 << bankpin, 0) != io_ok) // compare value = 0
		return(io_error);

	if(clear_set_register(error_message, info, INTCON(bank), 1 << bankpin, 0) != io_ok) // compare source = 0
		return(io_error);

	if(clear_set_register(error_message, info, GPPU(bank), 1 << bankpin, 0) != io_ok) // pullup = 0
		return(io_error);

	if(clear_set_register(error_message, info, GPIO(bank), 1 << bankpin, 0) != io_ok) // gpio = 0
		return(io_error);

	if(clear_set_register(error_message, info, OLAT(bank), 1 << bankpin, 0) != io_ok) // latch = 0
		return(io_error);

	if(clear_set_register(error_message, info, IODIR(bank), 0, 1 << bankpin) != io_ok) // direction = 1
		return(io_error);

	switch(pin_config->llmode)
	{
		case(io_pin_ll_disabled):
		{
			break;
		}

		case(io_pin_ll_counter):
		{
			mcp_data.instance[info->instance].counters |= (1 << pin);
			[[fallthrough]];
		}

		case(io_pin_ll_input_digital):
		{
			if((pin_config->flags & io_flag_pullup) && (clear_set_register(error_message, info, GPPU(bank), 0, 1 << bankpin) != io_ok))
				return(io_error);

			if((pin_config->llmode == io_pin_ll_counter) && (clear_set_register(error_message, info, GPINTEN(bank), 0, 1 << bankpin) != io_ok)) // pc int enable = 1
				return(io_error);

			break;
		}

		case(io_pin_ll_output_digital):
		{
			if(clear_set_register(error_message, info, IODIR(bank), 1 << bankpin, 0) != io_ok) // direction = 0
				return(io_error);

			break;
		}

		default:
		{
			if(error_message)
				string_append(error_message, "invalid mode for this pin\n");

			return(io_error);
		}
	}

	return(io_ok);
}

static io_error_t get_pin_info(string_t *dst, const io_info_entry_t *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin)
{
	int bank, bankpin;
	unsigned int tv;
	int io, olat, cached;

	bank = (pin & 0x08) >> 3;
	bankpin = pin & 0x07;

	tv = 0;

	switch(pin_config->llmode)
	{
		case(io_pin_ll_input_analog):
		{
			if(read_register(dst, info, GPIO(bank), &tv) != io_ok)
				return(io_error);

			string_format(dst, "current io: %s", onoff(tv & (1 << bankpin)));

			break;
		}

		case(io_pin_ll_counter):
		{
			if(read_register(dst, info, GPIO(bank), &tv) != io_ok)
				return(io_error);

			string_format(dst, "current io: %s", onoff(tv & (1 << bankpin)));

			break;
		}

		case(io_pin_ll_output_digital):
		{
			if(read_register(dst, info, GPIO(bank), &tv) != io_ok)
				return(io_error);

			io = tv & (1 << bankpin);

			if(read_register(dst, info, OLAT(bank), &tv) != io_ok)
				return(io_error);

			olat = tv & (1 << bankpin);
			cached = mcp_data.instance[info->instance].pin_output_cache[bank] & (1 << bankpin);

			string_format(dst, "latch: %s, io: %s, cache: %s", onoff(io), onoff(olat), onoff(cached));

			break;
		}

		default:
		{
		}
	}

	return(io_ok);
}

static io_error_t read_pin(string_t *error_message, const io_info_entry_t *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin, unsigned int *value)
{
	int bank, bankpin;
	unsigned int tv;

	bank = (pin & 0x08) >> 3;
	bankpin = pin & 0x07;

	tv = 0;

	switch(pin_config->llmode)
	{
		case(io_pin_ll_input_digital):
		case(io_pin_ll_output_digital):
		{
			if(read_register(error_message, info, GPIO(bank), &tv) != io_ok)
				return(io_error);

			*value = !!(tv & (1 << bankpin));

			if((pin_config->llmode == io_pin_ll_output_digital) && (pin_config->flags & io_flag_invert))
				*value = !*value;

			break;
		}

		default:
		{
			if(error_message)
				string_append(error_message, "invalid mode for this pin\n");

			return(io_error);
		}
	}

	return(io_ok);
}

static io_error_t write_pin(string_t *error_message, const io_info_entry_t *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin, unsigned int value)
{
	int bank, bankpin;

	bank = (pin & 0x08) >> 3;
	bankpin = pin & 0x07;

	if((pin_config->llmode == io_pin_ll_output_digital) && (pin_config->flags & io_flag_invert))
		value = !value;

	if(value)
		mcp_data.instance[info->instance].pin_output_cache[bank] |= 1 << bankpin;
	else
		mcp_data.instance[info->instance].pin_output_cache[bank] &= ~(1 << bankpin);

	switch(pin_config->llmode)
	{
		case(io_pin_ll_output_digital):
		{
			if(write_register(error_message, info, GPIO(bank),
						mcp_data.instance[info->instance].pin_output_cache[bank]) != io_ok)
				return(io_error);

			break;
		}

		default:
		{
			if(error_message)
				string_append(error_message, "invalid mode for this pin\n");

			return(io_error);
		}
	}

	return(io_ok);
}

static io_error_t set_mask(string_t *error_message, const io_info_entry_t *info, unsigned int mask, unsigned int pins)
{
	mcp_data.instance[info->instance].pin_output_cache[0] &= ~((mask & 0x00ff) >> 0);
	mcp_data.instance[info->instance].pin_output_cache[1] &= ~((mask & 0xff00) >> 8);
	mcp_data.instance[info->instance].pin_output_cache[0] |= (pins & 0x00ff) >> 0;
	mcp_data.instance[info->instance].pin_output_cache[1] |= (pins & 0xff00) >> 8;

	if(write_register_2(error_message, info, GPIO(0), mcp_data.instance[info->instance].pin_output_cache[0], mcp_data.instance[info->instance].pin_output_cache[1]) != io_ok)
		return(io_error);

	return(io_ok);
}

roflash const io_info_entry_t io_info_entry_mcp_20 =
{
	io_id_mcp_20, /* = 2 */
	0x20,
	io_mcp_instance_20,
	16,
	caps_input_digital |
		caps_counter |
		caps_output_digital |
		caps_pullup |
		caps_rotary_encoder,
	"MCP23017 I2C I/O expander #1",
	init_i2c,
	(void *)0, // postinit
	pin_max_value,
	periodic_slow,
	(void *)0, // periodic fast
	pin_change_handler,
	pin_mode,
	get_pin_info,
	read_pin,
	write_pin,
	set_mask,
};

roflash const io_info_entry_t io_info_entry_mcp_21 =
{
	io_id_mcp_21, /* = 3 */
	0x21,
	io_mcp_instance_21,
	16,
	caps_input_digital |
		caps_counter |
		caps_output_digital |
		caps_pullup |
		caps_rotary_encoder,
	"MCP23017 I2C I/O expander #2",
	init_i2c,
	(void *)0, // postinit
	pin_max_value,
	periodic_slow,
	(void *)0, // periodic fast
	pin_change_handler,
	pin_mode,
	get_pin_info,
	read_pin,
	write_pin,
	set_mask,
};

roflash const io_info_entry_t io_info_entry_mcp_22 =
{
	io_id_mcp_22, /* = 4 */
	0x22,
	io_mcp_instance_22,
	16,
	caps_input_digital |
		caps_counter |
		caps_output_digital |
		caps_pullup |
		caps_rotary_encoder,
	"MCP23017 I2C I/O expander #3",
	init_i2c,
	(void *)0, // postinit
	pin_max_value,
	periodic_slow,
	(void *)0, // periodic fast
	pin_change_handler,
	pin_mode,
	get_pin_info,
	read_pin,
	write_pin,
	set_mask,
};

roflash const io_info_entry_t io_info_entry_mcp_spi =
{
	io_id_mcp_spi, /* = 8 */
	0x00,
	io_mcp_instance_spi,
	16,
	caps_input_digital |
		caps_counter |
		caps_output_digital |
		caps_pullup |
		caps_rotary_encoder,
	"MCP23S17 SPI I/O expander",
	init_spi,
	(void *)0, // postinit
	pin_max_value,
	periodic_slow,
	(void *)0, // periodic fast
	pin_change_handler,
	pin_mode,
	get_pin_info,
	read_pin,
	write_pin,
	set_mask,
};

roflash const char help_description_io_mcp_spi[] = "enable MCP23S17, use io-mcp-spi <enable=0|1> [<spi cs io> <spi cs pin> [<speed>]]";

app_action_t application_function_io_mcp_spi(app_params_t *parameters)
{
	unsigned int enabled, speed;
	int cs_io, cs_pin;

	if(parse_uint(1, parameters->src, &enabled, 0, ' ') == parse_ok)
	{
		if(parse_int(2, parameters->src, &cs_io, 0, ' ') != parse_ok)
			cs_io = -1;

		if(parse_int(3, parameters->src, &cs_pin, 0, ' ') != parse_ok)
			cs_pin = -1;

		if((parse_uint(4, parameters->src, &speed, 0, ' ') != parse_ok) || (speed >= spi_clock_size))
			speed = spi_clock_none;

		if(!config_open_write())
			goto config_error;

		if(!enabled)
			config_delete("mcpspi.", true, -1, -1);
		else
		{
			if(!config_set_uint("mcpspi.enabled", 1, -1, -1))
				goto config_error;

			if((cs_io < 0) || (cs_pin < 0))
				config_delete("mcpspi.cs.", true, -1, -1);
			else
			{
				if(!config_set_uint("mcpspi.cs.io", cs_io, -1, -1))
					goto config_error;
				if(!config_set_uint("mcpspi.cs.pin", cs_pin, -1, -1))
					goto config_error;
			}

			if(speed == spi_clock_none)
				config_delete("mcpspi.speed", false, -1, -1);
			else
				if(!config_set_uint("mcpspi.speed", speed, -1, -1))
					goto config_error;
		}

		if(!config_close_write())
			goto config_error;
	}

	if(!config_get_uint("mcpspi.enabled", &enabled, -1, -1))
		enabled = 0;

	if(!enabled)
	{
		string_format(parameters->dst, "no mcp io with spi interface configured\n");
		return(app_action_error);
	}

	if(!config_get_int("mcpspi.cs.io", &cs_io, -1, -1))
		cs_io = -1;

	if(!config_get_int("mcpspi.cs.pin", &cs_pin, -1, -1))
		cs_pin = -1;

	if(!config_get_uint("mcpspi.speed", &speed, -1, -1))
		speed = spi_clock_none;

	string_format(parameters->dst, "mcp with spi interface configured\n");
	string_append(parameters->dst, "cs: ");

	if((cs_io >= 0) && (cs_pin >= 0))
		string_format(parameters->dst, "%d/%d\n", cs_io, cs_pin);
	else
		string_append(parameters->dst, "default\n");

	string_append(parameters->dst, "speed: ");

	if(speed != spi_clock_none)
		string_format(parameters->dst, "%u\n", speed);
	else
		string_append(parameters->dst, "default\n");

	return(app_action_normal);

config_error:
	config_abort_write();
	string_clear(parameters->dst);
	string_append(parameters->dst, "> cannot set config\n");
	return(app_action_error);
}
