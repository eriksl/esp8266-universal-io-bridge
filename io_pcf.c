#include "io_pcf.h"
#include "i2c.h"
#include "util.h"

#include <stdlib.h>

static uint8_t pcf_data_pin_table[io_pcf_instance_size];

io_error_t io_pcf_init(const struct io_info_entry_T *info)
{
	uint8_t i2cbuffer[1];

	pcf_data_pin_table[info->instance] = 0x00;

	if(i2c_receive(info->address, 1, i2cbuffer) != i2c_error_ok)
		return(io_error);

	return(io_ok);
}

attr_pure unsigned int io_pcf_pin_max_value(const struct io_info_entry_T *info, io_data_pin_entry_t *data, const io_config_pin_entry_t *pin_config, unsigned int pin)
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

		default:
			break;
	}

	return(value);
}

io_error_t io_pcf_init_pin_mode(string_t *error_message, const struct io_info_entry_T *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin)
{
	i2c_error_t error;

	switch(pin_config->llmode)
	{
		case(io_pin_ll_disabled):
		case(io_pin_ll_input_digital):
		{
			if((error = i2c_send1(info->address, 0xff)) != i2c_error_ok)
			{
				if(error_message)
					i2c_error_format_string(error_message, error);
				return(io_error);
			}

			break;
		}

		case(io_pin_ll_output_digital):
		{
			if((error = i2c_send1(info->address, 0x00)) != i2c_error_ok)
			{
				if(error_message)
					i2c_error_format_string(error_message, error);
				return(io_error);
			}

			break;
		}

		default:
		{
			if(error_message)
				string_append(error_message, "invalid mode for this pin\n");

			return(io_error);
		}
	}

	pcf_data_pin_table[info->instance] &= ~(1 << pin);

	return(io_ok);
}

io_error_t io_pcf_read_pin(string_t *error_message, const struct io_info_entry_T *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin, unsigned int *value)
{
	uint8_t i2c_data[1];
	i2c_error_t error;

	switch(pin_config->llmode)
	{
		case(io_pin_ll_input_digital):
		case(io_pin_ll_output_digital):
		{
			if((error = i2c_receive(info->address, 1, i2c_data)) != i2c_error_ok)
			{
				i2c_error_format_string(error_message, error);
				return(io_error);
			}

			break;
		}

		default:
		{
			if(error_message)
				string_append(error_message, "invalid mode for this pin\n");

			return(io_error);
		}
	}

	*value = !!(i2c_data[0] & (1 << pin));

	if(pin_config->flags & io_flag_invert)
		*value = !*value;

	return(io_ok);
}

io_error_t io_pcf_write_pin(string_t *error_message, const struct io_info_entry_T *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin, unsigned int value)
{
	i2c_error_t error;

	if(pin_config->flags & io_flag_invert)
		value = !value;

	switch(pin_config->llmode)
	{
		case(io_pin_ll_output_digital):
		{
			if(value)
				pcf_data_pin_table[info->instance] |= (1 << pin);
			else
				pcf_data_pin_table[info->instance] &= ~(1 << pin);
			
			if((error = i2c_send1(info->address, pcf_data_pin_table[info->instance])) != i2c_error_ok)
			{
				i2c_error_format_string(error_message, error);
				return(io_error);
			}

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
