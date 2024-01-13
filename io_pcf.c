#include "io_pcf.h"
#include "i2c.h"
#include "util.h"

#include <stdlib.h>

typedef struct attr_packed
{
	uint16_t	counters;
	uint8_t		pin_output_cache;
} pcf_data_instance_t;

assert_size(pcf_data_instance_t, 3);

typedef struct attr_packed
{
	unsigned int		polling:1;
	pcf_data_instance_t	instance[io_pcf_instance_size];
} pcf_data_t;

assert_size(pcf_data_t, (3 * io_pcf_instance_size) + 1);

static pcf_data_t pcf_data;

assert_size(pcf_data, (3 * io_pcf_instance_size) + 1);

static io_error_t init(const struct io_info_entry_T *info)
{
	pcf_data.instance[info->instance].counters = 0;
	pcf_data.instance[info->instance].pin_output_cache = 0xff;

	if(i2c_send1(info->address, pcf_data.instance[info->instance].pin_output_cache) != i2c_error_ok)
		return(io_error);

	return(io_ok);
}

static void periodic_slow(int io, const io_info_entry_t *info, io_data_entry_t *data, unsigned int rate_ms)
{
	static uint8_t previous_data[io_pcf_instance_size] = { 0xff, 0xff };
	uint8_t current_data[io_pcf_instance_size];
	uint8_t *previous, *current;
	uint8_t i2c_data[1];

	if(!pcf_data.instance[info->instance].counters)
		return;

	previous = &previous_data[info->instance];
	current	= &current_data[info->instance];

	if(i2c_receive(info->address, sizeof(i2c_data), i2c_data) != i2c_error_ok)
		return;

	*current = i2c_data[0] & pcf_data.instance[info->instance].counters;

	if(*current != *previous)
		dispatch_post_task(task_prio_low, task_pins_changed_pcf, *current ^ *previous, *current, info->id);

	*previous = *current;
}

static void pin_change_int_handler(int io, const io_info_entry_t *info, io_data_entry_t *data)
{
	periodic_slow(io, info, data, 0);
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

static attr_pure unsigned int max_value(const struct io_info_entry_T *info, io_data_pin_entry_t *data, const io_config_pin_entry_t *pin_config, unsigned int pin)
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

static io_error_t pin_mode(string_t *error_message, const struct io_info_entry_T *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin)
{
	i2c_error_t error;

	pcf_data.instance[info->instance].pin_output_cache &= ~(1 << pin);
	pcf_data.instance[info->instance].counters &= ~(1 << pin);

	switch(pin_config->llmode)
	{
		case(io_pin_ll_counter):
		{
			pcf_data.instance[info->instance].counters |= (1 << pin);
			[[fallthrough]];
		}

		case(io_pin_ll_disabled):
		case(io_pin_ll_input_digital):
		{
			pcf_data.instance[info->instance].pin_output_cache |= (1 << pin);
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

	if((error = i2c_send1(info->address, pcf_data.instance[info->instance].pin_output_cache)) != i2c_error_ok)
	{
		if(error_message)
			i2c_error_format_string(error_message, error);
		return(io_error);
	}

	return(io_ok);
}

static io_error_t read_pin(string_t *error_message, const struct io_info_entry_T *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin, unsigned int *value)
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

	if(pin_config->static_flags & io_flag_static_invert)
		*value = !*value;

	return(io_ok);
}

static io_error_t write_pin(string_t *error_message, const struct io_info_entry_T *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin, unsigned int value)
{
	i2c_error_t error;

	if(pin_config->static_flags & io_flag_static_invert)
		value = !value;

	switch(pin_config->llmode)
	{
		case(io_pin_ll_output_digital):
		{
			if(value)
				pcf_data.instance[info->instance].pin_output_cache |= (1 << pin);
			else
				pcf_data.instance[info->instance].pin_output_cache &= ~(1 << pin);
			
			if((error = i2c_send1(info->address, pcf_data.instance[info->instance].pin_output_cache)) != i2c_error_ok)
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

static io_error_t set_mask(string_t *error_message, const struct io_info_entry_T *info, unsigned int mask, unsigned int pins)
{
	i2c_error_t error;

	pcf_data.instance[info->instance].pin_output_cache = pins & 0x000000ff;

	if((error = i2c_send1(info->address, pcf_data.instance[info->instance].pin_output_cache)) != i2c_error_ok)
	{
		i2c_error_format_string(error_message, error);
		return(io_error);
	}

	return(io_ok);
}

roflash const io_info_entry_t io_info_entry_pcf_3a =
{
	io_id_pcf_3a, /* = 5 */
	0x3a,
	io_pcf_instance_3a,
	8,
	caps_input_digital |
		caps_counter |
		caps_output_digital |
		caps_rotary_encoder,
	"PCF8574A I2C I/O expander",
	init,
	(void *)0, // postinit
	max_value,
	periodic_slow,
	(void *)0, // periodic_fast
	pin_change_int_handler,
	pin_mode,
	(void *)0, // get pin info
	read_pin,
	write_pin,
	set_mask,
};

roflash const io_info_entry_t io_info_entry_pcf_26 =
{
	io_id_pcf_26, /* = 7 */
	0x26,
	io_pcf_instance_26,
	8,
	caps_input_digital |
		caps_counter |
		caps_output_digital |
		caps_rotary_encoder,
	"PCF8574 I2C I/O expander",
	init,
	(void *)0, // postinit
	max_value,
	periodic_slow,
	(void *)0, // periodic_fast
	pin_change_int_handler,
	pin_mode,
	(void *)0, // get pin info
	read_pin,
	write_pin,
	set_mask,
};
