#include "io_mcp.h"
#include "io_config.h"
#include "i2c.h"
#include "util.h"

#include <user_interface.h>

#include <stdlib.h>

typedef struct
{
	uint32_t counter;
	uint32_t debounce;
} mcp_data_pin_t;

static mcp_data_pin_t mcp_data_pin_table[io_mcp_instance_size][16];

irom static io_error_t readregister(string_t *error_message, int address, int reg, int *value)
{
	uint8_t i2cbuffer[2];
	i2c_error_t error;

	i2cbuffer[0] = reg;

	if((error = i2c_send(address, 1, &i2cbuffer[0])) != i2c_error_ok)
	{
		if(error_message)
			i2c_error_format_string(error_message, error);

		return(io_error);
	}

	if((error = i2c_receive(address, 1, &i2cbuffer[1])) != i2c_error_ok)
	{
		if(error_message)
			i2c_error_format_string(error_message, error);

		return(io_error);
	}

	*value = i2cbuffer[1];

	return(io_ok);
}

irom static io_error_t clearsetregister(string_t *error_message, int address, int reg, int clearmask, int setmask)
{
	uint8_t i2cbuffer[2];
	i2c_error_t error;

	i2cbuffer[0] = reg;

	if((error = i2c_send(address, 1, &i2cbuffer[0])) != i2c_error_ok)
	{
		if(error_message)
			i2c_error_format_string(error_message, error);

		return(io_error);
	}

	if((error = i2c_receive(address, 1, &i2cbuffer[1])) != i2c_error_ok)
	{
		if(error_message)
			i2c_error_format_string(error_message, error);

		return(io_error);
	}

	i2cbuffer[1] &= ~clearmask;
	i2cbuffer[1] |= setmask;

	if(i2c_send(address, 2, i2cbuffer) != i2c_error_ok)
	{
		if(error_message)
			string_cat(error_message, "can't write to device\n");

		return(io_error);
	}

	return(io_ok);
}

irom io_error_t io_mcp_init(const struct io_info_entry_T *info)
{
	int ix;
	int instance, pin;
	uint8_t i2cbuffer[0x16];
	mcp_data_pin_t *mcp_pin_data;

	if(i2c_send_1(info->address, 0x00) != i2c_error_ok)
		return(io_error);

	if(i2c_receive(info->address, sizeof(i2cbuffer), i2cbuffer) != i2c_error_ok)
		return(io_error);

	for(ix = 0x06; ix < 0x0b; ix++)
		if(i2cbuffer[ix] != 0x00)
			return(io_error);

	for(ix = 0x0e; ix < 0x0f; ix++)
		if(i2cbuffer[ix] != 0x00)
			return(io_error);

	for(instance = 0; instance < io_mcp_instance_size; instance++)
	{
		for(pin = 0; pin < 16; pin++)
		{
			mcp_pin_data = &mcp_data_pin_table[instance][pin];

			mcp_pin_data->counter = 0;
			mcp_pin_data->debounce = 0;
		}
	}

	return(io_ok);
}

irom void io_mcp_periodic(int io, const struct io_info_entry_T *info, io_data_entry_t *data, io_flags_t *flags)
{
	int pin;
	int intf[2], intcap[2];
	int bank, bankpin;
	mcp_data_pin_t *mcp_pin_data;
	io_config_pin_entry_t *pin_config;

	readregister((string_t *)0, info->address, /* INTF */ 0x0e + 0, &intf[0]);
	readregister((string_t *)0, info->address, /* INTF */ 0x0e + 1, &intf[1]);

	readregister((string_t *)0, info->address, /* INTCAP */ 0x10 + 0, &intcap[0]);
	readregister((string_t *)0, info->address, /* INTCAP */ 0x10 + 1, &intcap[1]);

	for(pin = 0; pin < 16; pin++)
	{
		bank = (pin & 0x08) >> 3;
		bankpin = pin & 0x07;

		mcp_pin_data = &mcp_data_pin_table[info->instance][pin];
		pin_config = &config.io_config[io][pin];

		if(pin_config->llmode == io_pin_ll_counter)
		{
			if(mcp_pin_data->debounce != 0)
			{
				if(mcp_pin_data->debounce >= 10)
					mcp_pin_data->debounce -= 10; // 10 ms per tick
				else
					mcp_pin_data->debounce = 0;
			}
			else
			{
				if((intf[bank] & (1 << bankpin)) && !(intcap[bank] & (1 << bankpin))) // only count downward edge, counter is mostly pull-up
				{
					mcp_pin_data->counter++;
					mcp_pin_data->debounce = pin_config->delay;
					flags->counter_triggered = 1;
				}
			}
		}
	}
}

