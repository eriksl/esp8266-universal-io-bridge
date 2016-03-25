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

typedef enum
{
	io_uart_rx,
	io_uart_tx,
	io_uart_none,
} io_uart_t;

typedef const struct
{
	const	bool		valid;
	const	uint32_t	mux;
	const	uint32_t	func;
	const	io_uart_t	uart_pin;
	const	uint32_t	uart_func;
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
	{ true, 	PERIPHS_IO_MUX_GPIO0_U, FUNC_GPIO0, io_uart_none, -1		},
	{ true,		PERIPHS_IO_MUX_U0TXD_U, FUNC_GPIO1, io_uart_tx, FUNC_U0TXD,	},
	{ true,		PERIPHS_IO_MUX_GPIO2_U, FUNC_GPIO2, io_uart_none, -1,		},
	{ true,		PERIPHS_IO_MUX_U0RXD_U, FUNC_GPIO3, io_uart_rx, FUNC_U0RXD	},
	{ true,		PERIPHS_IO_MUX_GPIO4_U, FUNC_GPIO4, io_uart_none, -1		},
	{ true,		PERIPHS_IO_MUX_GPIO5_U, FUNC_GPIO5, io_uart_none, -1		},
	{ false,	0, 0, io_uart_none, -1 },
	{ false,	0, 0, io_uart_none, -1 },
	{ false,	0, 0, io_uart_none, -1 },
	{ false,	0, 0, io_uart_none, -1 },
	{ false,	0, 0, io_uart_none, -1 },
	{ false,	0, 0, io_uart_none, -1 },
	{ true,		PERIPHS_IO_MUX_MTDI_U, FUNC_GPIO12, io_uart_none, -1 },
	{ true,		PERIPHS_IO_MUX_MTCK_U, FUNC_GPIO13, io_uart_none, -1 },
	{ true,		PERIPHS_IO_MUX_MTMS_U, FUNC_GPIO14, io_uart_none, -1 },
	{ true,		PERIPHS_IO_MUX_MTDO_U, FUNC_GPIO15, io_uart_none, -1 },
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
	int pinvalues;

	pinvalues = gpio_input_get();

	for(pin = 0; pin < io_gpio_pin_size; pin++)
	{
		pin_config = &config.io_config[io_id_gpio][pin];

		if((pc & (1 << pin)) && (pin_config->llmode == io_pin_ll_counter))
		{
			gpio_pin_data = &gpio_data[pin];

			if(pinvalues & (1 << pin)) // only count downward edge, counter is commonly pull-up
				gpio_pin_data->counter.debounce = 1; // workaround to ingore pcint but have it re-armed
			else
			{
				if(gpio_pin_data->counter.debounce == 0)
				{
					gpio_pin_data->counter.counter++;
					gpio_pin_data->counter.debounce = pin_config->delay;
					gpio_flags.counter_triggered = 1;
				}
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

		switch(pin_config->llmode)
		{
			case(io_pin_ll_output_analog):
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

			default:
			{
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

irom void io_gpio_periodic(int io, const struct io_info_entry_T *info, io_data_entry_t *data, io_flags_t *flags)
{
	io_config_pin_entry_t *pin_config;
	gpio_data_pin_t *gpio_pin_data;

	int pin;

	for(pin = 0; pin < io_gpio_pin_size; pin++)
	{
		pin_config = &config.io_config[io][pin];
		gpio_pin_data = &gpio_data[pin];

		if((pin_config->llmode == io_pin_ll_counter) && (gpio_pin_data->counter.debounce != 0))
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

	if(pin_config->llmode == io_pin_ll_disabled)
	{
		if(error_message)
			string_format(error_message, "pin %d disabled\n", pin);
		return(io_error);
	}

	pin_func_select(gpio_info->mux, gpio_info->func);

	gpio_pin_data = &gpio_data[pin];

	gpio_pin_data->counter.counter = 0;
	gpio_pin_data->counter.debounce = 0;

	switch(pin_config->llmode)
	{
		case(io_pin_ll_input_digital):
		case(io_pin_ll_counter):
		{
			gpio_output_set(0, 0, 0, 1 << pin);

			if(pin_config->flags.pullup)
				PIN_PULLUP_EN(gpio_info->mux);
			else
				PIN_PULLUP_DIS(gpio_info->mux);

			if(pin_config->llmode == io_pin_ll_counter)
				pin_arm_counter(pin);

			break;
		}

		case(io_pin_ll_output_digital):
		{
			gpio_output_set(0, 0, 1 << pin, 0);
			break;
		}

		case(io_pin_ll_output_analog):
		{
			int value = pin_config->shared.output_analog.lower_bound; 
			int channel = gpio_pin_data->pwm.channel;

			gpio_output_set(0, 0, 1 << pin, 0);

			pwm_duty[channel] = value;
			pwm_set_duty(value, channel);
			pwm_start();

			break;
		}

		case(io_pin_ll_i2c):
		{
			uint32_t pinaddr = GPIO_PIN_ADDR(GPIO_ID_PIN(pin));

			gpio_output_set(0, 0, 0, 1 << pin);		// set to input
			PIN_PULLUP_DIS(gpio_info->mux);			// disable pullup
			gpio_output_set(0, 0, 1 << pin, 0);		// set to output
			GPIO_REG_WRITE(pinaddr, GPIO_REG_READ(pinaddr) | GPIO_PIN_PAD_DRIVER_SET(GPIO_PAD_DRIVER_ENABLE)); // set to open drain
			gpio_output_set(1 << pin, 0, 0, 0);		// set idle = high

			break;
		}

		case(io_pin_ll_uart):
		{
			if(gpio_info->uart_pin == io_uart_none)
			{
				if(error_message)
					string_format(error_message, "gpio pin %d cannot be used for uart\n", pin);
				return(io_error);
			}

			pin_func_select(gpio_info->mux, gpio_info->uart_func);

			if(pin_config->flags.pullup)
				PIN_PULLUP_EN(gpio_info->mux);
			else
				PIN_PULLUP_DIS(gpio_info->mux);

			break;
		}

		default:
		{
			if(error_message)
			{
				string_format(error_message, "gpio %d invalid mode: ", pin);
				io_string_from_ll_mode(error_message, pin_config->llmode);
			}
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
		switch(pin_config->llmode)
		{
			case(io_pin_ll_counter):
			{
				string_format(dst, "current state: %s, debounce delay: %d",
						onoff(gpio_input_get() & 1 << pin),
						gpio_pin_data->counter.debounce);

				break;
			}

			case(io_pin_ll_output_analog):
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

			case(io_pin_ll_uart):
			{
				string_format(dst, "uart pin: %s", (gpio_info_table[pin].uart_pin == io_uart_rx) ? "rx" : "tx");

				break;
			}

			default:
			{
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
			string_format(error_message, "gpio %d invalid\n", pin);
		return(io_error);
	}

	gpio_pin_data = &gpio_data[pin];

	switch(pin_config->llmode)
	{
		case(io_pin_ll_input_digital):
		case(io_pin_ll_output_digital):
		case(io_pin_ll_i2c):
		{
			*value = !!(gpio_input_get() & 1 << pin);

			break;
		}

		case(io_pin_ll_counter):
		{
			*value = gpio_pin_data->counter.counter;

			break;
		}

		case(io_pin_ll_output_analog):
		{
			if(gpio_flags.pwm_subsystem_active && ((channel = gpio_pin_data->pwm.channel) >= 0))
				*value = pwm_get_duty(channel);
			else
				*value = 0;

			break;
		}

		default:
		{
			if(error_message)
				string_format(error_message, "cannot read from gpio %d\n", pin);
			return(io_error);
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
			string_format(error_message, "gpio %d invalid\n", pin);
		return(io_error);
	}

	gpio_pin_data = &gpio_data[pin];

	switch(pin_config->llmode)
	{
		case(io_pin_ll_counter):
		{
			gpio_pin_data->counter.counter = value;
			break;
		}

		case(io_pin_ll_output_digital):
		case(io_pin_ll_i2c):
		{
			if(value)
				gpio_output_set(1 << pin, 0, 0, 0);
			else
				gpio_output_set(0, 1 << pin, 0, 0);

			break;
		}

		case(io_pin_ll_output_analog):
		{
			int channel = gpio_pin_data->pwm.channel;

			pwm_duty[channel] = value;
			pwm_set_duty(value, channel);
			pwm_start();

			break;

		}

		default:
		{
			if(error_message)
				string_format(error_message, "cannot write to gpio %d\n", pin);
			return(io_error);
		}
	}

	return(io_ok);
}
