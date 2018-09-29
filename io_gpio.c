#include "io_gpio.h"

#include "stats.h"
#include "util.h"
#include "esp-alt-register.h"

#include <user_interface.h>
#include <osapi.h>
#include <ets_sys.h>

#include <stdlib.h>

enum
{
	io_gpio_pin_size = 16,
	io_gpio_pwm_max_channels = 4,
};

typedef enum
{
	io_uart_none = 0,
	io_uart_rx,
	io_uart_tx,
} io_uart_t;

enum
{
	gpio_pdm_source =	1 << 0,
	gpio_open_drain =	1 << 2,
} gpio_pin_output_mode;

enum
{
	gpio_pdm_reg =				0x60000368,
	gpio_pdm_enable =			1 << 16,
	gpio_pdm_prescale_shift =	8,
	gpio_pdm_prescale_mask =	0xff,
	gpio_pdm_target_shift =		0,
	gpio_pdm_target_mask =		0xff,
} gpio_pdm;

typedef const struct
{
	const _Bool			valid;
	const unsigned int	mux;
	const unsigned int	func;
	const io_uart_t		uart_pin;
	const unsigned int	uart_func;
	const unsigned int	uart;
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

static gpio_data_pin_t gpio_data[io_gpio_pin_size];

static gpio_info_t gpio_info_table[io_gpio_pin_size] =
{
	{ true, 	PERIPHS_IO_MUX_GPIO0_U,		FUNC_GPIO0,		io_uart_none,	0,				0	},
	{ true,		PERIPHS_IO_MUX_U0TXD_U,		FUNC_GPIO1,		io_uart_tx,		FUNC_U0TXD,		0	},
	{ true,		PERIPHS_IO_MUX_GPIO2_U,		FUNC_GPIO2,		io_uart_tx,		FUNC_U1TXD_BK,	1	},
	{ true,		PERIPHS_IO_MUX_U0RXD_U,		FUNC_GPIO3,		io_uart_rx,		FUNC_U0RXD,		0	},
	{ true,		PERIPHS_IO_MUX_GPIO4_U,		FUNC_GPIO4,		io_uart_none,	0,				0	},
	{ true,		PERIPHS_IO_MUX_GPIO5_U,		FUNC_GPIO5,		io_uart_none,	0,				0	},
	{ false,	PERIPHS_IO_MUX_SD_CLK_U,	FUNC_GPIO6,		io_uart_none,	0,				0	},
	{ false,	PERIPHS_IO_MUX_SD_DATA0_U,	FUNC_GPIO7,		io_uart_none,	0,				0	},
	{ false,	PERIPHS_IO_MUX_SD_DATA1_U,	FUNC_GPIO8,		io_uart_none,	0,				0	},
	{ false,	PERIPHS_IO_MUX_SD_DATA2_U,	FUNC_GPIO9,		io_uart_none,	0,				0	},
	{ false,	PERIPHS_IO_MUX_SD_DATA3_U,	FUNC_GPIO10,	io_uart_none,	0,				0	},
	{ false,	PERIPHS_IO_MUX_SD_CMD_U,	FUNC_GPIO11,	io_uart_none,	0,				0	},
	{ true,		PERIPHS_IO_MUX_MTDI_U,		FUNC_GPIO12,	io_uart_none,	0,				0	},
	{ true,		PERIPHS_IO_MUX_MTCK_U, 		FUNC_GPIO13,	io_uart_none,	0,				0	},
	{ true,		PERIPHS_IO_MUX_MTMS_U, 		FUNC_GPIO14,	io_uart_none,	0,				0	},
	{ true,		PERIPHS_IO_MUX_MTDO_U, 		FUNC_GPIO15,	io_uart_none,	0,				0	},
};

typedef struct
{
	unsigned int delay;
	const unsigned int delay_cpu_low_speed;
	const unsigned int delay_cpu_high_speed;
} pwm_delay_t;

enum
{
	pwm_table_delay_size = 24,
};

pwm_delay_t pwm_delay_entry[pwm_table_delay_size] =
{
	{	0,	16,		8,		},	// special case, this entry is only used for the default case (i.e. delay >= pwm_table_delay_size)
	{	1,	0,		0,		},	// special case, not used
	{	2,	0,		4,		},
	{	3,	1,		44,		},
	{	4,	12,		78,		},
	{	5,	30,		110,	},
	{	6,	48,		144,	},
	{	7,	62,		176,	},
	{	8,	78,		208,	},
	{	9,	94,		240,	},
	{	10,	112,	272,	},
	{	11,	128,	306,	},
	{	12,	144,	338,	},
	{	13,	160,	368,	},
	{	14,	176,	400,	},
	{	15,	190,	432,	},
	{	16,	208,	464,	},
	{	17,	222,	496,	},
	{	18,	238,	526,	},
	{	19,	256,	558,	},
	{	20,	272,	590,	},
	{	21,	286,	622,	},
	{	22,	304,	654,	},
	{	23,	320,	686,	},
};

static void pwm_go(void);

// set GPIO direction

irom static void gpio_direction_clear_mask(uint32_t mask)
{
	gpio_reg_write(GPIO_ENABLE_W1TC_ADDRESS, mask);
}

irom static void gpio_direction_set_mask(uint32_t mask)
{
	gpio_reg_write(GPIO_ENABLE_W1TS_ADDRESS, mask);
}

irom static void gpio_direction(unsigned int io, _Bool onoff)
{
	if(onoff)
		gpio_direction_set_mask(1 << io);
	else
		gpio_direction_clear_mask(1 << io);
}

// disable / enable pullup

irom static _Bool gpio_enable_pullup(unsigned int pin, _Bool onoff)
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

// clear / set quasi-PWM using PDM (Pulse Density Modulation, aka "sigma-delta") mode

irom static void gpio_enable_pdm(unsigned int pin, _Bool onoff)
{
	uint32_t pinaddr;
	uint32_t value;

	if(pin >= io_gpio_pin_size)
		return;

	pinaddr	= gpio_pin_addr(pin);
	value	= gpio_reg_read(pinaddr);

	if(onoff)
		value |= gpio_pdm_source;
	else
		value &= ~gpio_pdm_source;

	gpio_reg_write(pinaddr, value);
}

// clear / set open drain mode

irom static void gpio_enable_open_drain(unsigned int pin, _Bool onoff)
{
	uint32_t pinaddr;
	uint32_t value;

	if(pin >= io_gpio_pin_size)
		return;

	pinaddr	= gpio_pin_addr(pin);
	value	= gpio_reg_read(pinaddr);

	if(onoff)
		value |= gpio_open_drain;
	else
		value &= ~gpio_open_drain;

	gpio_reg_write(pinaddr, value);
}

// select pin function

irom static _Bool gpio_func_select(unsigned int pin, unsigned int func)
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

irom static void pdm_set_prescale(unsigned int value)
{
	uint32_t regval;

	regval = read_peri_reg(gpio_pdm_reg);
	regval &= ~(gpio_pdm_prescale_mask << gpio_pdm_prescale_shift);
	regval |= (value & gpio_pdm_prescale_mask) << gpio_pdm_prescale_shift;

	write_peri_reg(gpio_pdm_reg, regval);
}

irom static void pdm_set_target(unsigned int value)
{
	uint32_t regval;

	regval = read_peri_reg(gpio_pdm_reg);
	regval &= ~(gpio_pdm_target_mask << gpio_pdm_target_shift);
	regval |= (value & gpio_pdm_target_mask) << gpio_pdm_target_shift;

	write_peri_reg(gpio_pdm_reg, regval);
}

irom static unsigned int pdm_get_prescale(void)
{
	uint32_t regval;

	regval = read_peri_reg(gpio_pdm_reg);
	regval >>= gpio_pdm_prescale_shift;
	regval &= gpio_pdm_prescale_mask;

	return(regval);
}

irom static unsigned int pdm_get_target(void)
{
	uint32_t regval;

	regval = read_peri_reg(gpio_pdm_reg);
	regval >>= gpio_pdm_target_shift;
	regval &= gpio_pdm_target_mask;

	return(regval);
}

irom static void pdm_enable(_Bool onoff)
{
	uint32_t regval;

	regval = read_peri_reg(gpio_pdm_reg);

	if(onoff)
	{
		pdm_set_prescale(0);
		pdm_set_target(0);
		regval |= gpio_pdm_enable;
	}
	else
		regval &= ~gpio_pdm_enable;

	write_peri_reg(gpio_pdm_reg, regval);
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
	uint32_t		static_set_mask;
	uint32_t		static_clear_mask;
	pwm_phase_t		phase[io_gpio_pwm_max_channels + 1];
} pwm_phases_t;

typedef struct
{
	unsigned int	pwm_reset_phase_set:1;
	unsigned int	pwm_next_phase_set:1;
	unsigned int	pwm_int_enabled:1;
} io_gpio_flags_t;

static unsigned int		pwm_current_phase_set;
static pwm_phases_t		pwm_phase[2];
static io_gpio_flags_t	io_gpio_flags;

static int pwm_head;
static unsigned int pwm1_period;

static void pwm_isr(void);

irom _Bool io_gpio_pwm1_period_set(unsigned int period, _Bool load, _Bool save)
{
	unsigned int period_load;
	string_init(varname_pwmperiod, "pwm.period");

	if(load && config_get_int(&varname_pwmperiod, -1, -1, &period_load))
		period = period_load;

	switch(period)
	{
		case(256):
		case(512):
		case(1024):
		case(2048):
		case(4096):
		case(8196):
		case(16384):
		case(32768):
		case(65536):
			break;

		default:
			return(false);
	}

	pwm1_period = period;

	if(save)
	{
		if(pwm1_period == 65536)
			config_delete(&varname_pwmperiod, -1, -1, false);
		else
			config_set_int(&varname_pwmperiod, -1, -1, pwm1_period);
	}

	pwm_go();

	return(true);
}

irom attr_pure unsigned int io_gpio_pwm1_period_get(void)
{
	return(pwm1_period);
}

irom static void pwm_isr_setup(void)
{
	NmiTimSetFunc(pwm_isr);
	write_peri_reg(TIMER0_CTRL_REG, TIMER0_CTRL_INT_EDGE | TIMER0_CTRL_DIVIDE_BY_16 | TIMER0_CTRL_ENABLE_TIMER);
	clear_peri_reg_mask(INT_ENABLE_REG, INT_ENABLE_TIMER0);
	ets_isr_unmask(1 << ETS_TIMER0_INUM);
}

attr_inline _Bool pwm_isr_enabled(void)
{
	return(io_gpio_flags.pwm_int_enabled);
}

attr_inline void pwm_isr_enable(_Bool enable)
{
	if(enable)
	{
		io_gpio_flags.pwm_int_enabled = 1;
		set_peri_reg_mask(INT_ENABLE_REG, INT_ENABLE_TIMER0);
	}
	else
	{
		clear_peri_reg_mask(INT_ENABLE_REG, INT_ENABLE_TIMER0);
		io_gpio_flags.pwm_int_enabled = 0;
	}
}

attr_inline void pwm_timer_set(uint32_t value)
{
	write_peri_reg(TIMER0_LOAD_REG, value);
}

attr_inline uint32_t pwm_timer_get(void)
{
	return(read_peri_reg(TIMER0_COUNT_REG));
}

attr_speed iram static void pwm_isr(void)
{
	static unsigned int	phase, ticks_to_next_phase;

	stat_pwm_timer_interrupts++;

	if(!pwm_isr_enabled())
	{
		stat_pwm_timer_interrupts_while_nmi_masked++;
		return;
	}

	for(;;)
	{
		if(phase >= pwm_phase[pwm_current_phase_set].size)
			phase = 0;

		if(phase == 0)
		{
			if(io_gpio_flags.pwm_next_phase_set)
				pwm_current_phase_set ^= 0x01;

			if(io_gpio_flags.pwm_reset_phase_set || io_gpio_flags.pwm_next_phase_set)
			{
				gpio_set_mask(pwm_phase[pwm_current_phase_set].static_set_mask);
				gpio_clear_mask(pwm_phase[pwm_current_phase_set].static_clear_mask);
				io_gpio_flags.pwm_reset_phase_set = 0;
				io_gpio_flags.pwm_next_phase_set = 0;
			}

			if(pwm_phase[pwm_current_phase_set].size < 2)
			{
				pwm_isr_enable(false);
				return;
			}

			gpio_set_mask(pwm_phase[pwm_current_phase_set].phase[phase].mask);
		}
		else
			gpio_clear_mask(pwm_phase[pwm_current_phase_set].phase[phase].mask);

		ticks_to_next_phase = pwm_phase[pwm_current_phase_set].phase[phase].delay;
		phase++;

		if(ticks_to_next_phase < 2)
			continue;
		else
			if(ticks_to_next_phase < pwm_table_delay_size)
				csleep(pwm_delay_entry[ticks_to_next_phase].delay);
			else
				return(pwm_timer_set(ticks_to_next_phase - pwm_delay_entry[0].delay));
	}
}

irom static void pwm_go(void)
{
	io_config_pin_entry_t *pin1_config;
	gpio_info_t *pin1_info;
	gpio_data_pin_t *pin1_data, *pin2_data, *pin3_data;
	int pin1, pin2, pin3;
	pwm_phases_t *phase_data;
	unsigned int duty, delta, new_phase_set;
	uint32_t timer_value;
	_Bool isr_enabled;

	isr_enabled = pwm_isr_enabled();
	pwm_isr_enable(false);
	timer_value = pwm_timer_get();

	if(timer_value < 32)
		timer_value = 32;

	if(timer_value > pwm1_period)
		timer_value = pwm1_period;

	// if next set is already active or ISR is off, suspend ISR and re-configure current set

	if(io_gpio_flags.pwm_next_phase_set || !isr_enabled)
		new_phase_set = pwm_current_phase_set;
	else // configure new set, release ISR using current set
	{
		new_phase_set = (pwm_current_phase_set + 1) & 0x01;
		pwm_timer_set(timer_value);
		pwm_isr_enable(true);
	}

	pwm_head = -1;

	// reset linked list

	for(pin1 = 0; pin1 < io_gpio_pin_size; pin1++)
	{
		pin1_config	= &io_config[io_id_gpio][pin1];
		pin1_info	= &gpio_info_table[pin1];
		pin1_data	= &gpio_data[pin1];

		if((pin1_info->valid) &&
				(pin1_config->llmode == io_pin_ll_output_pwm1))
		{
			pin1_data->pwm.this = pin1;
			pin1_data->pwm.next = -1;

			if(pin1_data->pwm.duty >= pwm1_period)
				pin1_data->pwm.duty = pwm1_period - 1;
		}
	}

	// create linked list

	phase_data = &pwm_phase[new_phase_set];
	phase_data->static_clear_mask = 0;
	phase_data->static_set_mask = 0;

	for(pin1 = 0; pin1 < io_gpio_pin_size; pin1++)
	{
		pin1_info	= &gpio_info_table[pin1];
		pin1_config	= &io_config[io_id_gpio][pin1];
		pin1_data	= &gpio_data[pin1];

		if(!pin1_info->valid || (pin1_config->llmode != io_pin_ll_output_pwm1))
			continue;

		if(pin1_data->pwm.duty == 0)
			phase_data->static_clear_mask |= 1 << pin1;
		else
			if((pin1_data->pwm.duty + 1) >= pwm1_period)
				phase_data->static_set_mask |= 1 << pin1;
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
			phase_data->phase[phase_data->size].delay		= pwm1_period - 1 - pin1_data->pwm.duty;
			phase_data->phase[phase_data->size].mask		= 1 << pin1;
			phase_data->size++;
		}
		else
			phase_data->phase[phase_data->size - 1].mask |= 1 << pin1;
	}

	if(phase_data->size < 2)
		phase_data->size = 0;

	if(new_phase_set == pwm_current_phase_set)
	{
		io_gpio_flags.pwm_reset_phase_set = 1;
		io_gpio_flags.pwm_next_phase_set = 0;
		pwm_timer_set(timer_value);
		pwm_isr_enable(true);
	}
	else
	{
		io_gpio_flags.pwm_reset_phase_set = 0;
		io_gpio_flags.pwm_next_phase_set = 1;
	}
}

