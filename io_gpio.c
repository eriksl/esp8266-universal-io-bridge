#include "io_gpio.h"

#include "stats.h"
#include "util.h"
#include "dispatch.h"
#include "sys_string.h"
#include "eagle.h"

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

enum
{
	io_gpio_pin_size = 16,
	io_gpio_pwm_max_channels = 4,
	io_gpio_rotary_encoder_timeout = 2,
};

typedef enum
{
	io_gpio_func_gpio = 0,
	io_gpio_func_uart,
} io_func_t;

typedef enum
{
	io_uart_pin_none = 0,
	io_uart_pin_rx,
	io_uart_pin_tx,
} io_uart_pin_t;

assert_size(io_uart_pin_t, 4);

enum
{
	gi_valid = 1 << 0,
};

typedef const struct
{
	const uint32_t		flags;
	const uint32_t		mux;
	const uint32_t		func;
	const io_uart_pin_t	uart_pin;
	const uint32_t		uart_func;
	const uint32_t		uart_instance;
} gpio_info_t;

assert_size(bool, 1);
assert_size(gpio_info_t, 24);

typedef union
{
	struct
	{
		unsigned int counter;
		unsigned int debounce:16;
		unsigned int timeout:16;
	} counter;

	struct
	{
		int pwm_next;
		unsigned int pwm_duty;
	} pwm;
} gpio_data_pin_t;

assert_size(gpio_data_pin_t, 8);

static gpio_data_pin_t gpio_data[io_gpio_pin_size];

roflash static gpio_info_t gpio_info_table[io_gpio_pin_size] =
{
	{ gi_valid, 	PERIPHS_IO_MUX_GPIO0_U,		FUNC_GPIO0,		io_uart_pin_none,	0,				~0	},
	{ gi_valid,		PERIPHS_IO_MUX_U0TXD_U,		FUNC_GPIO1,		io_uart_pin_tx,		FUNC_U0TXD,		0	},
	{ gi_valid,		PERIPHS_IO_MUX_GPIO2_U,		FUNC_GPIO2,		io_uart_pin_tx,		FUNC_U1TXD_BK,	1	},
	{ gi_valid,		PERIPHS_IO_MUX_U0RXD_U,		FUNC_GPIO3,		io_uart_pin_rx,		FUNC_U0RXD,		0	},
	{ gi_valid,		PERIPHS_IO_MUX_GPIO4_U,		FUNC_GPIO4,		io_uart_pin_none,	0,				~0	},
	{ gi_valid,		PERIPHS_IO_MUX_GPIO5_U,		FUNC_GPIO5,		io_uart_pin_none,	0,				~0	},
	{ 0,			PERIPHS_IO_MUX_SD_CLK_U,	FUNC_GPIO6,		io_uart_pin_none,	0,				~0	},
	{ 0,			PERIPHS_IO_MUX_SD_DATA0_U,	FUNC_GPIO7,		io_uart_pin_none,	0,				~0	},
	{ 0,			PERIPHS_IO_MUX_SD_DATA1_U,	FUNC_GPIO8,		io_uart_pin_none,	0,				~0	},
	{ 0,			PERIPHS_IO_MUX_SD_DATA2_U,	FUNC_GPIO9,		io_uart_pin_none,	0,				~0	},
	{ 0,			PERIPHS_IO_MUX_SD_DATA3_U,	FUNC_GPIO10,	io_uart_pin_none,	0,				~0	},
	{ 0,			PERIPHS_IO_MUX_SD_CMD_U,	FUNC_GPIO11,	io_uart_pin_none,	0,				~0	},
	{ gi_valid,		PERIPHS_IO_MUX_MTDI_U,		FUNC_GPIO12,	io_uart_pin_none,	0,				~0	},
	{ gi_valid,		PERIPHS_IO_MUX_MTCK_U, 		FUNC_GPIO13,	io_uart_pin_none,	0,				~0	},
	{ gi_valid,		PERIPHS_IO_MUX_MTMS_U, 		FUNC_GPIO14,	io_uart_pin_none,	0,				~0	},
	{ gi_valid,		PERIPHS_IO_MUX_MTDO_U, 		FUNC_GPIO15,	io_uart_pin_none,	0,				~0	},
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
	{	0,	18,		9,		},	// special case, this entry is only used for the default case (i.e. delay >= pwm_table_delay_size)
	{	1,	0,		0,		},	// special case, not used
	{	2,	0,		0,		},
	{	3,	1,		20,		},
	{	4,	2,		55,		},
	{	5,	4,		90,		},
	{	6,	28,		120,	},
	{	7,	44,		152,	},
	{	8,	60,		184,	},
	{	9,	76,		216		},
	{	10,	94,		248,	},
	{	11,	110,	284,	},
	{	12,	126,	314,	},
	{	13,	142,	344,	},
	{	14,	158,	376,	},
	{	15,	172,	408,	},
	{	16,	190,	440,	},
	{	17,	200,	472,	},
	{	18,	220,	502,	},
	{	19,	238,	534,	},
	{	20,	254,	566,	},
	{	21,	268,	598,	},
	{	22,	286,	630,	},
	{	23,	302,	662,	},
};

