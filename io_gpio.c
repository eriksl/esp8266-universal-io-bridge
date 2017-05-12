#include "io_gpio.h"

#include "stats.h"
#include "util.h"

#include <user_interface.h>
#include <osapi.h>
#include <ets_sys.h>

#include <stdlib.h>

// missing from eagle_soc.h

enum
{
	FUNC_U0RXD = 0,
	FUNC_GPIO6 = 3,
	FUNC_GPIO7 = 3,
	FUNC_GPIO8 = 3,
	FUNC_GPIO11 = 3
};

// from SDK hw_timer.c

enum
{
	FRC1_DIVIDE_BY_1 = 0,
	FRC1_DIVIDE_BY_16 = 4,
	FRC1_DIVIDE_BY_256 = 8
};

enum
{
	FRC1_EDGE_INT = 0,
	FCR1_LEVEL_INT = 1
};

enum
{
	FRC1_AUTO_LOAD = 0x0040,
	FRC1_ENABLE_TIMER = 0x0080,
};

enum
{
	FRC1_NMI_SOURCE = 0x8000
};

enum
{
	io_gpio_pin_size = 16,
	io_gpio_pwm_max_channels = 4,
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
		unsigned int counter;
		unsigned int debounce;
	} counter;

	struct
	{
		int this;
		int next;
		unsigned int duty;
	} pwm;
} gpio_data_pin_t;

static uint32_t gpio_pc_pins_previous;
static gpio_data_pin_t gpio_data[io_gpio_pin_size];
static uint32_t pwm_static_set_mask;
static uint32_t pwm_static_clear_mask;

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

	if(pin >= io_gpio_pin_size)
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

