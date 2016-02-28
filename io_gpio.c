#include "io_gpio.h"
#include "io_config.h"

#include "util.h"

#include <user_interface.h>
#include <osapi.h>
#include <gpio.h>
#include <pwm.h>
#include <ets_sys.h>

#include <stdlib.h>

enum
{
	io_gpio_pin_size = 16,
	io_gpio_pwm_size = 8,
};

typedef const struct
{
	const	bool		valid;
	const	uint32_t	mux;
	const	uint32_t	func;
} gpio_info_t;

typedef union
{
	struct
	{
		uint32_t	counter;
		uint32_t	debounce;
	} counter;

	struct
	{
		int8_t		channel;
	} pwm;
} gpio_data_pin_t;

static struct
{
	unsigned int pwm_subsystem_active:1;
	unsigned int counter_triggered:1;
} gpio_flags;

static gpio_data_pin_t gpio_data[io_gpio_pin_size];

static gpio_info_t gpio_info_table[io_gpio_pin_size] =
{
	{ true, 	PERIPHS_IO_MUX_GPIO0_U, FUNC_GPIO0	},
	{ true,		PERIPHS_IO_MUX_U0TXD_U, FUNC_GPIO1	},
	{ true,		PERIPHS_IO_MUX_GPIO2_U, FUNC_GPIO2	},
	{ true,		PERIPHS_IO_MUX_U0RXD_U, FUNC_GPIO3	},
	{ true,		PERIPHS_IO_MUX_GPIO4_U, FUNC_GPIO4	},
	{ true,		PERIPHS_IO_MUX_GPIO5_U, FUNC_GPIO5	},
	{ false,	0, 0, },
	{ false,	0, 0, },
	{ false,	0, 0, },
	{ false,	0, 0, },
	{ false,	0, 0, },
	{ false,	0, 0, },
	{ true,		PERIPHS_IO_MUX_MTDI_U, FUNC_GPIO12 },
	{ true,		PERIPHS_IO_MUX_MTCK_U, FUNC_GPIO13 },
	{ true,		PERIPHS_IO_MUX_MTMS_U, FUNC_GPIO14 },
	{ true,		PERIPHS_IO_MUX_MTDO_U, FUNC_GPIO15 },
};

static uint32_t pwm_duty[io_gpio_pwm_size];

irom static inline void pin_arm_counter(int pin)
{
	// no use in specifying POSEDGE or NEGEDGE here (bummer),
	// they act exactly like ANYEDGE, I assume that's an SDK bug

	gpio_pin_intr_state_set(pin, GPIO_PIN_INTR_ANYEDGE);
}

iram static void pc_int_handler(uint32_t pc, void *arg)
{
	io_config_pin_entry_t *pin_config;
	gpio_data_pin_t *gpio_pin_data;
	int pin;

	for(pin = 0; pin < io_gpio_pin_size; pin++)
	{
		if(pc & (1 << pin))
		{
			pin_config = &config.io_config[io_id_gpio][pin];
			gpio_pin_data = &gpio_data[pin];

			if((pin_config->mode == io_pin_counter) && (gpio_pin_data->counter.debounce == 0))
			{
				gpio_pin_data->counter.counter++;
				gpio_pin_data->counter.debounce = pin_config->delay;
				gpio_flags.counter_triggered = 1;
			}
		}
	}

	gpio_intr_ack(pc);
}