static void pwm_go(void);

// set GPIO direction

attr_inline void gpio_direction_clear_mask(uint32_t mask)
{
	gpio_reg_write(GPIO_ENABLE_W1TC_ADDRESS, mask);
}

attr_inline void gpio_direction_set_mask(uint32_t mask)
{
	gpio_reg_write(GPIO_ENABLE_W1TS_ADDRESS, mask);
}

attr_inline void gpio_direction(unsigned int pin, bool set_on)
{
	if(set_on)
		gpio_direction_set_mask(1 << pin);
	else
		gpio_direction_clear_mask(1 << pin);
}

// disable / enable pullup

attr_inline bool gpio_enable_pullup(unsigned int pin, bool onoff)
{
	uint32_t value;
	gpio_info_t *gpio_pin_info;

	if(pin >= io_gpio_pin_size)
		return(false);

	gpio_pin_info = &gpio_info_table[pin];

	if(!(gpio_pin_info->flags & gi_valid))
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

static void gpio_enable_pdm(unsigned int pin, bool onoff)
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

static void gpio_enable_open_drain(unsigned int pin, bool onoff)
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

attr_inline bool gpio_func_select(unsigned int pin, io_func_t gpio_pin_mode)
{
	gpio_info_t *gpio_pin_info;
	uint32_t value, func;

	if(pin >= io_gpio_pin_size)
		return(false);

	gpio_pin_info = &gpio_info_table[pin];

	if(!(gpio_pin_info->flags & gi_valid))
		return(false);

	switch(gpio_pin_mode)
	{
		case(io_gpio_func_gpio): func = gpio_pin_info->func; break;
		case(io_gpio_func_uart): func = gpio_pin_info->uart_func; break;
		default: return(false);
	}

	value = read_peri_reg(gpio_pin_info->mux);

	value &= ~(PERIPHS_IO_MUX_FUNC << PERIPHS_IO_MUX_FUNC_S);
	value |= (func & (1 << 2)) << (PERIPHS_IO_MUX_FUNC_S + 2);
	value |= (func & (1 << 1)) << (PERIPHS_IO_MUX_FUNC_S + 0);
	value |= (func & (1 << 0)) << (PERIPHS_IO_MUX_FUNC_S + 0);

	write_peri_reg(gpio_pin_info->mux, value);

	return(true);
}

static void pdm_set_prescale(unsigned int value)
{
	uint32_t regval;

	regval = read_peri_reg(gpio_pdm_reg);
	regval &= ~(gpio_pdm_prescale_mask << gpio_pdm_prescale_shift);
	regval |= (value & gpio_pdm_prescale_mask) << gpio_pdm_prescale_shift;

	write_peri_reg(gpio_pdm_reg, regval);
}

static void pdm_set_target(unsigned int value)
{
	uint32_t regval;

	regval = read_peri_reg(gpio_pdm_reg);
	regval &= ~(gpio_pdm_target_mask << gpio_pdm_target_shift);
	regval |= (value & gpio_pdm_target_mask) << gpio_pdm_target_shift;

	write_peri_reg(gpio_pdm_reg, regval);
}

static unsigned int pdm_get_prescale(void)
{
	uint32_t regval;

	regval = read_peri_reg(gpio_pdm_reg);
	regval >>= gpio_pdm_prescale_shift;
	regval &= gpio_pdm_prescale_mask;

	return(regval);
}

static unsigned int pdm_get_target(void)
{
	uint32_t regval;

	regval = read_peri_reg(gpio_pdm_reg);
	regval >>= gpio_pdm_target_shift;
	regval &= gpio_pdm_target_mask;

	return(regval);
}

static void pdm_enable(bool onoff)
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
	uint32_t	phase_duty;
	uint32_t	phase_delay;
	uint16_t	phase_active_pins_clear_mask;
} pwm_phase_t;

