#include "io_mcp.h"
#include "i2c.h"
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

static uint32_t counters[io_mcp_instance_size];
static uint8_t pin_output_cache[io_mcp_instance_size][2];

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

attr_inline io_error_t read_register(string_t *error_message, int address, int reg, int *value)
{
	uint8_t i2cbuffer[1];
	i2c_error_t error;

	if((error = i2c_send1_receive(address, reg, sizeof(i2cbuffer), i2cbuffer)) != i2c_error_ok)
	{
		if(error_message)
			i2c_error_format_string(error_message, error);

		return(io_error);
	}

	*value = i2cbuffer[0];

	return(io_ok);
}

attr_inline io_error_t write_register(string_t *error_message, int address, int reg, int value)
{
	i2c_error_t error;

	if((error = i2c_send2(address, reg, value)) != i2c_error_ok)
	{
		if(error_message)
			i2c_error_format_string(error_message, error);

		return(io_error);
	}

	return(io_ok);
}

attr_inline io_error_t clear_set_register(string_t *error_message, int address, int reg, int clearmask, int setmask)
{
	uint8_t i2cbuffer[1];
	i2c_error_t error;

	if((error = i2c_send1_receive(address, reg, sizeof(i2cbuffer), i2cbuffer)) != i2c_error_ok)
	{
		if(error_message)
			i2c_error_format_string(error_message, error);

		return(io_error);
	}

	i2cbuffer[0] &= ~clearmask;
	i2cbuffer[0] |= setmask;

	if((error = i2c_send2(address, reg, i2cbuffer[0])) != i2c_error_ok)
	{
		if(error_message)
			i2c_error_format_string(error_message, error);

		return(io_error);
	}

	return(io_ok);
}

static io_error_t init(const struct io_info_entry_T *info)
{
	static const unsigned int iocon_value = iocon_intpol_low | iocon_int_open_drain | iocon_no_haen | iocon_disslw | iocon_seqop | iocon_int_mirror | iocon_nobank;
	uint8_t i2c_buffer[1];

	counters[info->instance] = 0;

	// switch to linear mode, assuming config is in banked mode
	// if config was in linear mode already, GPINTEN(1) will be written instead of IOCON
	// fix this after the config is in lineair mode

	if(i2c_send2(info->address, IOCON_banked(0), iocon_value) != i2c_error_ok)
		return(io_error);

	// config should be in linear mode now

	if(i2c_send2(info->address, IOCON_linear(0), iocon_value) != i2c_error_ok)
		return(io_error);

	if(i2c_send1_receive(info->address, IOCON_linear(1), sizeof(i2c_buffer), i2c_buffer) != i2c_error_ok)
		return(io_error);

	if(i2c_buffer[0] != iocon_value)
		return(io_error);

	if(i2c_send2(info->address, GPINTEN(0), 0x00) != i2c_error_ok)
		return(io_error);

	if(i2c_send2(info->address, GPINTEN(1), 0x00) != i2c_error_ok)
		return(io_error);

	pin_output_cache[info->instance][0] = 0;
	pin_output_cache[info->instance][1] = 0;

	return(io_ok);
}

static attr_pure unsigned int pin_max_value(const struct io_info_entry_T *info, io_data_pin_entry_t *data, const io_config_pin_entry_t *pin_config, unsigned int pin)
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