irom io_error_t io_gpio_init(const struct io_info_entry_T *info)
{
	int pin;
	io_config_pin_entry_t *pin_config;
	gpio_info_t *gpio_pin_info;
	gpio_data_pin_t *gpio_pin_data;
	int pwmchannel = 0;
	uint32_t pwm_io_info[io_gpio_pwm_size][3];

	gpio_init();

	for(pin = 0; pin < io_gpio_pin_size; pin++)
	{
		pin_config = &config.io_config[io_id_gpio][pin];
		gpio_pin_info = &gpio_info_table[pin];
		gpio_pin_data = &gpio_data[pin];

		gpio_pin_data->counter.counter = 0;
		gpio_pin_data->counter.debounce = 0;
		gpio_pin_data->pwm.channel = -1;

		switch(pin_config->mode)
		{
			case(io_pin_disabled):
			case(io_pin_input_digital):
			case(io_pin_output_digital):
			case(io_pin_timer):
			case(io_pin_input_analog):
			case(io_pin_i2c):
			case(io_pin_error):
			case(io_pin_counter):
			{
				break;
			}

			case(io_pin_output_analog):
			{
				if(pwmchannel < io_gpio_pwm_size)
				{
					pwm_io_info[pwmchannel][0] = gpio_pin_info->mux;
					pwm_io_info[pwmchannel][1] = gpio_pin_info->func;
					pwm_io_info[pwmchannel][2] = pin;
					pwm_duty[pwmchannel] = pin_config->shared.output_analog.lower_bound;

					gpio_pin_data->pwm.channel = pwmchannel++;
				}

				break;
			}
		}
	}

	gpio_intr_handler_register(pc_int_handler, 0);

	if(pwmchannel > 0)
	{
		pwm_init(3000, pwm_duty, pwmchannel, pwm_io_info);
		gpio_flags.pwm_subsystem_active = true;
	}

	gpio_flags.counter_triggered = 0;

	return(io_ok);
}

irom void io_gpio_periodic(const struct io_info_entry_T *info, io_data_entry_t *data, io_flags_t *flags)
{
	io_config_pin_entry_t *pin_config;
	gpio_data_pin_t *gpio_pin_data;

	int pin;

	(void)info;

	for(pin = 0; pin < io_gpio_pin_size; pin++)
	{
		pin_config = &config.io_config[io_id_gpio][pin];
		gpio_pin_data = &gpio_data[pin];

		if((pin_config->mode == io_pin_counter) && (gpio_pin_data->counter.debounce != 0))
		{
			if(gpio_pin_data->counter.debounce >= 10)
				gpio_pin_data->counter.debounce -= 10; // 10 ms per tick
			else
				gpio_pin_data->counter.debounce = 0;

			if(gpio_pin_data->counter.debounce == 0)
				pin_arm_counter(pin);
		}
	}

	if(gpio_flags.counter_triggered)
	{
		gpio_flags.counter_triggered = 0;
		flags->counter_triggered = 1;
	}
}

irom io_error_t io_gpio_init_pin_mode(string_t *error_message, const struct io_info_entry_T *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin)
{
	gpio_info_t *gpio_info = &gpio_info_table[pin];
	gpio_data_pin_t *gpio_pin_data;

	if(!gpio_info->valid)
	{
		if(error_message)
			string_cat(error_message, "io invalid\n");
		return(io_error);
	}

	if(pin_config->mode == io_pin_disabled)
	{
		if(error_message)
			string_format(error_message, "pin %d disabled\n", pin);
		return(io_error);
	}

	pin_func_select(gpio_info->mux, gpio_info->func);

	if(pin_config->flags.pullup)
		PIN_PULLUP_EN(gpio_info->mux);
	else
		PIN_PULLUP_DIS(gpio_info->mux);

	gpio_pin_data = &gpio_data[pin];

	gpio_pin_data->counter.counter = 0;
	gpio_pin_data->counter.debounce = 0;

	switch(pin_config->mode)
	{
		case(io_pin_input_digital):
		{
			gpio_output_set(0, 0, 0, 1 << pin);
			break;
		}

		case(io_pin_counter):
		{
			gpio_output_set(0, 0, 0, 1 << pin);
			pin_arm_counter(pin);
			break;
		}

		case(io_pin_output_digital):
		case(io_pin_timer):
		{
			gpio_output_set(0, 0, 1 << pin, 0);
			break;
		}

		case(io_pin_output_analog):
		{
			int value = pin_config->shared.output_analog.lower_bound; 
			int channel = gpio_pin_data->pwm.channel;

			gpio_output_set(0, 0, 1 << pin, 0);

			pwm_duty[channel] = value;
			pwm_set_duty(value, channel);
			pwm_start();

			break;
		}

		case(io_pin_i2c):
		{
			uint32_t pinaddr = GPIO_PIN_ADDR(GPIO_ID_PIN(pin));

			/* set to open drain */
			GPIO_REG_WRITE(pinaddr, GPIO_REG_READ(pinaddr) | GPIO_PIN_PAD_DRIVER_SET(GPIO_PAD_DRIVER_ENABLE));
			gpio_output_set(0, 0, 1 << pin, 0);

			break;
		}

		default:
		{
			if(error_message)
				string_format(error_message, "gpio %d invalid mode: %d\n", pin, pin_config->mode);
			return(io_error);
		}
	}

	return(io_ok);
}