assert_size(pwm_phase_t, 12);

typedef struct
{
	uint32_t		amount_phases;
	uint32_t		static_pins_on_mask;
	uint32_t		static_pins_off_mask;
	uint32_t		active_pins_all_set_mask;
	uint32_t		active_pins_noduty1_set_mask;	// special case for duty is 1 period, only set intermittently
	uint32_t		active_pins_duty1_clear_mask;	// special case for duty is 1 period, clear immediately after set
	pwm_phase_t		phase[io_gpio_pwm_max_channels + 1];
} pwm_phases_t;

assert_size(pwm_phases_t, 84);

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
static unsigned int pwm1_width;

static void pwm_isr(void);

attr_pure unsigned int io_gpio_pwm1_width_get(void)
{
	return(pwm1_width);
}

attr_inline unsigned int pwm1_period(void)
{
	return((unsigned int)(1 << pwm1_width));
}

attr_inline attr_const unsigned int pwm2_period(void)
{
	return(0x100);
}

bool io_gpio_pwm1_width_set(unsigned int width, bool load, bool save)
{
	unsigned int current;
	roflash static const uint32_t pwm_widths[][2] =
	{
		{	6,		6	},
		{	7,		7	},
		{	8,		8	},
		{	9,		9	},
		{	10,		10	},
		{	11,		11	},
		{	12,		12	},
		{	13,		13	},
		{	14,		14	},
		{	15,		15	},
		{	16,		16	},
		{	17,		17	},
		{	18,		18	},
		{	16,		4	},
		{	32,		5	},
		{	64,		6	},
		{	128,	7	},
		{	256,	8	},
		{	512,	9	},
		{	1024,	10	},
		{	2048,	11	},
		{	4096,	12	},
		{	8192,	13	},
		{	16384,	14	},
		{	32768,	15	},
		{	65536,	16	},
		{	131072,	17	},
		{	262144,	18	},
		{	0,		0	}
	};

	if(load && !config_get_uint("pwm.period", &width, -1, -1) && !config_get_uint("pwm.width", &width, -1, -1))
		width = 16;

	for(current = 0; pwm_widths[current][0] != 0; current++)
	{
		if(pwm_widths[current][0] == width)
		{
			pwm1_width = pwm_widths[current][1];
			break;
		}
	}

	if(pwm_widths[current][0] == 0)
		return(false);

	if(save)
	{
		config_open_write();

		config_delete("pwm.period", false, -1, -1);

		if((pwm1_width == 16) || (pwm1_width == 65536))
			config_delete("pwm.width", false, -1, -1);
		else
			config_set_int("pwm.width", pwm1_width, -1, -1);

		config_close_write();
	}

	pwm_go();

	return(true);
}

static void pwm_isr_setup(void)
{
	NmiTimSetFunc(pwm_isr);
	write_peri_reg(TIMER0_CTRL_REG, TIMER0_CTRL_INT_EDGE | TIMER0_CTRL_DIVIDE_BY_16 | TIMER0_CTRL_ENABLE_TIMER);
	clear_peri_reg_mask(INT_ENABLE_REG, INT_ENABLE_TIMER0);
	ets_isr_unmask(1 << ETS_TIMER0_INUM);
}

attr_inline bool pwm_isr_enabled(void)
{
	return(io_gpio_flags.pwm_int_enabled);
}

attr_inline void pwm_isr_enable(bool enable)
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