// other

irom io_error_t io_gpio_init(const struct io_info_entry_T *info)
{
	unsigned int entry;
	_Bool cpu_high_speed = config_flags_get().flag.cpu_high_speed;

	pdm_enable(false);

	for(entry = 0; entry < pwm_table_delay_size; entry++)
		if(cpu_high_speed)
			pwm_delay_entry[entry].delay = pwm_delay_entry[entry].delay_cpu_high_speed;
		else
			pwm_delay_entry[entry].delay = pwm_delay_entry[entry].delay_cpu_low_speed;

	io_gpio_pwm1_period_set(65536, /*load*/true, /*save*/false);

	pwm_current_phase_set = 0;
	io_gpio_flags.pwm_reset_phase_set = 0;
	io_gpio_flags.pwm_next_phase_set = 0;

	pwm_phase[0].size = 0;
	pwm_phase[1].size = 0;

	gpio_init();
	pwm_isr_setup();

	return(io_ok);
}

iram void io_gpio_periodic_fast(int io, const struct io_info_entry_T *info, io_data_entry_t *data, io_flags_t *flags)
{
	static uint32_t gpio_pc_pins_previous;
	static _Bool first_call = true;

	int pin;
	uint32_t gpio_pc_pins_current;

	gpio_pc_pins_current = gpio_get_all();

	if(first_call)
		first_call = false;
	else
	{
		for(pin = 0; pin < io_gpio_pin_size; pin++)
		{
			io_config_pin_entry_t *pin_config = &io_config[io][pin];

			if(pin_config->llmode == io_pin_ll_counter)
			{
				gpio_data_pin_t *gpio_pin_data = &gpio_data[pin];

				if(gpio_pin_data->counter.debounce == 0)
				{
					if((gpio_pc_pins_previous & (1 << pin)) && !(gpio_pc_pins_current & (1 << pin)))
					{
						gpio_pin_data->counter.counter++;
						gpio_pin_data->counter.debounce = pin_config->speed;
						flags->counter_triggered = 1;
						stat_pc_counts++;
					}
				}
				else
				{
					if(gpio_pin_data->counter.debounce >= ms_per_fast_tick)
						gpio_pin_data->counter.debounce -= ms_per_fast_tick;
					else
						gpio_pin_data->counter.debounce = 0;
				}
			}
		}
	}

	gpio_pc_pins_previous = gpio_pc_pins_current;
}