irom static void gpio_open_drain(int pin, int onoff)
{
	uint32_t pinaddr;
	uint32_t value;

	pinaddr	= gpio_pin_addr(GPIO_ID_PIN(pin));
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

	if(pin >= io_gpio_pin_size)
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

// PWM

typedef struct
{
	int			duty;
	int			delay;
	uint16_t	mask;
} pwm_phase_t;

typedef struct
{
	unsigned int	size;
	pwm_phase_t		phase[io_gpio_pwm_max_channels + 1];
} pwm_phases_t;

typedef struct
{
	unsigned int	pwm_swap_phase_set:1;
	unsigned int	pwm_cpu_high_speed:1;
} io_gpio_flags_t;

static unsigned int		pwm_current_phase_set;
static pwm_phases_t		pwm_phase[2];
static io_gpio_flags_t	io_gpio_flags;

static int pwm_head;

iram static inline bool_t pwm_isr_enabled(void)
{
	return(gpio_interrupts_enabled() & (1 << ETS_FRC_TIMER1_INUM));
}

iram static inline void pwm_isr_enable(bool_t onoff)
{
	if(onoff)
		ets_isr_unmask(1 << ETS_FRC_TIMER1_INUM);
	else
		ets_isr_mask(1 << ETS_FRC_TIMER1_INUM);
}

iram static inline void pwm_timer_reload(uint32_t value)
{
	write_peri_reg(PERIPHS_TIMER_BASEDDR + FRC1_LOAD_ADDRESS, value);
}

iram static void pwm_isr(void)
{
	static unsigned int	phase, delay;
	static pwm_phases_t *phase_data;

	stat_pwm_timer_interrupts++;

	phase_data = &pwm_phase[pwm_current_phase_set];

	for(;;)
	{
		if(phase >= phase_data->size)
			phase = 0;

		if(phase == 0)
		{
			if(io_gpio_flags.pwm_swap_phase_set)
			{
				gpio_set_mask(pwm_static_set_mask);
				gpio_clear_mask(pwm_static_clear_mask);
				pwm_static_set_mask = 0;
				pwm_static_clear_mask = 0;
				pwm_current_phase_set = pwm_current_phase_set ? 0 : 1;
				io_gpio_flags.pwm_swap_phase_set = 0;
				phase_data = &pwm_phase[pwm_current_phase_set];
			}

			if(phase_data->size < 2)
			{
				pwm_isr_enable(false);
				return;
			}

			gpio_set_mask(phase_data->phase[phase].mask);
		}
		else
			gpio_clear_mask(phase_data->phase[phase].mask);

		delay = phase_data->phase[phase].delay;

		phase++;

		if(delay < 2)
			continue;
		else
			if(delay < 24)
				if(io_gpio_flags.pwm_cpu_high_speed)
					for(delay = ((delay - 2) * 6) + 5; delay > 0; delay--)
						asm volatile("nop");
				else
					for(delay = ((delay - 2) * 3) + 2; delay > 0; delay--)
						asm volatile("nop");
			else
			{
				if(io_gpio_flags.pwm_cpu_high_speed)
					delay -= 7;
				else
					delay -= 14;

				pwm_timer_reload(delay);

				return;
			}
	}
}

irom static bool_t pwm_go(void)
{
	io_config_pin_entry_t *pin1_config;
	gpio_info_t *pin1_info;
	gpio_data_pin_t *pin1_data, *pin2_data, *pin3_data;
	int pin1, pin2, pin3;
	pwm_phases_t *phase_data;
	unsigned int duty, delta, new_set, pwm_period;

	if(!config_get_int("pwm.period", -1, -1, &pwm_period))
		pwm_period = 65536;

	if(io_gpio_flags.pwm_swap_phase_set)
		return(false);

	new_set = pwm_current_phase_set;

	if(pwm_isr_enabled())
		new_set = new_set ? 0 : 1;

	io_gpio_flags.pwm_cpu_high_speed = config_flags_get().flag.cpu_high_speed;

	pwm_head = -1;

	// reset linked list

	for(pin1 = 0; pin1 < io_gpio_pin_size; pin1++)
	{
		pin1_config	= &io_config[io_id_gpio][pin1];
		pin1_info	= &gpio_info_table[pin1];
		pin1_data	= &gpio_data[pin1];

		if((pin1_info->valid) &&
				(pin1_config->llmode == io_pin_ll_output_analog))
		{
			pin1_data->pwm.this = pin1;
			pin1_data->pwm.next = -1;

			if(pin1_data->pwm.duty >= pwm_period)
				pin1_data->pwm.duty = pwm_period - 1;
		}
	}

	// create linked list

	for(pin1 = 0; pin1 < io_gpio_pin_size; pin1++)
	{
		pin1_info	= &gpio_info_table[pin1];
		pin1_config	= &io_config[io_id_gpio][pin1];
		pin1_data	= &gpio_data[pin1];

		if(!pin1_info->valid || (pin1_config->llmode != io_pin_ll_output_analog))
			continue;

		if(pin1_data->pwm.duty == 0)
		{
			if(new_set == pwm_current_phase_set)
				gpio_set(pin1, 0);
			else
				pwm_static_clear_mask |= 1 << pin1;
		}
		else
			if((pin1_data->pwm.duty + 1) >= pwm_period)
			{
				if(new_set == pwm_current_phase_set)
					gpio_set(pin1, 1);
				else
					pwm_static_set_mask |= 1 << pin1;
			}
			else
				if(pwm_head < 0)
				{
					pwm_head = pin1;
					pin1_data->pwm.next = -1;
				}
				else
				{
					pin2 = pwm_head;
					pin2_data = &gpio_data[pin2];

					if(pin2_data->pwm.duty > pin1_data->pwm.duty)
					{
						pwm_head = pin1;
						pin1_data->pwm.next = pin2;
					}
					else
					{
						for(pin2 = pwm_head; pin2 >= 0; pin2 = pin3)
						{
							pin2_data	= &gpio_data[pin2];
							pin3		= pin2_data->pwm.next;

							if(pin3 < 0)
							{
								pin1_data->pwm.next = -1;
								pin2_data->pwm.next = pin1;

								break;
							}
							else
							{
								pin3_data = &gpio_data[pin3];

								if(pin3_data->pwm.duty > pin1_data->pwm.duty)
								{
									pin1_data->pwm.next = pin3;
									pin2_data->pwm.next = pin1;

									break;
								}
							}
						}
					}
				}
	}

	phase_data = &pwm_phase[new_set];

	phase_data->phase[0].duty = 0;
	phase_data->phase[0].delay = 0;
	phase_data->phase[0].mask = 0x0000;
	phase_data->size = 1;

	for(pin1 = pwm_head, duty = 0; (phase_data->size < (io_gpio_pwm_max_channels + 1)) && (pin1 >= 0); pin1 = pin1_data->pwm.next)
	{
		pin1_data = &gpio_data[pin1];

		phase_data->phase[0].mask |= 1 << pin1;

		delta = pin1_data->pwm.duty - duty;
		duty = pin1_data->pwm.duty;

		if(delta != 0)
		{
			phase_data->phase[phase_data->size - 1].delay	= delta;
			phase_data->phase[phase_data->size].duty		= pin1_data->pwm.duty;
			phase_data->phase[phase_data->size].delay		= pwm_period - 1 - pin1_data->pwm.duty;
			phase_data->phase[phase_data->size].mask		= 1 << pin1;
			phase_data->size++;
		}
		else
			phase_data->phase[phase_data->size - 1].mask |= 1 << pin1;
	}

	if(phase_data->size < 2)
		phase_data->size = 0;

#if 0
	dprintf("* program");

	for(phase = 0; phase < phase_data->size; phase++)
	{
		dprintf("phase:%d du:%d de:%d m:%x",
				phase,
				phase_data->phase[phase].duty,
				phase_data->phase[phase].delay,
				phase_data->phase[phase].mask);
	}
#endif

	if(!pwm_isr_enabled()) // ISR will turn itself off immediately when no phases present
	{
		pwm_isr_enable(true);
		pwm_timer_reload(32);
	}
	else
		io_gpio_flags.pwm_swap_phase_set = 1;

	return(true);
}

// other

irom io_error_t io_gpio_init(const struct io_info_entry_T *info)
{
	gpio_pc_pins_previous = gpio_get_mask(); // init pin change notification

	pwm_current_phase_set = 0;
	io_gpio_flags.pwm_swap_phase_set = 0;

	pwm_phase[0].size = 0;
	pwm_phase[1].size = 0;

	pwm_isr_enable(false);
	ets_isr_attach(ETS_FRC_TIMER1_INUM, pwm_isr, 0);
	set_peri_reg_mask(EDGE_INT_ENABLE_REG, BIT1);
	write_peri_reg(PERIPHS_TIMER_BASEDDR + FRC1_CTRL_ADDRESS, FRC1_ENABLE_TIMER | FRC1_DIVIDE_BY_16 | FRC1_EDGE_INT);

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
		pin_config = &io_config[io][pin];

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
			gpio_direction(pin, 1);
			gpio_pin_data->pwm.duty = 0;
			gpio_set(pin, 0);
			pwm_go();

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
	unsigned int pwm_period;

	if(!config_get_int("pwm.period", -1, -1, &pwm_period))
		pwm_period = 65536;

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
				unsigned int duty, frequency, dutypct, dutypctfraction;

				duty = gpio_pin_data->pwm.duty;
				frequency = 5000000 / pwm_period;

				dutypct = duty * 100 / (pwm_period - 1);
				dutypctfraction = duty * 10000 / (pwm_period - 1);
				dutypctfraction -= dutypct * 100;

				if(!pwm_isr_enabled())
					frequency = 0;

				string_format(dst, "frequency: %u Hz, duty: %u (%u.%02u %%), state: %s",
						frequency, duty, dutypct, dutypctfraction, onoff(gpio_get(pin)));

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
			*value = gpio_pin_data->pwm.duty;

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
	int saved_value;

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
			if(value < 0)
				value = 0;

			if(gpio_pin_data->pwm.duty != (unsigned int)value)
			{
				saved_value = value;
				gpio_pin_data->pwm.duty = value;

				if(!pwm_go())
					gpio_pin_data->pwm.duty = saved_value;
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

irom app_action_t application_function_pwm_period(const string_t *src, string_t *dst)
{
	int new_pwm_period;

	if(parse_int(1, src, &new_pwm_period, 0) == parse_ok)
	{
		if((new_pwm_period < 256) || (new_pwm_period > 65536))
		{
			string_format(dst, "pwm-period: invalid period: %d (must be 256-65536)\n", new_pwm_period);
			return(app_action_error);
		}

		config_set_int("pwm.period", -1, -1, new_pwm_period);

		pwm_go();
	}

	if(!config_get_int("pwm.period", -1, -1, &new_pwm_period))
		new_pwm_period = 65536;

	string_format(dst, "pwm_period: %d\n", new_pwm_period);

	return(app_action_normal);
}
