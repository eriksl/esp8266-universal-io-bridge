#include "io_gpio.h"
#include "io_config.h"

#include "util.h"

#include <user_interface.h>
#include <osapi.h>
#include <pwm.h>
#include <ets_sys.h>

#include <stdlib.h>

// missing from eagle_soc.h

#define FUNC_GPIO6                          3
#define FUNC_GPIO7                          3
#define FUNC_GPIO8                          3
#define FUNC_GPIO11                         3

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

static uint32_t gpio_pc_pins_previous;
static gpio_data_pin_t gpio_data[io_gpio_pin_size];

static gpio_info_t gpio_info_table[io_gpio_pin_size] =
{
	{ true, 	PERIPHS_IO_MUX_GPIO0_U,		FUNC_GPIO0,		io_uart_none,	-1			},
	{ true,		PERIPHS_IO_MUX_U0TXD_U,		FUNC_GPIO1,		io_uart_tx,		FUNC_U0TXD,	},
	{ true,		PERIPHS_IO_MUX_GPIO2_U,		FUNC_GPIO2,		io_uart_none,	-1,			},
	{ true,		PERIPHS_IO_MUX_U0RXD_U,		FUNC_GPIO3,		io_uart_rx,		FUNC_U0RXD	},
	{ true,		PERIPHS_IO_MUX_GPIO4_U,		FUNC_GPIO4,		io_uart_none,	-1			},
	{ true,		PERIPHS_IO_MUX_GPIO5_U,		FUNC_GPIO5,		io_uart_none,	-1			},
	{ false,	PERIPHS_IO_MUX_SD_CLK_U,	FUNC_GPIO6,		io_uart_none,	-1			},
	{ false,	PERIPHS_IO_MUX_SD_DATA0_U,	FUNC_GPIO7,		io_uart_none,	-1			},
	{ false,	PERIPHS_IO_MUX_SD_DATA1_U,	FUNC_GPIO8,		io_uart_none,	-1			},
	{ false,	PERIPHS_IO_MUX_SD_DATA2_U,	FUNC_GPIO9,		io_uart_none,	-1			},
	{ false,	PERIPHS_IO_MUX_SD_DATA3_U,	FUNC_GPIO10,	io_uart_none,	-1			},
	{ false,	PERIPHS_IO_MUX_SD_CMD_U,	FUNC_GPIO11,	io_uart_none,	-1			},
	{ true,		PERIPHS_IO_MUX_MTDI_U,		FUNC_GPIO12,	io_uart_none,	-1			},
	{ true,		PERIPHS_IO_MUX_MTCK_U, 		FUNC_GPIO13,	io_uart_none,	-1			},
	{ true,		PERIPHS_IO_MUX_MTMS_U, 		FUNC_GPIO14,	io_uart_none,	-1			},
	{ true,		PERIPHS_IO_MUX_MTDO_U, 		FUNC_GPIO15,	io_uart_none,	-1			},
};

static uint32_t pwm_duty[io_gpio_pwm_size];

// set GPIO direction

irom static void gpio_direction_clear_mask(uint32_t mask)
{
	gpio_reg_write(GPIO_ENABLE_W1TC_ADDRESS, mask);
}

irom static void gpio_direction_set_mask(uint32_t mask)
{
	gpio_reg_write(GPIO_ENABLE_W1TS_ADDRESS, mask);
}

irom static void gpio_direction(int io, int onoff)
{
	if(onoff)
		gpio_direction_set_mask(1 << io);
	else
		gpio_direction_clear_mask(1 << io);
}

// disable / enable pullup

irom static bool_t gpio_pullup(int pin, int onoff)
{
	uint32_t value;
	gpio_info_t *gpio_pin_info;

	if(pin > 15)
		return(false);

	gpio_pin_info = &gpio_info_table[pin];

	if(!gpio_pin_info->valid)
		return(false);

	value = read_peri_reg(gpio_pin_info->mux);

	if(onoff)
		value |= PERIPHS_IO_MUX_PULLUP;
	else
		value &= ~(PERIPHS_IO_MUX_PULLUP);

	write_peri_reg(gpio_pin_info->mux, value);

	return(true);
}

// clear / set open drain mode

irom static void gpio_open_drain(int io, int onoff)
{
	uint32_t pinaddr;
	uint32_t value;

	pinaddr	= gpio_pin_addr(GPIO_ID_PIN(io));
	value	= gpio_reg_read(pinaddr);

	if(onoff)
		value |= GPIO_PIN_PAD_DRIVER_SET(GPIO_PAD_DRIVER_ENABLE);
	else
		value &= ~(GPIO_PIN_PAD_DRIVER_SET(GPIO_PAD_DRIVER_ENABLE));

	gpio_reg_write(pinaddr, value);
}

// select pin function

irom static bool_t gpio_func_select(int pin, int func)
{
	gpio_info_t *gpio_pin_info;
	uint32_t value;

	if(pin > 15)
		return(false);

	gpio_pin_info = &gpio_info_table[pin];

	if(!gpio_pin_info->valid)
		return(false);

	value = read_peri_reg(gpio_pin_info->mux);

	value &= ~(PERIPHS_IO_MUX_FUNC << PERIPHS_IO_MUX_FUNC_S);
	value |= (func & (1 << 2)) << (PERIPHS_IO_MUX_FUNC_S + 2);
	value |= (func & (1 << 1)) << (PERIPHS_IO_MUX_FUNC_S + 0);
	value |= (func & (1 << 0)) << (PERIPHS_IO_MUX_FUNC_S + 0);

	write_peri_reg(gpio_pin_info->mux, value);

	return(true);
}