irom io_error_t io_gpio_init_pin_mode(string_t *error_message, const struct io_info_entry_T *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin)
{
	gpio_info_t *gpio_info;
	gpio_data_pin_t *gpio_pin_data;

	if((pin < 0) || (pin >= io_gpio_pin_size))
	{
		if(error_message)
			string_format(error_message, "pin %d invalid\n", pin);
		return(io_error);
	}

	gpio_info = &gpio_info_table[pin];

	if(!gpio_info->valid)
	{
		if(error_message)
			string_append(error_message, "io invalid\n");
		return(io_error);
	}

	if(pin_config->llmode == io_pin_ll_disabled)
	{
		if(error_message)
			string_format(error_message, "pin %d disabled\n", pin);
		return(io_error);
	}

	gpio_func_select(pin, gpio_info->func);
	gpio_pin_intr_state_set(pin, GPIO_PIN_INTR_DISABLE);

	gpio_pin_data = &gpio_data[pin];

	switch(pin_config->llmode)
	{
		case(io_pin_ll_input_digital):
		case(io_pin_ll_counter):
		{
			gpio_direction(pin, 0);
			gpio_enable_open_drain(pin, 0);
			gpio_enable_pdm(pin, 0);
			gpio_enable_pullup(pin, pin_config->flags.pullup);

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
			gpio_enable_open_drain(pin, 0);
			gpio_enable_pdm(pin, 0);
			break;
		}

		case(io_pin_ll_output_pwm1):
		{
			gpio_direction(pin, 1);
			gpio_enable_open_drain(pin, 0);
			gpio_enable_pdm(pin, 0);
			gpio_pin_data->pwm.duty = 0;
			gpio_set(pin, 0);
			pwm_go();

			break;
		}

		case(io_pin_ll_output_pwm2):
		{
			gpio_direction(pin, 1);
			gpio_enable_open_drain(pin, 0);
			gpio_set(pin, 0);
			gpio_enable_pdm(pin, 1);
			gpio_pin_data->pwm.duty = 0;
			pdm_enable(true);

			break;
		}

		case(io_pin_ll_i2c):
		{
			gpio_direction(pin, 0);
			gpio_enable_pullup(pin, 0);
			gpio_direction(pin, 1);
			gpio_enable_open_drain(pin, 1);
			gpio_enable_pdm(pin, 0);
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

			if((pin_config->mode == io_pin_ledpixel) && (gpio_info->uart_pin != io_uart_tx))
			{
				if(error_message)
					string_format(error_message, "gpio pin %d (uart rx) cannot be used for ledpixel mode\n", pin);
				return(io_error);
			}

			gpio_func_select(pin, gpio_info->uart_func);
			gpio_enable_pullup(pin, pin_config->flags.pullup);

			break;
		}

		default:
		{
			if(error_message)
			{
				string_format(error_message, "gpio %d invalid mode: ", pin);
				io_string_from_ll_mode(error_message, pin_config->llmode, 0);
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

	if((pin < 0) || (pin >= io_gpio_pin_size))
		return(io_error);

	gpio_pin_data = &gpio_data[pin];

	if(!gpio_info_table[pin].valid)
		string_append(dst, "unusable i/o pin");
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

			case(io_pin_ll_i2c):
			{
				string_format(dst, "current state: %s",
						onoff(gpio_get(pin)));

				break;
			}

			case(io_pin_ll_output_pwm1):
			{
				unsigned int duty, frequency, dutypct, dutypctfraction;

				pwm_period = io_gpio_pwm1_period_get();

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

			case(io_pin_ll_output_pwm2):
			{
				unsigned int target, prescale, frequency;

				pwm_period = 0;
				frequency = 0;

				target = pdm_get_target();
				prescale = pdm_get_prescale();

				if(target > 0)
				{
					if(target <= 128)
						pwm_period = (prescale + 1) * 256 / target;
					else
						pwm_period = ((prescale + 1) * 256) / (256 - target);
				}

				if(pwm_period != 0)
					frequency = 80000000 / pwm_period;

				string_format(dst, "frequency: %u Hz, duty: %u %%, prescale: %u, target: %u, state: %s",
						frequency, gpio_pin_data->pwm.duty * 100 / 255, prescale, target, onoff(gpio_get(pin)));

				break;
			}

			case(io_pin_ll_uart):
			{
				unsigned int uart = gpio_info_table[pin].uart;

				if((uart != 0) && (uart != 1))
					string_append(dst, "<invalid uart>");
				else
				{
					string_format(dst, "uart %u, pin: ", uart);

					switch(gpio_info_table[pin].uart_pin)
					{
						case(io_uart_tx):
						{
							_Bool			enabled;
							unsigned int	character;

							uart_is_autofill(uart, &enabled, &character);
							string_format(dst, "tx, autofill: %s, character: 0x%02x", yesno(enabled), character);

							break;
						}

						case(io_uart_rx):
						{
							string_append(dst, "rx");
							break;
						}

						default:
						{
							string_append(dst, "<invalid uart pin mode>");
							break;
						}
					}
				}

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

iram io_error_t io_gpio_read_pin(string_t *error_message, const struct io_info_entry_T *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin, uint32_t *value)
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

		case(io_pin_ll_output_pwm1):
		case(io_pin_ll_output_pwm2):
		{
			*value = gpio_pin_data->pwm.duty;

			break;
		}

		case(io_pin_ll_uart):
		{
			_Bool			enabled;
			unsigned int	character;
			int				uart = gpio_info_table[pin].uart;

			if((uart < 0) || (uart > 1))
			{
				if(error_message)
					string_format(error_message, "cannot uart read from gpio %d\n", pin);
				return(io_error);
			}

			if(gpio_info_table[pin].uart_pin == io_uart_tx)
			{
				uart_is_autofill(uart, &enabled, &character);
				*value = !!enabled;
			}
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

iram io_error_t io_gpio_write_pin(string_t *error_message, const struct io_info_entry_T *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin, uint32_t value)
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

		case(io_pin_ll_output_pwm1):
		{
			if(gpio_pin_data->pwm.duty != value)
			{
				gpio_pin_data->pwm.duty = value;
				pwm_go();
			}

			break;
		}

		case(io_pin_ll_output_pwm2):
		{
			unsigned int prescale, target;

			gpio_pin_data->pwm.duty = value;

			if(value > 255)
				value = 255;

			prescale = 0;
			target = value;

			if(value > 0)
			{
				if(value < 128)
					prescale = (target * 2) - 1;
				else
					prescale = 256 - (target * 2) - 1;
			}

			pdm_set_prescale(prescale);
			pdm_set_target(target);

			break;
		}

		case(io_pin_ll_uart):
		{
			int uart = gpio_info_table[pin].uart;

			if((uart < 0) || (uart > 1))
			{
				if(error_message)
					string_format(error_message, "cannot uart write to gpio %d\n", pin);
				return(io_error);
			}

			if(gpio_info_table[pin].uart_pin == io_uart_tx)
			{
				if(value == 0)
				{
					uart_autofill(uart, false, 0);
					uart_set_initial(uart);
				}
				else
				{
					uart_data_bits(uart, 8);
					uart_stop_bits(uart, 1);
					uart_parity(uart, parity_none);
					uart_baudrate(uart, value << 1);
					uart_autofill(uart, true, 0x55);
				}
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

irom attr_const int io_gpio_get_uart_from_pin(unsigned int pin)
{
	if(pin >= max_pins_per_io)
		return(-1);

	if(gpio_info_table[pin].uart_pin == io_uart_none)
		return(-1);

	return(gpio_info_table[pin].uart);
}