static void periodic_slow(int io, const struct io_info_entry_T *info, io_data_entry_t *data, unsigned int rate_ms)
{
	uint8_t i2c_buffer[4];
	unsigned int intf[2];
	unsigned int intcap[2];

	if(!counters[info->instance])
		return;

	if(i2c_send1_receive(info->address, INTF(0), sizeof(i2c_buffer), i2c_buffer) != i2c_error_ok) // INTFA, INTFB, INTCAPA, INTCAPB
		return;

	intf[0] = i2c_buffer[0];
	intf[1] = i2c_buffer[1];
	intcap[0] = i2c_buffer[2];
	intcap[1] = i2c_buffer[3];

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

static io_error_t pin_mode(string_t *error_message, const struct io_info_entry_T *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin)
{
	int bank, bankpin;

	bank = (pin & 0x08) >> 3;
	bankpin = pin & 0x07;
	counters[info->instance] &= ~(1 << pin);

	if((pin_config->llmode == io_pin_ll_input_digital) && (pin_config->flags & io_flag_invert))
	{
		if(clear_set_register(error_message, info->address, IPOL(bank), 0, 1 << bankpin) != io_ok) // input polarity inversion = 1
			return(io_error);
	}
	else
	{
		if(clear_set_register(error_message, info->address, IPOL(bank), 1 << bankpin, 0) != io_ok) // input polarity inversion = 0
			return(io_error);
	}

	if(clear_set_register(error_message, info->address, GPINTEN(bank), 1 << bankpin, 0) != io_ok) // pc int enable = 0
		return(io_error);

	if(clear_set_register(error_message, info->address, DEFVAL(bank), 1 << bankpin, 0) != io_ok) // compare value = 0
		return(io_error);

	if(clear_set_register(error_message, info->address, INTCON(bank), 1 << bankpin, 0) != io_ok) // compare source = 0
		return(io_error);

	if(clear_set_register(error_message, info->address, GPPU(bank), 1 << bankpin, 0) != io_ok) // pullup = 0
		return(io_error);

	if(clear_set_register(error_message, info->address, GPIO(bank), 1 << bankpin, 0) != io_ok) // gpio = 0
		return(io_error);

	if(clear_set_register(error_message, info->address, OLAT(bank), 1 << bankpin, 0) != io_ok) // latch = 0
		return(io_error);

	if(clear_set_register(error_message, info->address, IODIR(bank), 0, 1 << bankpin) != io_ok) // direction = 1
		return(io_error);

	switch(pin_config->llmode)
	{
		case(io_pin_ll_disabled):
		{
			break;
		}

		case(io_pin_ll_counter):
		{
			counters[info->instance] |= (1 << pin);
			[[fallthrough]];
		}

		case(io_pin_ll_input_digital):
		{
			if((pin_config->flags & io_flag_pullup) && (clear_set_register(error_message, info->address, GPPU(bank), 0, 1 << bankpin) != io_ok))
				return(io_error);

			if((pin_config->llmode == io_pin_ll_counter) && (clear_set_register(error_message, info->address, GPINTEN(bank), 0, 1 << bankpin) != io_ok)) // pc int enable = 1
				return(io_error);

			break;
		}

		case(io_pin_ll_output_digital):
		{
			if(clear_set_register(error_message, info->address, IODIR(bank), 1 << bankpin, 0) != io_ok) // direction = 0
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

static io_error_t get_pin_info(string_t *dst, const struct io_info_entry_T *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin)
{
	int bank, bankpin, tv;
	int io, olat, cached;

	bank = (pin & 0x08) >> 3;
	bankpin = pin & 0x07;

	tv = 0;

	switch(pin_config->llmode)
	{
		case(io_pin_ll_input_analog):
		{
			if(read_register(dst, info->address, GPIO(bank), &tv) != io_ok)
				return(io_error);

			string_format(dst, "current io: %s", onoff(tv & (1 << bankpin)));

			break;
		}

		case(io_pin_ll_counter):
		{
			if(read_register(dst, info->address, GPIO(bank), &tv) != io_ok)
				return(io_error);

			string_format(dst, "current io: %s", onoff(tv & (1 << bankpin)));

			break;
		}

		case(io_pin_ll_output_digital):
		{
			if(read_register(dst, info->address, GPIO(bank), &tv) != io_ok)
				return(io_error);

			io = tv & (1 << bankpin);

			if(read_register(dst, info->address, OLAT(bank), &tv) != io_ok)
				return(io_error);

			olat = tv & (1 << bankpin);
			cached = pin_output_cache[info->instance][bank] & (1 << bankpin);

			string_format(dst, "latch: %s, io: %s, cache: %s", onoff(io), onoff(olat), onoff(cached));

			break;
		}

		default:
		{
		}
	}

	return(io_ok);
}

static io_error_t read_pin(string_t *error_message, const struct io_info_entry_T *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin, unsigned int *value)
{
	int bank, bankpin, tv;

	bank = (pin & 0x08) >> 3;
	bankpin = pin & 0x07;

	tv = 0;

	switch(pin_config->llmode)
	{
		case(io_pin_ll_input_digital):
		case(io_pin_ll_output_digital):
		{
			if(read_register(error_message, info->address, GPIO(bank), &tv) != io_ok)
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

static io_error_t write_pin(string_t *error_message, const struct io_info_entry_T *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin, unsigned int value)
{
	int bank, bankpin;

	bank = (pin & 0x08) >> 3;
	bankpin = pin & 0x07;

	if((pin_config->llmode == io_pin_ll_output_digital) && (pin_config->flags & io_flag_invert))
		value = !value;

	if(value)
		pin_output_cache[info->instance][bank] |= 1 << bankpin;
	else
		pin_output_cache[info->instance][bank] &= ~(1 << bankpin);

	switch(pin_config->llmode)
	{
		case(io_pin_ll_output_digital):
		{
			if(write_register(error_message, info->address, GPIO(bank),
						pin_output_cache[info->instance][bank]) != io_ok)
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

static io_error_t set_mask(string_t *error_message, const struct io_info_entry_T *info, unsigned int mask, unsigned int pins)
{
	unsigned int index = info->instance;

	pin_output_cache[index][0] &= ~((mask & 0x00ff) >> 0);
	pin_output_cache[index][1] &= ~((mask & 0xff00) >> 8);
	pin_output_cache[index][0] |= (pins & 0x00ff) >> 0;
	pin_output_cache[index][1] |= (pins & 0xff00) >> 8;

	if(i2c_send3(info->address, GPIO(0), pin_output_cache[index][0], pin_output_cache[index][1]) != i2c_error_ok)
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
	init,
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
	init,
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
	init,
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