irom io_error_t io_mcp_init_pin_mode(string_t *error_message, const struct io_info_entry_T *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin)
{
	int bank, bankpin;

	bank = (pin & 0x08) >> 3;
	bankpin = pin & 0x07;

	if(clearsetregister(error_message, info->address, 0x02 + bank, 1 << bankpin, 0) != io_ok) // polarity inversion = 0
		return(io_error);

	if(clearsetregister(error_message, info->address, 0x04 + bank, 1 << bankpin, 0) != io_ok) // pc int enable = 0
		return(io_error);

	if(clearsetregister(error_message, info->address, 0x06 + bank, 1 << bankpin, 0) != io_ok) // compare value = 0
		return(io_error);

	if(clearsetregister(error_message, info->address, 0x08 + bank, 1 << bankpin, 0) != io_ok) // compare source = 0
		return(io_error);

	if(clearsetregister(error_message, info->address, 0x0c + bank, 1 << bankpin, 0) != io_ok) // pullup = 0
		return(io_error);

	if(clearsetregister(error_message, info->address, 0x12 + bank, 1 << bankpin, 0) != io_ok) // gpio = 0
		return(io_error);

	if(clearsetregister(error_message, info->address, 0x14 + bank, 1 << bankpin, 0) != io_ok) // latch = 0
		return(io_error);

	switch(pin_config->llmode)
	{
		case(io_pin_ll_disabled):
		{
			break;
		}

		case(io_pin_ll_input_digital):
		case(io_pin_ll_counter):
		{
			if(clearsetregister(error_message, info->address, 0x00 + bank, 0, 1 << bankpin) != io_ok) // direction = 1
				return(io_error);

			if(pin_config->flags.pullup && (clearsetregister(error_message, info->address, 0x0c + bank, 0, 1 << bankpin) != io_ok))
				return(io_error);

			if((pin_config->llmode == io_pin_ll_counter) && (clearsetregister(error_message, info->address, 0x04 + bank, 0, 1 << bankpin) != io_ok)) // pc int enable = 1
				return(io_error);

			break;
		}

		case(io_pin_ll_output_digital):
		{
			if(clearsetregister(error_message, info->address, 0x00 + bank, 1 << bankpin, 0) != io_ok) // direction = 0
				return(io_error);

			break;
		}

		default:
		{
			if(error_message)
				string_cat(error_message, "invalid mode for this pin\n");

			return(io_error);
		}
	}

	return(io_ok);
}

irom io_error_t io_mcp_get_pin_info(string_t *dst, const struct io_info_entry_T *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin)
{
	int bank, bankpin, tv, value;
	mcp_data_pin_t *mcp_pin_data;

	bank = (pin & 0x08) >> 3;
	bankpin = pin & 0x07;

	mcp_pin_data = &mcp_data_pin_table[info->instance][pin];

	switch(pin_config->llmode)
	{
		case(io_pin_ll_counter):
		{
			if(readregister(dst, info->address, 0x12 + bank, &tv) != io_ok)
				return(io_error);

			value = !!(tv & (1 << bankpin));

			string_format(dst, "current state: %s, debounce: %d", onoff(value), mcp_pin_data->debounce);

			break;
		}

		default:
		{
		}
	}

	return(io_ok);
}

irom io_error_t io_mcp_read_pin(string_t *error_message, const struct io_info_entry_T *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin, int *value)
{
	int bank, bankpin, tv;
	mcp_data_pin_t *mcp_pin_data;

	bank = (pin & 0x08) >> 3;
	bankpin = pin & 0x07;

	mcp_pin_data = &mcp_data_pin_table[info->instance][pin];

	switch(pin_config->llmode)
	{
		case(io_pin_ll_input_digital):
		case(io_pin_ll_output_digital):
		{
			if(readregister(error_message, info->address, 0x12 + bank, &tv) != io_ok)
				return(io_error);

			*value = !!(tv & (1 << bankpin));

			break;
		}

		case(io_pin_ll_counter):
		{
			*value = mcp_pin_data->counter;

			break;
		}

		default:
		{
			if(error_message)
				string_cat(error_message, "invalid mode for this pin\n");

			return(io_error);
		}
	}

	return(io_ok);
}

irom io_error_t io_mcp_write_pin(string_t *error_message, const struct io_info_entry_T *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin, int value)
{
	int bank, bankpin, clearmask, setmask;
	mcp_data_pin_t *mcp_pin_data;

	bank = (pin & 0x08) >> 3;
	bankpin = pin & 0x07;

	mcp_pin_data = &mcp_data_pin_table[info->instance][pin];

	if(value)
	{
		clearmask = 0x00;
		setmask = 1 << bankpin;
	}
	else
	{
		clearmask = 1 << bankpin;
		setmask = 0x00;
	}

	switch(pin_config->llmode)
	{
		case(io_pin_ll_output_digital):
		{
			if(clearsetregister(error_message, info->address, 0x12 + bank, clearmask, setmask) != io_ok)
				return(io_error);

			break;
		}

		case(io_pin_ll_counter):
		{
			mcp_pin_data->counter = value;

			break;
		}

		default:
		{
			if(error_message)
				string_cat(error_message, "invalid mode for this pin\n");

			return(io_error);
		}
	}

	return(io_ok);
}
