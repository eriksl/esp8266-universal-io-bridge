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

		default:
			break;
	}

	return(value);
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
			string_format(dst, "builtin rtc gpio, state: %s", onoff(read_peri_reg(RTC_GPIO_IN_DATA) & 0x01));
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
		caps_output_digital |
		caps_input_analog,
	"Auxilliary GPIO (RTC+ADC)",
	init,
	(void *)0, // postinit
	pin_max_value,
	(void *)0, // periodic slow
	(void *)0, // periodic fast
	(void *)0, // pin change handler
	init_pin_mode,
	get_pin_info,
	read_pin,
	write_pin,
	(void *)0, // set_mask
};