irom io_error_t io_gpio_get_pin_info(string_t *dst, const struct io_info_entry_T *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin)
{
	int channel;
	int duty;
	int freq;
	gpio_data_pin_t *gpio_pin_data;

	gpio_pin_data = &gpio_data[pin];

	if(!gpio_info_table[pin].valid)
		string_cat(dst, "unusable i/o pin");
	else
	{
		switch(pin_config->mode)
		{
			case(io_pin_disabled):
			case(io_pin_input_digital):
			case(io_pin_output_digital):
			case(io_pin_timer):
			case(io_pin_input_analog):
			case(io_pin_i2c):
			case(io_pin_error):
			{
				break;
			}

			case(io_pin_counter):
			{
				string_format(dst, "current state: %s, debounce delay: %d",
						onoff(gpio_input_get() & 1 << pin),
						gpio_pin_data->counter.debounce);

				break;
			}

			case(io_pin_output_analog):
			{
				if(gpio_flags.pwm_subsystem_active)
				{
					channel = gpio_pin_data->pwm.channel;
					duty = pwm_get_duty(channel);
					freq = 1000000 / pwm_get_period();

					string_format(dst, "channel: %d, frequency: %d, duty: %d", channel, freq, duty);
				}
				else
					string_cat(dst, "pwm inactive");

				break;
			}
		}
	}

	return(io_ok);
}

irom io_error_t io_gpio_read_pin(string_t *error_message, const struct io_info_entry_T *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin, int *value)
{
	gpio_data_pin_t *gpio_pin_data;
	int channel;

	if(!gpio_info_table[pin].valid)
	{
		if(error_message)
			string_format(error_message, "io %d/%d invalid\n", io_id_gpio, pin);
		return(io_error);
	}

	gpio_pin_data = &gpio_data[pin];

	switch(pin_config->mode)
	{
		case(io_pin_disabled):
		case(io_pin_input_analog):
		case(io_pin_error):
		{
			if(error_message)
				string_format(error_message, "io %d/%d invalid\n", io_id_gpio, pin);
			return(io_error);
		}

		case(io_pin_input_digital):
		case(io_pin_output_digital):
		case(io_pin_timer):
		case(io_pin_i2c):
		{
			*value = !!(gpio_input_get() & 1 << pin);

			break;
		}

		case(io_pin_counter):
		{
			*value = gpio_pin_data->counter.counter;

			break;
		}

		case(io_pin_output_analog):
		{
			if(gpio_flags.pwm_subsystem_active && ((channel = gpio_pin_data->pwm.channel) >= 0))
				*value = pwm_get_duty(channel);
			else
				*value = 0;

			break;
		}
	}

	return(io_ok);
}

irom io_error_t io_gpio_write_pin(string_t *error_message, const struct io_info_entry_T *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin, int value)
{
	gpio_data_pin_t *gpio_pin_data;

	if(!gpio_info_table[pin].valid)
	{
		if(error_message)
			string_format(error_message, "io %d/%d invalid\n", io_id_gpio, pin);
		return(io_error);
	}

	gpio_pin_data = &gpio_data[pin];

	switch(pin_config->mode)
	{
		case(io_pin_disabled):
		case(io_pin_input_digital):
		case(io_pin_input_analog):
		case(io_pin_error):
		{
			break;
		}

		case(io_pin_counter):
		{
			gpio_pin_data->counter.counter = value;
			break;
		}

		case(io_pin_output_digital):
		case(io_pin_timer):
		case(io_pin_i2c):
		{
			if(value)
				gpio_output_set(1 << pin, 0, 0, 0);
			else
				gpio_output_set(0, 1 << pin, 0, 0);

			break;
		}

		case(io_pin_output_analog):
		{
			int channel = gpio_pin_data->pwm.channel;

			pwm_duty[channel] = value;
			pwm_set_duty(value, channel);
			pwm_start();

			break;

		}
	}

	return(io_ok);
}
