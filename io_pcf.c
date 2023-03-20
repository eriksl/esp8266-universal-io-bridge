#include "io_pcf.h"
#include "i2c.h"
#include "util.h"

#include <stdlib.h>

static uint32_t counters[io_pcf_instance_size];
static uint8_t output_pin_cache[io_pcf_instance_size];

io_error_t io_pcf_init(const struct io_info_entry_T *info)
{
	counters[info->instance] = 0;
	output_pin_cache[info->instance] = 0xff;

	if(i2c_send1(info->address, output_pin_cache[info->instance]) != i2c_error_ok)
		return(io_error);

	return(io_ok);
}

void io_pcf_periodic_fast(int io, const struct io_info_entry_T *info, io_data_entry_t *data, unsigned int rate_ms)
{
	static uint8_t previous_data[io_pcf_instance_size] = { 0xff, 0xff };
	uint8_t current_data[io_pcf_instance_size];
	uint8_t *previous, *current;
	uint8_t i2c_data[1];

	if(!counters[info->instance])
		return;

	previous = &previous_data[info->instance];
	current	= &current_data[info->instance];

	if(i2c_receive(info->address, sizeof(i2c_data), i2c_data) != i2c_error_ok)
		return;

	*current = i2c_data[0] & counters[info->instance];

	if(*current != *previous)
		dispatch_post_task(task_prio_low, task_pins_changed_pcf, *current ^ *previous, *current, info->id);

	*previous = *current;
}

void io_pcf_pins_changed(uint32_t pin_status_mask, uint16_t pin_value_mask, uint8_t io)
{
	io_config_pin_entry_t *pin_config;
	unsigned int pin;

	if(io >= io_id_size)
	{
		log("[pcf] pin change: invalid io\n");
		return;
	}

	for(pin = 0; pin < io_pcf_pin_size; pin++)
	{
		if(!(pin_status_mask & (1 << pin)))
			continue;

		pin_config = &io_config[io][pin];

		if(pin_config->llmode != io_pin_ll_counter)
			continue;

		io_pin_changed(io, pin, pin_value_mask);
	}
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

io_error_t io_pcf_init_pin_mode(string_t *error_message, const struct io_info_entry_T *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin)
{
	i2c_error_t error;

	output_pin_cache[info->instance] &= ~(1 << pin);
	counters[info->instance] &= ~(1 << pin);

	switch(pin_config->llmode)
	{
		case(io_pin_ll_counter):
		{
			counters[info->instance] |= (1 << pin);
			[[fallthrough]];
		}

		case(io_pin_ll_disabled):
		case(io_pin_ll_input_digital):
		{
			output_pin_cache[info->instance] |= (1 << pin);
			break;
		}

		case(io_pin_ll_output_digital):
		{
			break;
		}

		default:
		{
			if(error_message)
				string_append(error_message, "invalid mode for this pin\n");

			return(io_error);
		}
	}

	if((error = i2c_send1(info->address, output_pin_cache[info->instance])) != i2c_error_ok)
	{
		if(error_message)
			i2c_error_format_string(error_message, error);
		return(io_error);
	}

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
			if((error = i2c_receive(info->address, sizeof(i2c_data), i2c_data)) != i2c_error_ok)
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
				output_pin_cache[info->instance] |= (1 << pin);
			else
				output_pin_cache[info->instance] &= ~(1 << pin);
			
			if((error = i2c_send1(info->address, output_pin_cache[info->instance])) != i2c_error_ok)
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

io_error_t io_pcf_set_mask(string_t *error_message, const struct io_info_entry_T *info, unsigned int mask, unsigned int pins)
{
	i2c_error_t error;

	output_pin_cache[info->instance] = pins & 0x000000ff;

	if((error = i2c_send1(info->address, output_pin_cache[info->instance])) != i2c_error_ok)
	{
		i2c_error_format_string(error_message, error);
		return(io_error);
	}

	return(io_ok);
}
