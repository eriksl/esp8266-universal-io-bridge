#include "io_aux.h"

#include "io_gpio.h"
#include "dispatch.h"
#include "util.h"

#include <stdlib.h>

static attr_const io_error_t init(const struct io_info_entry_T *info)
{
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

iram static void periodic_fast(int io, const struct io_info_entry_T *info, io_data_entry_t *data, unsigned int rate_ms)
{
	const io_config_pin_entry_t *pin_config = &io_config[io][io_aux_pin_gpio];
	static uint32_t last_value_mask = 0;
	uint32_t pin_value_mask;

	if(pin_config->llmode != io_pin_ll_counter)
		return;

	pin_value_mask = read_peri_reg(RTC_GPIO_IN_DATA) & 0x01;

	if(pin_value_mask != last_value_mask)
	{
		last_value_mask = pin_value_mask;
		dispatch_post_task(task_prio_medium, task_pins_changed_aux, (1 << 0), pin_value_mask & 0x0000ffff, 0);
	}
}

void io_aux_pins_changed(uint32_t pin_status_mask, uint16_t pin_value_mask)
{
	io_pin_changed(io_id_aux, io_aux_pin_gpio, pin_value_mask);
}

static io_error_t init_pin_mode(string_t *error_message, const struct io_info_entry_T *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin)
{
	switch(pin)
	{
		case(io_aux_pin_gpio):
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

static io_error_t get_pin_info(string_t *dst, const struct io_info_entry_T *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin)
{
	switch(pin)
	{
		case(io_aux_pin_gpio):
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
			string_format(dst, ", state: %s", onoff(read_peri_reg(RTC_GPIO_IN_DATA) & 0x01));
			break;
		}

		default:
		{
			break;
		}
	}

	return(io_ok);
}

iram static io_error_t read_pin(string_t *error_message, const struct io_info_entry_T *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin, unsigned int *value)
{
	switch(pin)
	{
		case(io_aux_pin_gpio):
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

iram static io_error_t write_pin(string_t *error_message, const struct io_info_entry_T *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin, unsigned int value)
{
	switch(pin)
	{
		case(io_aux_pin_gpio):
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

roflash const io_info_entry_t io_info_entry_aux =
{
	io_id_aux,/* = 1 */
	0x01,
	0,
	2,
	caps_input_digital |
		caps_counter |
		caps_output_digital |
		caps_input_analog,
	"Auxilliary GPIO (RTC+ADC)",
	init,
	(void *)0, // postinit
	pin_max_value,
	(void *)0, // periodic slow
	periodic_fast,
	init_pin_mode,
	get_pin_info,
	read_pin,
	write_pin,
	(void *)0, // set_mask
};