irom io_error_t io_gpio_init(const struct io_info_entry_T *info)
{
	int pin;
	io_config_pin_entry_t *pin_config;
	gpio_info_t *gpio_pin_info;
	gpio_data_pin_t *gpio_pin_data;
	int pwmchannel = 0;
	uint32_t pwm_io_info[io_gpio_pwm_size][3];

	for(pin = 0; pin < io_gpio_pin_size; pin++)
	{
		gpio_pin_info = &gpio_info_table[pin];

		if(gpio_pin_info->valid)
		{
			pin_config = &config.io_config[io_id_gpio][pin];
			gpio_pin_data = &gpio_data[pin];

			if(pin_config->llmode == io_pin_ll_output_analog)
			{
				gpio_pin_data->pwm.channel = -1;

				if(pwmchannel < io_gpio_pwm_size)
				{
					pwm_io_info[pwmchannel][0] = gpio_pin_info->mux;
					pwm_io_info[pwmchannel][1] = gpio_pin_info->func;
					pwm_io_info[pwmchannel][2] = pin;
					pwm_duty[pwmchannel] = 0;

					gpio_pin_data->pwm.channel = pwmchannel++;
				}
			}
		}
	}

	gpio_pc_pins_previous = gpio_get_mask();

	if(pwmchannel > 0)
	{
		pwm_init(3000, pwm_duty, pwmchannel, pwm_io_info);
		gpio_flags.pwm_subsystem_active = true;
	}

	gpio_flags.counter_triggered = 0;

	return(io_ok);
}

iram void io_gpio_periodic(int io, const struct io_info_entry_T *info, io_data_entry_t *data, io_flags_t *flags)
{
	io_config_pin_entry_t *pin_config;
	gpio_data_pin_t *gpio_pin_data;
	int pin;
	uint32_t gpio_pc_pins_current;

	gpio_pc_pins_current = gpio_get_mask();

	for(pin = 0; pin < io_gpio_pin_size; pin++)
	{
		pin_config = &config.io_config[io][pin];

		if(pin_config->llmode == io_pin_ll_counter)
		{
			gpio_pin_data = &gpio_data[pin];

			if((gpio_pin_data->counter.debounce == 0))
			{
				if((gpio_pc_pins_previous & (1 << pin)) && !(gpio_pc_pins_current & (1 << pin)))
				{
					gpio_pin_data->counter.counter++;
					gpio_pin_data->counter.debounce = pin_config->speed;
					flags->counter_triggered = 1;
				}
			}
			else
			{
				if(gpio_pin_data->counter.debounce >= 10)
					gpio_pin_data->counter.debounce -= 10; // 10 ms per tick
				else
					gpio_pin_data->counter.debounce = 0;
			}
		}
	}

	gpio_pc_pins_previous = gpio_pc_pins_current;
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

	gpio_func_select(pin, gpio_info->func);

	gpio_pin_data = &gpio_data[pin];

	switch(pin_config->llmode)
	{
		case(io_pin_ll_input_digital):
		case(io_pin_ll_counter):
		{
			gpio_direction(pin, 0);
			gpio_pullup(pin, pin_config->flags.pullup);

			if(pin_config->llmode == io_pin_ll_counter)
			{
				gpio_pin_data->counter.counter = 0;
				gpio_pin_data->counter.debounce = 0;
			}

			break;
		}

		case(io_pin_ll_output_digital):
		{
			gpio_direction(pin, 1);
			break;
		}

		case(io_pin_ll_output_analog):
		{
			int channel = gpio_pin_data->pwm.channel;

			gpio_direction(pin, 1);

			if(channel >= 0)
			{
				pwm_duty[channel] = 0;
				pwm_set_duty(0, channel);
				pwm_start();
			}

			break;
		}

		case(io_pin_ll_i2c):
		{
			gpio_direction(pin, 0);
			gpio_pullup(pin, 0);
			gpio_direction(pin, 1);
			gpio_open_drain(pin, 1);
			gpio_set(pin, 1);

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

			gpio_func_select(pin, gpio_info->uart_func);
			gpio_pullup(pin, pin_config->flags.pullup);

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
						onoff(gpio_get(pin)), gpio_pin_data->counter.debounce);

				break;
			}

			case(io_pin_ll_output_analog):
			{
				if(gpio_flags.pwm_subsystem_active)
				{
					int channel = gpio_pin_data->pwm.channel;
					int duty, freq;

					if(channel >= 0)
					{
						duty = pwm_get_duty(channel);
						freq = 1000000 / pwm_get_period();
					}
					else
					{
						duty = -1;
						freq = -1;
					}

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

iram io_error_t io_gpio_read_pin(string_t *error_message, const struct io_info_entry_T *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin, int *value)
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
		case(io_pin_ll_input_digital):
		case(io_pin_ll_output_digital):
		case(io_pin_ll_i2c):
		{
			*value = gpio_get(pin);

			break;
		}

		case(io_pin_ll_counter):
		{
			*value = gpio_pin_data->counter.counter;

			break;
		}

		case(io_pin_ll_output_analog):
		{
			int channel = gpio_pin_data->pwm.channel;

			if((channel >= 0) && gpio_flags.pwm_subsystem_active)
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

iram io_error_t io_gpio_write_pin(string_t *error_message, const struct io_info_entry_T *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin, int value)
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
			gpio_set(pin, value);

			break;
		}

		case(io_pin_ll_output_analog):
		{
			int channel = gpio_pin_data->pwm.channel;

			if(channel >= 0)
			{
				pwm_duty[channel] = value;
				pwm_set_duty(value, channel);
				pwm_start();
			}

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
