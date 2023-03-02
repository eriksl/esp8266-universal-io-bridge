#include "io_aux.h"

#include "io_gpio.h"
#include "dispatch.h"
#include "util.h"

#include <stdlib.h>

typedef union
{
	struct
	{
		unsigned int counter;
		unsigned int debounce;
		unsigned int last_value;
	} counter;
} io_aux_data_pin_t;

static io_aux_data_pin_t aux_pin_data[io_aux_pin_size];

attr_const io_error_t io_aux_init(const struct io_info_entry_T *info)
{
	int pin;

	for(pin = io_aux_pin_rtc; pin < io_aux_pin_size; pin++)
	{
		aux_pin_data[pin].counter.counter = 0;
		aux_pin_data[pin].counter.debounce = 0;
		aux_pin_data[pin].counter.last_value = 0;
	}

	return(io_ok);
}

attr_pure unsigned int io_aux_pin_max_value(const struct io_info_entry_T *info, io_data_pin_entry_t *data, const io_config_pin_entry_t *pin_config, unsigned int pin)
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

		case(io_pin_ll_input_analog):
		{
			value = 0x400;
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

iram void io_aux_periodic_fast(int io, const struct io_info_entry_T *info, io_data_entry_t *data, unsigned int period)
{
	unsigned int pin;

	for(pin = io_aux_pin_rtc; pin < io_aux_pin_size; pin++)
	{
		io_config_pin_entry_t *pin_config = &io_config[io][pin];

		if(pin_config->llmode == io_pin_ll_counter)
		{
			io_aux_data_pin_t *io_aux_data_pin = &aux_pin_data[pin];

			// debouncing on input requested && debouncing period active
			if((pin_config->speed != 0) && (io_aux_data_pin->counter.debounce != 0))
			{
				if(io_aux_data_pin->counter.debounce > (1000 / period))
					io_aux_data_pin->counter.debounce -= 1000 / period;
				else
					io_aux_data_pin->counter.debounce = 0;
			}
			else
			{
				unsigned int pin_value = !!(read_peri_reg(RTC_GPIO_IN_DATA) & 0x01);

				if(pin_value != io_aux_data_pin->counter.last_value)
				{
					io_aux_data_pin->counter.last_value = pin_value;
					io_aux_data_pin->counter.debounce = pin_config->speed;

					if(!pin_value)
					{
						io_aux_data_pin->counter.counter++;
						dispatch_post_task(task_prio_medium, task_alert_pin_changed, 0);
					}
				}
			}
		}
	}
}

io_error_t io_aux_init_pin_mode(string_t *error_message, const struct io_info_entry_T *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin)
{
	switch(pin)
	{
		case(io_aux_pin_rtc):
		{
			switch(pin_config->llmode)
			{
				case(io_pin_ll_disabled):
				{
					break;
				}

				case(io_pin_ll_input_digital):
				case(io_pin_ll_counter):
				{
					clear_set_peri_reg_mask(PAD_XPD_DCDC_CONF, 0x43, 0x01);
					clear_set_peri_reg_mask(RTC_GPIO_CONF, 0x01, 0x00);
					clear_set_peri_reg_mask(RTC_GPIO_ENABLE, 0x01, 0x00);

					break;
				}

				case(io_pin_ll_output_digital):
				{
					clear_set_peri_reg_mask(PAD_XPD_DCDC_CONF, 0x43, 0x01);
					clear_set_peri_reg_mask(RTC_GPIO_CONF, 0x01, 0x00);
					clear_set_peri_reg_mask(RTC_GPIO_ENABLE, 0x01, 0x01);

					break;
				}

				default:
				{
					if(error_message)
						string_append(error_message, "invalid mode for this pin\n");

					return(io_error);
				}
			}

			break;
		}

		case(io_aux_pin_adc):
		{
			switch(pin_config->llmode)
			{
				case(io_pin_ll_input_analog):
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

			break;
		}

		default:
		{
			if(error_message)
				string_append(error_message, "invalid mode for this io\n");

			return(io_error);
		}
	}

	return(io_ok);
}

io_error_t io_aux_get_pin_info(string_t *dst, const struct io_info_entry_T *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin)
{
	switch(pin)
	{
		case(io_aux_pin_rtc):
		{
			string_append(dst, "builtin rtc gpio");
			break;
		}

		case(io_aux_pin_adc):
		{
			string_append(dst, "builtin adc input");
			break;
		}

		default:
		{
			string_append(dst, "invalid mode for this io\n");
			return(io_error);
		}
	}

	switch(pin_config->llmode)
	{
		case(io_pin_ll_counter):
		{
			io_aux_data_pin_t *io_aux_data_pin = &aux_pin_data[pin];

			string_format(dst, ", current state: %s, prev state: %s, debounce delay: %u ms",
					onoff(read_peri_reg(RTC_GPIO_IN_DATA) & 0x01),
					onoff(io_aux_data_pin->counter.last_value),
					io_aux_data_pin->counter.debounce);
			break;
		}

		default:
		{
			break;
		}
	}

	return(io_ok);
}

iram io_error_t io_aux_read_pin(string_t *error_message, const struct io_info_entry_T *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin, unsigned int *value)
{
	switch(pin)
	{
		case(io_aux_pin_rtc):
		{
			switch(pin_config->llmode)
			{
				case(io_pin_ll_input_digital):
				case(io_pin_ll_output_digital):
				{
					*value = !!(read_peri_reg(RTC_GPIO_IN_DATA) & 0x01);

					if(pin_config->flags & io_flag_invert)
						*value = !*value;

					break;
				}

				case(io_pin_ll_counter):
				{
					io_aux_data_pin_t *io_aux_data_pin = &aux_pin_data[pin];
					*value = io_aux_data_pin->counter.counter;
					break;
				}

				default:
				{
					if(error_message)
						string_append(error_message, "invalid mode for this pin\n");
					return(io_error);
				}
			}

			break;
		}

		case(io_aux_pin_adc):
		{
			switch(pin_config->llmode)
			{
				case(io_pin_ll_input_analog):
				{
					*value = system_adc_read() << 6;

					break;
				}

				default:
				{
					if(error_message)
						string_append(error_message, "invalid mode for this pin\n");

					return(io_error);
				}

			}

			break;
		}

		default:
		{
			if(error_message)
				string_append(error_message, "invalid pin for this io\n");

			return(io_error);
		}
	}

	return(io_ok);
}

iram io_error_t io_aux_write_pin(string_t *error_message, const struct io_info_entry_T *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin, unsigned int value)
{
	switch(pin)
	{
		case(io_aux_pin_rtc):
		{
			switch(pin_config->llmode)
			{
				case(io_pin_ll_input_digital):
				{
					if(error_message)
						string_append(error_message, "cannot write to input\n");
					return(io_error);
				}

				case(io_pin_ll_output_digital):
				{
					if(pin_config->flags & io_flag_invert)
						value = !value;

					clear_set_peri_reg_mask(RTC_GPIO_OUT, 0x01, value ? 0x01 : 0x00);
					break;
				}

				case(io_pin_ll_counter):
				{
					io_aux_data_pin_t *io_aux_data_pin = &aux_pin_data[pin];
					io_aux_data_pin->counter.counter = value;
					break;
				}

				default:
				{
					if(error_message)
						string_append(error_message, "invalid mode for this pin\n");

					return(io_error);
				}

			}

			break;
		}

		case(io_aux_pin_adc):
		{
			switch(pin_config->llmode)
			{
				case(io_pin_ll_input_analog):
				{
					if(error_message)
						string_append(error_message, "cannot write to adc\n");

					return(io_error);
				}

				default:
				{
					if(error_message)
						string_append(error_message, "invalid mode for this pin\n");

					return(io_error);
				}

			}

			break;
		}

		default:
		{
			if(error_message)
				string_append(error_message, "invalid pin for this io\n");

			return(io_error);
		}
	}

	return(io_ok);
}