iram static void pwm_isr(void)
{
	static unsigned int	phase, ticks_to_next_phase;
	const pwm_phases_t *current_phase_set;

	stat_pwm_timer_interrupts++;

	if(!pwm_isr_enabled())
	{
		stat_pwm_timer_interrupts_while_nmi_masked++;
		return;
	}

	current_phase_set = &pwm_phase[pwm_current_phase_set];

	for(;;)
	{
		if(phase >= current_phase_set->amount_phases)
			phase = 0;

		if(phase == 0)
		{
			if(io_gpio_flags.pwm_next_phase_set)
			{
				pwm_current_phase_set ^= 0x01;
				current_phase_set = &pwm_phase[pwm_current_phase_set];
			}

			if(io_gpio_flags.pwm_reset_phase_set || io_gpio_flags.pwm_next_phase_set)
			{
				gpio_set_mask(current_phase_set->static_pins_on_mask);
				gpio_clear_mask(current_phase_set->static_pins_off_mask);
				io_gpio_flags.pwm_reset_phase_set = 0;
				io_gpio_flags.pwm_next_phase_set = 0;
			}

			if(current_phase_set->amount_phases < 2)
			{
				pwm_isr_enable(false);
				return;
			}

			if((++stat_pwm_cycles & 0x07) == 0x00)
				gpio_set_mask(current_phase_set->active_pins_all_set_mask);
			else
				gpio_set_mask(current_phase_set->active_pins_noduty1_set_mask);

			gpio_clear_mask(current_phase_set->active_pins_duty1_clear_mask);
		}
		else
			gpio_clear_mask(current_phase_set->phase[phase].phase_active_pins_clear_mask);

		ticks_to_next_phase = current_phase_set->phase[phase].phase_delay;
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

iram static void pwm_go(void)
{
	io_config_pin_entry_t *pin1_config;
	gpio_info_t *pin1_info;
	gpio_data_pin_t *pin1_data, *pin2_data, *pin3_data;
	int pin1, pin2, pin3;
	pwm_phases_t *phase_data;
	unsigned int duty, delta, new_phase_set;
	uint32_t timer_value;
	bool isr_enabled;

	isr_enabled = pwm_isr_enabled();
	pwm_isr_enable(false);
	timer_value = pwm_timer_get();

	if(timer_value < 32)
		timer_value = 32;

	if(timer_value >= pwm1_period())
		timer_value = pwm1_period() - 1;

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

		if((pin1_info->flags & gi_valid) &&
				(pin1_config->llmode == io_pin_ll_output_pwm1))
		{
			pin1_data->pwm.pwm_next = -1;

			if(pin1_data->pwm.pwm_duty >= pwm1_period())
				pin1_data->pwm.pwm_duty = pwm1_period() - 1;
		}
	}

	// create linked list

	phase_data = &pwm_phase[new_phase_set];
	phase_data->static_pins_off_mask = 0;
	phase_data->static_pins_on_mask = 0;

	for(pin1 = 0; pin1 < io_gpio_pin_size; pin1++)
	{
		pin1_info	= &gpio_info_table[pin1];
		pin1_config	= &io_config[io_id_gpio][pin1];
		pin1_data	= &gpio_data[pin1];

		if(!(pin1_info->flags & gi_valid) || (pin1_config->llmode != io_pin_ll_output_pwm1))
			continue;

		if(pin1_data->pwm.pwm_duty == 0)
			phase_data->static_pins_off_mask |= 1 << pin1;
		else
			if((pin1_data->pwm.pwm_duty + 1) >= pwm1_period())
				phase_data->static_pins_on_mask |= 1 << pin1;
			else
				if(pwm_head < 0)
				{
					pwm_head = pin1;
					pin1_data->pwm.pwm_next = -1;
				}
				else
				{
					pin2 = pwm_head;
					pin2_data = &gpio_data[pin2];

					if(pin2_data->pwm.pwm_duty > pin1_data->pwm.pwm_duty)
					{
						pwm_head = pin1;
						pin1_data->pwm.pwm_next = pin2;
					}
					else
					{
						for(pin2 = pwm_head; pin2 >= 0; pin2 = pin3)
						{
							pin2_data	= &gpio_data[pin2];
							pin3		= pin2_data->pwm.pwm_next;

							if(pin3 < 0)
							{
								pin1_data->pwm.pwm_next = -1;
								pin2_data->pwm.pwm_next = pin1;

								break;
							}
							else
							{
								pin3_data = &gpio_data[pin3];

								if(pin3_data->pwm.pwm_duty > pin1_data->pwm.pwm_duty)
								{
									pin1_data->pwm.pwm_next = pin3;
									pin2_data->pwm.pwm_next = pin1;

									break;
								}
							}
						}
					}
				}
	}

	phase_data->phase[0].phase_duty = 0;
	phase_data->phase[0].phase_delay = 0;
	phase_data->phase[0].phase_active_pins_clear_mask = 0x0000;
	phase_data->amount_phases = 1;
	phase_data->active_pins_all_set_mask = 0x0000;
	phase_data->active_pins_noduty1_set_mask = 0x0000;
	phase_data->active_pins_duty1_clear_mask = 0x0000;

	for(pin1 = pwm_head, duty = 0; (phase_data->amount_phases < (io_gpio_pwm_max_channels + 1)) && (pin1 >= 0); pin1 = pin1_data->pwm.pwm_next)
	{
		pin1_data = &gpio_data[pin1];

		delta = pin1_data->pwm.pwm_duty - duty;
		duty = pin1_data->pwm.pwm_duty;

		/*
		 * treat pins with duty == 1 specially:
		 * 	- add it to the duty1_clear_mask, so it gets cleared immediately
		 * 	  after settings to ensure the smallest "on time"
		 *  - if pwm1_extended is set, don't add it to the noduty1_set_mask,
		 *    so it only gets set intermittently, to realise an even smaller
		 *    duty cycle, using an effectively lower refresh cycle
		 */

		phase_data->active_pins_all_set_mask |= 1 << pin1;
		phase_data->active_pins_noduty1_set_mask |= 1 << pin1;

		if(duty == 1)
		{
			phase_data->active_pins_duty1_clear_mask |= 1 << pin1;

			if(config_flags_match(flag_pwm1_extend))
				phase_data->active_pins_noduty1_set_mask &= ~(1 << pin1);
		}

		if(delta != 0)
		{
			phase_data->phase[phase_data->amount_phases - 1].phase_delay = delta;
			phase_data->phase[phase_data->amount_phases].phase_duty = pin1_data->pwm.pwm_duty;
			phase_data->phase[phase_data->amount_phases].phase_delay = pwm1_period() - 1 - pin1_data->pwm.pwm_duty;
			phase_data->phase[phase_data->amount_phases].phase_active_pins_clear_mask = 1 << pin1;
			phase_data->amount_phases++;
		}
		else
			phase_data->phase[phase_data->amount_phases - 1].phase_active_pins_clear_mask |= 1 << pin1;
	}

	if(phase_data->amount_phases < 2)
		phase_data->amount_phases = 0;

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

attr_inline void pin_arm_counter(int pin, bool enable)
{
	gpio_pin_intr_state_set(pin, enable ? GPIO_PIN_INTR_NEGEDGE : GPIO_PIN_INTR_DISABLE);
}

iram static void pc_int_isr(void *arg)
{
	io_config_pin_entry_t *pin_config;
	gpio_data_pin_t *gpio_pin_data;
	int pin;
	uint32_t pin_status;

	ets_isr_mask(1 << ETS_GPIO_INUM);
	pin_status = gpio_reg_read(GPIO_STATUS_ADDRESS);
	gpio_reg_write(GPIO_STATUS_W1TC_ADDRESS, pin_status);

	stat_pc_counts++;

	for(pin = 0; pin < io_gpio_pin_size; pin++)
	{
		if(!(pin_status & (1 << pin)))
			continue;

		pin_config = &io_config[0][pin];

		if(pin_config->llmode != io_pin_ll_counter)
			continue;

		gpio_pin_data = &gpio_data[pin];

		if(pin_config->speed != 0)
		{
			pin_arm_counter(pin, false);
			gpio_pin_data->counter.debounce = pin_config->speed;
		}

		dispatch_post_task(1, task_alert_pin_changed, 0);

		if(pin_config->mode != io_pin_rotary_encoder)
		{
			gpio_pin_data->counter.counter++;
			continue;
		}

		unsigned int partner_pin = pin_config->shared.renc.partner;

		if(partner_pin >= io_gpio_pin_size)
			continue;

		gpio_data_pin_t *gpio_partner_pin_data = &gpio_data[partner_pin];
		gpio_pin_data->counter.timeout = io_gpio_rotary_encoder_timeout;

		if(gpio_partner_pin_data->counter.timeout == 0)
			continue;

		if((pin_config->shared.renc.pin_type == io_renc_1a) || (pin_config->shared.renc.pin_type == io_renc_2a))
		{
			gpio_pin_data->counter.counter++;
			gpio_partner_pin_data->counter.counter++;
		}
		else
		{
			gpio_pin_data->counter.counter--;
			gpio_partner_pin_data->counter.counter--;
		}

		gpio_pin_data->counter.timeout = 0;
		gpio_partner_pin_data->counter.timeout = 0;
	}

	ets_isr_unmask(1 << ETS_GPIO_INUM);
	return;
}

io_error_t io_gpio_init(const struct io_info_entry_T *info)
{
	unsigned int entry;
	bool cpu_high_speed = config_flags_match(flag_cpu_high_speed);

	pdm_enable(false);

	for(entry = 0; entry < pwm_table_delay_size; entry++)
		if(cpu_high_speed)
			pwm_delay_entry[entry].delay = pwm_delay_entry[entry].delay_cpu_high_speed;
		else
			pwm_delay_entry[entry].delay = pwm_delay_entry[entry].delay_cpu_low_speed;

	io_gpio_pwm1_width_set(16, /*load*/true, /*save*/false);

	pwm_current_phase_set = 0;
	io_gpio_flags.pwm_reset_phase_set = 0;
	io_gpio_flags.pwm_next_phase_set = 0;

	pwm_phase[0].amount_phases = 0;
	pwm_phase[1].amount_phases = 0;

	gpio_init();
	pwm_isr_setup();

	ets_isr_attach(ETS_GPIO_INUM, pc_int_isr, 0);
	ets_isr_unmask(1 << ETS_GPIO_INUM);

	return(io_ok);
}

attr_pure unsigned int io_gpio_pin_max_value(const struct io_info_entry_T *info, io_data_pin_entry_t *data, const io_config_pin_entry_t *pin_config, unsigned int pin)
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

		case(io_pin_ll_uart):
		{
			value = 0xff;
			break;
		}

		case(io_pin_ll_counter):
		{
			value = ~0;
			break;
		}

		case(io_pin_ll_output_pwm1):
		{
			value = (1 << io_gpio_pwm1_width_get()) - 1;
			break;
		}

		case(io_pin_ll_output_pwm2):
		{
			value = pwm2_period() - 1;
		}

		default:
			break;
	}

	return(value);
}

