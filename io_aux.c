#include "io_aux.h"
#include "io_config.h"

#include "util.h"

#include <user_interface.h>

#include <stdlib.h>

enum
{
	io_aux_adc_samples = 256,
};

static unsigned int adc_index;
static unsigned int adc_total;
static unsigned int adc_last;

irom static void setclear_perireg(uint32_t reg, uint32_t clear, uint32_t set)
{
	uint32_t tmp;

	tmp = READ_PERI_REG(reg);
	tmp &= (uint32_t)~clear;
	tmp |= set;
    WRITE_PERI_REG(reg, tmp);
}

irom io_error_t io_aux_init(const struct io_info_entry_T *info)
{
	adc_index = 0;
	adc_total = 0;
	adc_last = 0;

	return(io_ok);
}

irom void io_aux_periodic(const struct io_info_entry_T *info, io_data_entry_t *data, io_flags_t *flags)
{
	unsigned int current;

	current = system_adc_read();

	if(current == 1023)
		current = 1024;

	adc_total += current;

	if(++adc_index >= io_aux_adc_samples)
	{
		adc_last = adc_total / (io_aux_adc_samples / 64);

		if(adc_last > 65535)
			adc_last = 65535;

		if(adc_last < 256)
			adc_last = 0;

		adc_index = 0;
		adc_total = 0;
	}
}

irom io_error_t io_aux_init_pin_mode(string_t *error_message, const struct io_info_entry_T *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin)
{
	switch(pin)
	{
		case(io_aux_pin_rtc):
		{
			switch(pin_config->mode)
			{
				case(io_pin_counter):
				case(io_pin_timer):
				case(io_pin_input_analog):
				case(io_pin_output_analog):
				case(io_pin_i2c):
				case(io_pin_error):
				{
					if(error_message)
						string_cat(error_message, "invalid mode for this pin\n");

					return(io_error);
				}

				case(io_pin_disabled):
				{
					break;
				}

				case(io_pin_input_digital):
				{
					setclear_perireg(PAD_XPD_DCDC_CONF, 0x43, 0x01);
					setclear_perireg(RTC_GPIO_CONF, 0x01, 0x00);
					setclear_perireg(RTC_GPIO_ENABLE, 0x01, 0x00);

					break;
				}

				case(io_pin_output_digital):
				{
					setclear_perireg(PAD_XPD_DCDC_CONF, 0x43, 0x01);
					setclear_perireg(RTC_GPIO_CONF, 0x01, 0x00);
					setclear_perireg(RTC_GPIO_ENABLE, 0x01, 0x01);

					break;
				}
			}

			break;
		}

		case(io_aux_pin_adc):
		{
			switch(pin_config->mode)
			{
				case(io_pin_disabled):
				case(io_pin_input_digital):
				case(io_pin_counter):
				case(io_pin_timer):
				case(io_pin_output_digital):
				case(io_pin_output_analog):
				case(io_pin_i2c):
				case(io_pin_error):
				{
					if(error_message)
						string_cat(error_message, "invalid mode for this pin\n");

					return(io_error);
				}

				case(io_pin_input_analog):
				{
					break;
				}
			}

			break;
		}

		default:
		{
			if(error_message)
				string_cat(error_message, "invalid mode for this io\n");

			return(io_error);
		}
	}

	return(io_ok);
}

irom io_error_t io_aux_get_pin_info(string_t *dst, const struct io_info_entry_T *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin)
{
	switch(pin)
	{
		case(io_aux_pin_rtc):
		{
			string_cat(dst, "builtin rtc gpio");

			break;
		}

		case(io_aux_pin_adc):
		{
			string_format(dst, "builtin adc input, samples: %d/%d, current total: %d",
					adc_index, io_aux_adc_samples, adc_total);

			break;
		}

		default:
		{
			string_cat(dst, "invalid mode for this io\n");

			return(io_error);
		}
	}

	return(io_ok);
}

irom io_error_t io_aux_read_pin(string_t *error_message, const struct io_info_entry_T *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin, int *value)
{
	switch(pin)
	{
		case(io_aux_pin_rtc):
		{
			switch(pin_config->mode)
			{
				case(io_pin_disabled):
				case(io_pin_counter):
				case(io_pin_timer):
				case(io_pin_input_analog):
				case(io_pin_output_analog):
				case(io_pin_i2c):
				case(io_pin_error):
				{
					if(error_message)
						string_cat(error_message, "invalid mode for this pin\n");

					return(io_error);
				}

				case(io_pin_input_digital):
				case(io_pin_output_digital):
				{
					*value = !!(READ_PERI_REG(RTC_GPIO_IN_DATA) & 0x01);

					break;
				}
			}

			break;
		}

		case(io_aux_pin_adc):
		{
			switch(pin_config->mode)
			{
				case(io_pin_disabled):
				case(io_pin_input_digital):
				case(io_pin_counter):
				case(io_pin_timer):
				case(io_pin_output_digital):
				case(io_pin_output_analog):
				case(io_pin_i2c):
				case(io_pin_error):
				{
					if(error_message)
						string_cat(error_message, "invalid mode for this pin\n");

					return(io_error);
				}

				case(io_pin_input_analog):
				{
					*value = adc_last;

					break;
				}
			}

			break;
		}

		default:
		{
			if(error_message)
				string_cat(error_message, "invalid pin for this io\n");

			return(io_error);
		}
	}

	return(io_ok);
}

irom io_error_t io_aux_write_pin(string_t *error_message, const struct io_info_entry_T *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin, int value)
{
	switch(pin)
	{
		case(io_aux_pin_rtc):
		{
			switch(pin_config->mode)
			{
				case(io_pin_disabled):
				case(io_pin_counter):
				case(io_pin_timer):
				case(io_pin_input_analog):
				case(io_pin_output_analog):
				case(io_pin_i2c):
				case(io_pin_error):
				{
					if(error_message)
						string_cat(error_message, "invalid mode for this pin\n");

					return(io_error);
				}

				case(io_pin_input_digital):
				{
					if(error_message)
						string_cat(error_message, "cannot write to input\n");

					return(io_error);
				}

				case(io_pin_output_digital):
				{
					setclear_perireg(RTC_GPIO_OUT, 0x01, value ? 0x01 : 0x00);

					break;
				}
			}

			break;
		}

		case(io_aux_pin_adc):
		{
			switch(pin_config->mode)
			{
				case(io_pin_input_digital):
				case(io_pin_counter):
				case(io_pin_timer):
				case(io_pin_output_digital):
				case(io_pin_output_analog):
				case(io_pin_i2c):
				case(io_pin_disabled):
				case(io_pin_error):
				{
					if(error_message)
						string_cat(error_message, "invalid mode for this pin\n");

					return(io_error);
				}

				case(io_pin_input_analog):
				{
					if(error_message)
						string_cat(error_message, "cannot write to adc\n");

					return(io_error);
				}
			}

			break;
		}

		default:
		{
			if(error_message)
				string_cat(error_message, "invalid pin for this io\n");

			return(io_error);
		}
	}

	return(io_ok);
}