iram void io_gpio_periodic_fast(int io, const struct io_info_entry_T *info, io_data_entry_t *data)
{
	int pin;

	for(pin = 0; pin < io_gpio_pin_size; pin++)
	{
		io_config_pin_entry_t *pin_config = &io_config[io][pin];

		if(pin_config->llmode != io_pin_ll_counter)
			continue;

		gpio_data_pin_t *gpio_pin_data = &gpio_data[pin];

		if((pin_config->mode == io_pin_rotary_encoder) && (gpio_pin_data->counter.timeout > 0))
			gpio_pin_data->counter.timeout--;

		if(gpio_pin_data->counter.debounce > 0)
		{
			if(gpio_pin_data->counter.debounce >= ms_per_fast_tick)
				gpio_pin_data->counter.debounce -= ms_per_fast_tick;
			else
			{
				gpio_pin_data->counter.debounce = 0;
				pin_arm_counter(pin, true);
			}
		}
	}
}

io_error_t io_gpio_init_pin_mode(string_t *error_message, const struct io_info_entry_T *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin)
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

	if(!(gpio_info->flags & gi_valid))
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

	pin_arm_counter(pin, false);

	gpio_func_select(pin, io_gpio_func_gpio);
	gpio_pin_intr_state_set(pin, GPIO_PIN_INTR_DISABLE);

	gpio_pin_data = &gpio_data[pin];

	switch(pin_config->llmode)
	{
		case(io_pin_ll_input_digital):
		case(io_pin_ll_counter):
		{
			gpio_direction(pin, false);
			gpio_enable_open_drain(pin, false);
			gpio_enable_pdm(pin, false);
			gpio_enable_pullup(pin, (pin_config->flags & io_flag_pullup));

			if(pin_config->llmode == io_pin_ll_counter)
			{
				gpio_pin_data->counter.counter = 0;
				gpio_pin_data->counter.debounce = 0;

				pin_arm_counter(pin, true);
			}

			break;
		}

		case(io_pin_ll_output_digital):
		{
			gpio_direction(pin, true);
			gpio_enable_open_drain(pin, false);
			gpio_enable_pdm(pin, false);
			break;
		}

		case(io_pin_ll_output_pwm1):
		{
			gpio_direction(pin, true);
			gpio_enable_open_drain(pin, false);
			gpio_enable_pdm(pin, false);
			gpio_pin_data->pwm.pwm_duty = pin_config->flags & io_flag_invert ? pwm1_period() - 1 : 0;
			gpio_set(pin, false);
			pwm_go();

			break;
		}

		case(io_pin_ll_output_pwm2):
		{
			gpio_direction(pin, true);
			gpio_enable_open_drain(pin, false);
			gpio_set(pin, false);
			gpio_enable_pdm(pin, true);
			gpio_pin_data->pwm.pwm_duty = pin_config->flags & io_flag_invert ? pwm2_period() - 1 : 0;
			pdm_enable(true);

			break;
		}

		case(io_pin_ll_i2c):
		{
			gpio_direction(pin, false);
			gpio_enable_pullup(pin, 0);
			gpio_direction(pin, true);
			gpio_enable_open_drain(pin, true);
			gpio_enable_pdm(pin, false);
			gpio_set(pin, true);

			break;
		}

		case(io_pin_ll_uart):
		{
			uart_direction_t direction;
			bool enable;

			if(gpio_info->uart_pin == io_uart_pin_none)
			{
				if(error_message)
					string_format(error_message, "gpio pin %d cannot be used for uart\n", pin);
				return(io_error);
			}

			if((pin_config->mode == io_pin_ledpixel) && (gpio_info->uart_pin != io_uart_pin_tx))
			{
				if(error_message)
					string_format(error_message, "gpio pin %d (uart rx) cannot be used for ledpixel mode\n", pin);
				return(io_error);
			}

			direction = gpio_info->uart_pin == io_uart_pin_rx ? uart_dir_rx : uart_dir_tx;
			enable = !!(pin_config->flags & io_flag_invert);

			gpio_func_select(pin, io_gpio_func_uart);
			gpio_enable_pullup(pin, pin_config->flags & io_flag_pullup);
			uart_invert(gpio_info->uart_instance, direction, enable);

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

io_error_t io_gpio_get_pin_info(string_t *dst, const struct io_info_entry_T *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin)
{
	gpio_data_pin_t *gpio_pin_data;

	if((pin < 0) || (pin >= io_gpio_pin_size))
		return(io_error);

	gpio_pin_data = &gpio_data[pin];

	if(!(gpio_info_table[pin].flags & gi_valid))
		string_append(dst, "unusable i/o pin");
	else
	{
		switch(pin_config->llmode)
		{
			case(io_pin_ll_counter):
			{
				string_format(dst, "current state: %s, debounce delay: %u/%u",
						onoff(gpio_get(pin)), gpio_pin_data->counter.debounce, gpio_pin_data->counter.timeout);

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
				unsigned int duty, dutycycle, dutypct, dutypctfraction, frequency;

				dutycycle = pwm1_period() - 1;
				duty = gpio_pin_data->pwm.pwm_duty;

				if(pin_config->flags & io_flag_invert)
					duty = dutycycle - duty;

				dutypct = duty * 100 / dutycycle;
				dutypctfraction = duty * 10000 / dutycycle;
				dutypctfraction -= dutypct * 100;

				frequency = 5000000 / pwm1_period();

				if(!pwm_isr_enabled())
					frequency = 0;

				string_format(dst, "frequency: %u Hz, duty: %u (%u.%02u %%), state: %s",
						frequency, duty, dutypct, dutypctfraction, onoff(gpio_get(pin)));

				break;
			}

			case(io_pin_ll_output_pwm2):
			{
				unsigned int duty, dutycycle, dutypct, dutypctfraction, frequency;
				unsigned int period, target, prescale;

				dutycycle = pwm2_period() - 1;
				duty = gpio_pin_data->pwm.pwm_duty;

				if(pin_config->flags & io_flag_invert)
					duty = dutycycle - duty;

				dutypct = duty * 100 / dutycycle;
				dutypctfraction = duty * 10000 / dutycycle;
				dutypctfraction -= dutypct * 100;

				target = pdm_get_target();
				prescale = pdm_get_prescale();

				period = 0;
				frequency = 0;

				if(target > 0)
				{
					if(target <= 128)
						period = (prescale + 1) * 256 / target;
					else
						period = ((prescale + 1) * 256) / (256 - target);
				}

				if(period != 0)
					frequency = 80000000 / period;

				string_format(dst, "frequency: %u Hz, duty: %u (%u.%02u %%) %%, prescale: %u, target: %u, state: %s",
						frequency, duty, dutypct, dutypctfraction, prescale, target, onoff(gpio_get(pin)));

				break;
			}

			case(io_pin_ll_uart):
			{
				unsigned int uart = gpio_info_table[pin].uart_instance;

				if((uart != 0) && (uart != 1))
					string_append(dst, "<invalid uart>");
				else
				{
					string_format(dst, "uart %u, pin: ", uart);

					switch(gpio_info_table[pin].uart_pin)
					{
						case(io_uart_pin_tx):
						{
							bool			enabled;
							unsigned int	character;

							uart_is_autofill(uart, &enabled, &character);
							string_format(dst, "tx, autofill: %s, character: 0x%02x", yesno(enabled), character);

							break;
						}

						case(io_uart_pin_rx):
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

					string_format(dst, ", inverted: %s", yesno(pin_config->flags & io_flag_invert));
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

io_error_t io_gpio_read_pin(string_t *error_message, const struct io_info_entry_T *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin, unsigned int *value)
{
	gpio_data_pin_t *gpio_pin_data;

	if(!(gpio_info_table[pin].flags & gi_valid))
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
		{
			*value = gpio_pin_data->pwm.pwm_duty;

			if(pin_config->flags & io_flag_invert)
				*value = pwm1_period() - 1 - *value;

			break;
		}

		case(io_pin_ll_output_pwm2):
		{
			*value = gpio_pin_data->pwm.pwm_duty;

			if(pin_config->flags & io_flag_invert)
				*value = pwm2_period() - 1 - *value;

			break;
		}

		case(io_pin_ll_uart):
		{
			bool			enabled;
			unsigned int	character;

			switch(gpio_info_table[pin].uart_pin)
			{
				case(io_uart_pin_none):
				{
					if(error_message)
						string_format(error_message, "cannot uart read from gpio %d\n", pin);
					return(io_error);
				}

				case(io_uart_pin_tx):
				{
					uart_is_autofill(gpio_info_table[pin].uart_instance, &enabled, &character);
					*value = !!enabled;
					break;
				}

				case(io_uart_pin_rx):
				{
					*value = 0;
					break;
				}
			}

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

io_error_t io_gpio_write_pin(string_t *error_message, const struct io_info_entry_T *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin, unsigned int value)
{
	gpio_data_pin_t *gpio_pin_data;

	if(!(gpio_info_table[pin].flags & gi_valid))
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
			if(value >= pwm1_period())
				value = pwm1_period() - 1;

			if(pin_config->flags & io_flag_invert)
				value = pwm1_period() - 1 - value;

			if(gpio_pin_data->pwm.pwm_duty != value)
			{
				gpio_pin_data->pwm.pwm_duty = value;
				pwm_go();
			}

			break;
		}

		case(io_pin_ll_output_pwm2):
		{
			unsigned int prescale, target;

			if(value >= pwm2_period())
				value = pwm2_period() - 1;

			if(pin_config->flags & io_flag_invert)
				value = pwm2_period() - 1 - value;

			gpio_pin_data->pwm.pwm_duty = value;

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
			int uart = gpio_info_table[pin].uart_instance;

			if((uart < 0) || (uart > 1))
			{
				if(error_message)
					string_format(error_message, "cannot uart write to gpio %d\n", pin);
				return(io_error);
			}

			if(gpio_info_table[pin].uart_pin == io_uart_pin_tx)
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
			else
			{
				if(error_message)
					string_format(error_message, "cannot uart write to gpio %d\n", pin);
				return(io_error);
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

attr_const int io_gpio_get_uart_from_pin(unsigned int pin)
{
	if(pin >= max_pins_per_io)
		return(-1);

	if(gpio_info_table[pin].uart_pin == io_uart_pin_none)
		return(-1);

	return(gpio_info_table[pin].uart_instance);
}
