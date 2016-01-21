#include "gpios.h"

#include "application-parameters.h"
#include "util.h"
#include "config.h"
#include "i2c.h"

#include <user_interface.h>
#include <osapi.h>
#include <gpio.h>
#include <pwm.h>
#include <ets_sys.h>

#include <stdlib.h>

typedef enum
{
	rtcgpio_input,
	rtcgpio_output
} rtcgpio_setup_t;

_Static_assert(sizeof(rtcgpio_setup_t) == 4, "sizeof(rtcgpio_setup_t) != 4");

typedef struct
{
	const	gpio_id_t		id;
	const	char			*name;
	const	unsigned int	index;
	const struct
	{
		unsigned int rtc_gpio;
	} flags;

	const	uint32_t	io_mux;
	const	uint32_t	io_func;

	struct
	{
		unsigned int count;
		unsigned int debounce;
	} counter;

	struct
	{
		unsigned int delay;
	} timer;

	struct
	{
		unsigned int		channel;
		unsigned int		min_duty;
		unsigned int		max_duty;
		unsigned int		delay_current;
		unsigned int		delay_top;
		gpio_direction_t	direction;
	} pwm;
} gpio_t;

typedef struct
{
	gpio_mode_t		mode;
	const char		*name;
	void			(*init_fn)(gpio_t *);
} gpio_mode_trait_t;

static void gpio_init_disabled(gpio_t *);
static void gpio_init_input(gpio_t *);
static void gpio_init_counter(gpio_t *);
static void gpio_init_output(gpio_t *);
static void gpio_init_timer(gpio_t *);
static void gpio_init_pwm(gpio_t *);
static void gpio_init_i2c(gpio_t *);

static gpio_t *find_gpio(gpio_id_t);
static void gpio_config_init(gpio_config_entry_t *);

static struct
{
	unsigned int pwm_subsystem_active:1;
	unsigned int counter_triggered:1;
} gpio_flags;

static gpio_mode_trait_t gpio_mode_trait[gpio_mode_size] =
{
	{ gpio_disabled,	"disabled",		gpio_init_disabled },
	{ gpio_input,		"input",		gpio_init_input },
	{ gpio_counter,		"counter",		gpio_init_counter },
	{ gpio_output,		"output",		gpio_init_output },
	{ gpio_timer,		"timer",		gpio_init_timer },
	{ gpio_pwm,			"pwm",			gpio_init_pwm },
	{ gpio_i2c,			"i2c",			gpio_init_i2c },
};

static gpio_t gpios[gpio_size] =
{
	{
		.id = gpio_0,
		.name = "gpio0",
		.index = 0,
		.flags = {
			.rtc_gpio = 0,
		},
		.io_mux = PERIPHS_IO_MUX_GPIO0_U,
		.io_func = FUNC_GPIO0,
	},
	{
		.id = gpio_1,
		.name = "gpio1",
		.index = 1,
		.flags = {
			.rtc_gpio = 0,
		},
		.io_mux = PERIPHS_IO_MUX_U0TXD_U,
		.io_func = FUNC_GPIO1
	},
	{
		.id = gpio_2,
		.name = "gpio2",
		.index = 2,
		.flags = {
			.rtc_gpio = 0,
		},
		.io_mux = PERIPHS_IO_MUX_GPIO2_U,
		.io_func = FUNC_GPIO2,
	},
	{
		.id = gpio_3,
		.name = "gpio3",
		.index = 3,
		.flags = {
			.rtc_gpio = 0,
		},
		.io_mux = PERIPHS_IO_MUX_U0RXD_U,
		.io_func = FUNC_GPIO3,
	},
	{
		.id = gpio_4,
		.name = "gpio4",
		.index = 4,
		.flags = {
			.rtc_gpio = 0,
		},
		.io_mux = PERIPHS_IO_MUX_GPIO4_U,
		.io_func = FUNC_GPIO4,
	},
	{
		.id = gpio_5,
		.name = "gpio5",
		.index = 5,
		.flags = {
			.rtc_gpio = 0,
		},
		.io_mux = PERIPHS_IO_MUX_GPIO5_U,
		.io_func = FUNC_GPIO5,
	},
	{
		.id = gpio_12,
		.name = "gpio12",
		.index = 12,
		.flags = {
			.rtc_gpio = 0,
		},
		.io_mux = PERIPHS_IO_MUX_MTDI_U,
		.io_func = FUNC_GPIO12,
	},
	{
		.id = gpio_13,
		.name = "gpio13",
		.index = 13,
		.flags = {
			.rtc_gpio = 0,
		},
		.io_mux = PERIPHS_IO_MUX_MTCK_U,
		.io_func = FUNC_GPIO13,
	},
	{
		.id = gpio_14,
		.name = "gpio14",
		.index = 14,
		.flags = {
			.rtc_gpio = 0,
		},
		.io_mux = PERIPHS_IO_MUX_MTMS_U,
		.io_func = FUNC_GPIO14,
	},
	{
		.id = gpio_15,
		.name = "gpio15",
		.index = 15,
		.flags = {
			.rtc_gpio = 0,
		},
		.io_mux = PERIPHS_IO_MUX_MTDO_U,
		.io_func = FUNC_GPIO15,
	},
	{
		.id = gpio_16,
		.name = "gpio16",
		.index = 16,
		.flags = {
			.rtc_gpio = 1,
		}
	}
};

static unsigned int analog_sampling_current = 0;
static unsigned int analog_sampling_total = 0;
static unsigned int analog_sampling_value = 0;

irom static gpio_config_entry_t *get_config(const gpio_t *gpio)
{
	return(&config->gpios.entry[gpio->id]);
}

iram static void pc_int_handler(uint32_t pc, void *arg)
{
	gpio_t *gpio;
	gpio_config_entry_t *cfg;
	unsigned int current;

	for(current = 0; current < gpio_size; current++)
	{
		gpio = &gpios[current];

		if(pc & (1 << gpio->index))
		{
			cfg = get_config(gpio);

			gpio->counter.count++;
			gpio->counter.debounce = cfg->counter.debounce;
		}
	}

	gpio_intr_ack(pc);

	gpio_flags.counter_triggered = true;
}

irom static void select_pin_function(const gpio_t *gpio)
{
	if(gpio->flags.rtc_gpio)
		return;

	pin_func_select(gpio->io_mux, gpio->io_func);
	PIN_PULLUP_DIS(gpio->io_mux);
}

irom void gpios_init(void)
{
	unsigned int current;
	unsigned int pwmchannel;
	gpio_t *gpio;
	gpio_config_entry_t *cfg;
	uint32_t pwm_io_info[gpio_pwm_size][3];
	uint32_t pwm_duty_init[gpio_pwm_size];
	uint32_t state_change_mask;
	int sda, scl;

	sda = scl = -1;

	gpio_init();

	state_change_mask = 0;

	for(current = 0, pwmchannel = 0; current < gpio_size; current++)
	{
		gpio = &gpios[current];
		cfg = get_config(gpio);

		if(cfg->mode != gpio_disabled)
			select_pin_function(gpio);

		if(cfg->mode == gpio_counter)
			state_change_mask |= (1 << gpio->index);

		if((cfg->mode == gpio_pwm) && (pwmchannel < gpio_pwm_size))
		{
			gpio->pwm.channel = pwmchannel;
			pwm_io_info[pwmchannel][0] = gpio->io_mux;
			pwm_io_info[pwmchannel][1] = gpio->io_func;
			pwm_io_info[pwmchannel][2] = gpio->index;
			pwm_duty_init[pwmchannel] = cfg->pwm.min_duty;
			pwmchannel++;
		}

		if(cfg->mode == gpio_i2c)
		{
			if(cfg->i2c.pin == gpio_i2c_sda)
				sda = gpio->index;

			if(cfg->i2c.pin == gpio_i2c_scl)
				scl = gpio->index;
		}
	}

	if(state_change_mask != 0)
		gpio_intr_handler_register(pc_int_handler, 0);

	if(pwmchannel > 0)
	{
		pwm_init(3000, pwm_duty_init, pwmchannel, pwm_io_info);
		gpio_flags.pwm_subsystem_active = true;
	}

	for(current = 0; current < gpio_size; current++)
		gpio_mode_trait[config->gpios.entry[current].mode].init_fn(&gpios[current]);

	if((sda >= 0) && (scl >= 0))
		i2c_init(sda, scl, config->i2c_delay);

	gpio_flags.counter_triggered = false;
}

irom static void gpio_config_init(gpio_config_entry_t *gpio)
{
	gpio->mode = gpio_disabled;
	gpio->counter.debounce = 100;
	gpio->counter.reset_on_get = false;
	gpio->output.startup_state = false;
	gpio->timer.direction = gpio_up;
	gpio->timer.delay = 0;
	gpio->timer.repeat = false;
	gpio->timer.autotrigger = false;
	gpio->pwm.min_duty = 0;
	gpio->pwm.max_duty = 0;
	gpio->pwm.delay = 0;
	gpio->i2c.pin = gpio_i2c_sda;
}

irom void gpios_config_init(gpio_config_t *cfg_gpios)
{
	int current;

	for(current = 0; current < gpio_size; current++)
		gpio_config_init(&cfg_gpios->entry[current]);
}

irom static void setclear_perireg(uint32_t reg, uint32_t clear, uint32_t set)
{
	uint32_t tmp;

	tmp = READ_PERI_REG(reg);
	tmp &= (uint32_t)~clear;
	tmp |= set;
    WRITE_PERI_REG(reg, tmp);
}

irom static void rtcgpio_config(rtcgpio_setup_t io)
{
	setclear_perireg(PAD_XPD_DCDC_CONF, 0x43, 0x01);
	setclear_perireg(RTC_GPIO_CONF, 0x01, 0x00);
	setclear_perireg(RTC_GPIO_ENABLE, 0x01, (io == rtcgpio_output) ? 0x01 : 0x00);
}

irom static void rtcgpio_output_set(bool_t value)
{
	setclear_perireg(RTC_GPIO_OUT, 0x01, value ? 0x01 : 0x00);
}

irom static bool rtcgpio_input_get(void)
{
	return(!!(READ_PERI_REG(RTC_GPIO_IN_DATA) & 0x01));
}

irom static void set_output(const gpio_t *gpio, bool_t onoff)
{
	if(gpio->flags.rtc_gpio)
		rtcgpio_output_set(onoff);
	else
		gpio_output_set(onoff ? (1 << gpio->index) : 0x00,
						!onoff ? (1 << gpio->index) : 0x00,
						0x00, 0x00);
}

irom static bool_t get_input(const gpio_t *gpio)
{
	if(gpio->flags.rtc_gpio)
		return(rtcgpio_input_get());

	return(!!(gpio_input_get() & (1 << gpio->index)));
}

irom static inline void arm_counter(const gpio_t *gpio)
{
	// no use in specifying POSEDGE or NEGEDGE here (bummer),
	// they act exactly like ANYEDGE, I assume that's an SDK bug

	gpio_pin_intr_state_set(gpio->index, GPIO_PIN_INTR_ANYEDGE);
}

irom void gpios_periodic(void)
{
	gpio_t *gpio;
	const gpio_config_entry_t *cfg;
	unsigned int current;
	unsigned int duty;
	bool_t pwm_changed = false;

	current = system_adc_read();

	if(current == 1023)
		current = 1024;

	analog_sampling_total += current;

	if(++analog_sampling_current >= 256)
	{
		current = analog_sampling_total / 4;

		if(current > 65535)
			current = 65535;

		if(current < 256)
			current = 0;

		analog_sampling_current = 0;
		analog_sampling_total = 0;
		analog_sampling_value = current;
	}

	for(current = 0; current < gpio_size; current++)
	{
		gpio = &gpios[current];
		cfg = get_config(gpio);

		if((cfg->mode == gpio_counter) && (gpio->counter.debounce != 0))
		{
			if(gpio->counter.debounce >= 10)
				gpio->counter.debounce -= 10; // 10 ms per tick
			else
				gpio->counter.debounce = 0;

			if(gpio->counter.debounce == 0)
				arm_counter(gpio);
		}

		if((cfg->mode == gpio_timer) && (gpio->timer.delay > 0))
		{
			if(gpio->timer.delay >= 10)
				gpio->timer.delay -= 10; // 10 ms per tick
			else
				gpio->timer.delay = 0;

			if(gpio->timer.delay == 0)
			{
				set_output(gpio, !get_input(gpio));

				if(cfg->timer.repeat)
					gpio->timer.delay = cfg->timer.delay;
			}
		}

		if((cfg->mode == gpio_pwm) && (gpio->pwm.delay_top > 0))
		{
			if(++gpio->pwm.delay_current > gpio->pwm.delay_top)
			{
				gpio->pwm.delay_current = 0;

				duty = pwm_get_duty(gpio->pwm.channel);

				if(gpio->pwm.direction == gpio_up)
				{
					if(duty < gpio->pwm.min_duty)
						duty = gpio->pwm.min_duty;

					if(duty < 16)
						duty = 16;

					duty *= 115;
					duty /= 100;

					if(duty >= gpio->pwm.max_duty)
					{
						duty = gpio->pwm.max_duty;
						gpio->pwm.direction = gpio_down;
					}
				}
				else
				{
					if(duty > gpio->pwm.max_duty)
						duty = gpio->pwm.max_duty;

					duty *= 100;
					duty /= 115;

					if(duty <= gpio->pwm.min_duty)
					{
						duty = gpio->pwm.min_duty;
						gpio->pwm.direction = gpio_up;
					}

					if(duty < 16)
					{
						duty = 16;
						gpio->pwm.direction = gpio_up;
					}
				}

				pwm_changed = true;
				pwm_set_duty(duty, gpio->pwm.channel);
			}
		}
	}

	if(pwm_changed)
		pwm_start();

	if(gpio_flags.counter_triggered)
	{
		gpio_flags.counter_triggered = false;

		if(config->stat_trigger_gpio >= 0)
			gpios_trigger_output(config->stat_trigger_gpio);
	}
}

irom static void trigger_timer(gpio_t *gpio, bool_t onoff)
{
	const gpio_config_entry_t *cfg = get_config(gpio);

	if(onoff)
	{
		set_output(gpio, cfg->timer.direction == gpio_up ? 1 : 0);
		gpio->timer.delay = cfg->timer.delay;
	}
	else
	{
		set_output(gpio, cfg->timer.direction == gpio_up ? 0 : 1);
		gpio->timer.delay = 0;
	}
}

irom static gpio_t *find_gpio(gpio_id_t index)
{
	unsigned int current;
	gpio_t *gpio;

	for(current = 0; current < gpio_size; current++)
	{
		gpio = &gpios[current];

		if(gpio->index == index)
			return(gpio);
	}

	return(0);
}

irom static gpio_mode_t gpio_mode_from_string(const char *mode)
{
	const  gpio_mode_trait_t *entry;
	unsigned int current;

	for(current = 0; current < gpio_mode_size; current++)
	{
		entry = &gpio_mode_trait[current];

		if(!strcmp(mode, entry->name))
			return(entry->mode);
	}

	return(gpio_mode_error);
}

irom static gpio_i2c_t gpio_i2c_pin_from_string(const char *mode)
{
	if(!strcmp(mode, "sda"))
		return(gpio_i2c_sda);
	else if(!strcmp(mode, "scl"))
		return(gpio_i2c_scl);
	else
		return(gpio_i2c_error);
}

irom static void gpio_init_disabled(gpio_t *gpio)
{
}

irom static void gpio_init_input(gpio_t *gpio)
{
	if(gpio->flags.rtc_gpio)
		rtcgpio_config(rtcgpio_input);
	else
		gpio_output_set(0, 0, 0, 1 << gpio->index);
}

irom static void gpio_init_counter(gpio_t *gpio)
{
	gpio_output_set(0, 0, 0, 1 << gpio->index);
	arm_counter(gpio);
}

irom static void gpio_init_output(gpio_t *gpio)
{
	const gpio_config_entry_t *cfg = get_config(gpio);

	if(gpio->flags.rtc_gpio)
		rtcgpio_config(rtcgpio_output);
	else
		gpio_output_set(0, 0, 1 << gpio->index, 0);

	set_output(gpio, cfg->output.startup_state);
}

irom static void gpio_init_timer(gpio_t *gpio)
{
	const gpio_config_entry_t *cfg = get_config(gpio);

	gpio->timer.delay = 0;

	gpio_init_output(gpio);

	if(cfg->timer.direction == gpio_up)
		gpio_output_set(0, 1 << gpio->index, 0, 0);
	else
		gpio_output_set(1 << gpio->index, 0, 0, 0);

	if(cfg->timer.autotrigger)
		trigger_timer(gpio, true);
}

irom static void gpio_init_pwm(gpio_t *gpio)
{
	const gpio_config_entry_t *cfg = get_config(gpio);

	gpio->pwm.min_duty = cfg->pwm.min_duty;
	gpio->pwm.max_duty = cfg->pwm.max_duty;
	gpio->pwm.delay_top = cfg->pwm.delay;
	gpio->pwm.delay_current = 0;
	gpio->pwm.direction	= gpio_up;
}

irom static void gpio_init_i2c(gpio_t *gpio)
{
	uint32_t pin = GPIO_PIN_ADDR(GPIO_ID_PIN(gpio->index));

	/* set to open drain */
	GPIO_REG_WRITE(pin, GPIO_REG_READ(pin) | GPIO_PIN_PAD_DRIVER_SET(GPIO_PAD_DRIVER_ENABLE));

	gpio_output_set(1 << gpio->index, 0, 1 << gpio->index, 0);
}

typedef enum
{
	ds_id_gpio,
	ds_id_disabled,
	ds_id_input,
	ds_id_counter,
	ds_id_output,
	ds_id_timer,
	ds_id_pwm_inactive,
	ds_id_pwm_active,
	ds_id_pwm_duty_default,
	ds_id_pwm_duty_current,
	ds_id_i2c,
	ds_id_unknown,
	ds_id_header,
	ds_id_footer,
	ds_id_preline,
	ds_id_postline,

	ds_id_length,
	ds_id_invalid = ds_id_length
} dump_string_id_t;

typedef struct {
	struct
	{
		const char plain[ds_id_length][256];
		const char html[ds_id_length][256];
	} strings;
} dump_string_t;

static roflash const dump_string_t dump_strings =
{
	.strings =
	{
		.plain =
		{
			"> gpio: %u, name: %s, mode: ",
			"disabled",
			"input, state: %s",
			"counter, state: %s, counter: %u, debounce: %u/%u, reset on get: %s",
			"output, state: %s, startup: %s",
			"timer, direction: %s, delay: %u ms, repeat: %s, autotrigger: %s, active: %s, current state: %s",
			"pwm, inactive",
			"pwm, active, channel: %u, current frequency: %u Hz, current duty: %u",
			"\ndefault min duty: %u, max duty: %u, delay: %u",
			"\ncurrent min duty: %u, max duty: %u, delay: %u",
			"i2c, pin: %s",
			"unknown",
			"",
			"",
			"",
			"\n",
		},

		.html =
		{
			"<td>%u</td><td>%s</td>",
			"<td>disabled</td>",
			"<td>input</td><td>state: %s</td>",
			"<td>counter</td><td>state: %s</td><td>counter: %u</td><td>debounce: %u/%u</td><td>reset on get: %s</td>",
			"<td>output</td><td>state: %s</td><td>startup: %s</td>",
			"<td>timer</td><td>direction: %s</td><td>delay: %u ms</td><td> repeat: %s</td><td>autotrigger: %s</td><td>active: %s</td><td>current state: %s</td>",
			"<td>pwm</<td><td>inactive</td>",
			"<td>pwm</td><td>active</td><td>channel: %u</td><td>current frequency: %u Hz</td><td>current duty: %u</td>",
			"<td>default min duty: %u, max duty: %u, delay: %u</td>",
			"<td>current min duty: %u, max duty: %u, delay: %u</td>",
			"<td>i2c</td><td>pin %s</td>",
			"<td>unknown</td>",
			"<table border=\"1\"><tr><th>index</th><th>name</th><th>mode</th><th colspan=\"8\"></th></tr>",
			"</table>\n",
			"<tr>",
			"</trd>\n",
		}
	}
};

irom static unsigned int dump(const gpio_config_t *cfgs, const gpio_t *gpio_in, unsigned int size, char *str, bool html)
{
	unsigned int current;
	unsigned int length;
	const gpio_t *gpio;
	const gpio_config_entry_t *cfg;
	const char (*strings)[256];
	const char * str_in;

	str_in = str;

	if(html)
		strings = dump_strings.strings.html;
	else
		strings = dump_strings.strings.plain;

	length = snprintf_roflash(str, size, strings[ds_id_header]);
	size -= length;
	str += length;

	for(current = 0; current < gpio_size; current++)
	{
		gpio = &gpios[current];
		cfg = &cfgs->entry[current];

		if(!gpio_in || (gpio_in->id == gpio->id))
		{
			length = snprintf_roflash(str, size, strings[ds_id_preline], gpio->index, gpio->name);
			size -= length;
			str += length;

			length = snprintf_roflash(str, size, strings[ds_id_gpio], gpio->index, gpio->name);
			size -= length;
			str += length;
			length = 0;

			switch(cfg->mode)
			{
				case(gpio_disabled):
				{
					length = snprintf_roflash(str, size, strings[ds_id_disabled]);

					break;
				}

				case(gpio_input):
				{
					length = snprintf_roflash(str, size, strings[ds_id_input], onoff(get_input(gpio)));

					break;
				}

				case(gpio_counter):
				{
					length = snprintf_roflash(str, size, strings[ds_id_counter], onoff(get_input(gpio)), gpio->counter.count,
							cfg->counter.debounce, gpio->counter.debounce, onoff(cfg->counter.reset_on_get));

					break;
				}

				case(gpio_output):
				{
					length = snprintf_roflash(str, size, strings[ds_id_output], onoff(get_input(gpio)), onoff(cfg->output.startup_state));

					break;
				}

				case(gpio_timer):
				{
					length = snprintf_roflash(str, size, strings[ds_id_timer], cfg->timer.direction == gpio_up ? "up" : "down",
							cfg->timer.delay, onoff(cfg->timer.repeat), onoff(cfg->timer.autotrigger),
							onoff(gpio->timer.delay > 0), onoff(get_input(gpio)));
					break;
				}

				case(gpio_pwm):
				{
					if(gpio_flags.pwm_subsystem_active)
						snprintf_roflash(str, size, strings[ds_id_pwm_active], gpio->pwm.channel,
								1000000 / pwm_get_period(), pwm_get_duty(gpio->pwm.channel));
					else
						length = snprintf_roflash(str, size, strings[ds_id_pwm_inactive]);

					str += length;
					size -= length;

					length = snprintf_roflash(str, size, strings[ds_id_pwm_duty_default], cfg->pwm.min_duty,
							cfg->pwm.max_duty, cfg->pwm.delay);

					str += length;
					size -= length;

					length = snprintf_roflash(str, size, strings[ds_id_pwm_duty_current],
							gpio->pwm.min_duty, gpio->pwm.max_duty, gpio->pwm.delay_top);

					break;
				}

				case(gpio_i2c):
				{
					length = snprintf_roflash(str, size, strings[ds_id_i2c], cfg->i2c.pin == gpio_i2c_sda ? "sda" : "scl");

					break;
				}


				default:
				{
					length = snprintf(str, size, "unknown mode");
					break;
				}
			}

			str += length;
			size =- length;

			length = snprintf_roflash(str, size, strings[ds_id_postline]);
			str += length;
			size -= length;
		}
	}

	snprintf_roflash(str, size, strings[ds_id_footer]);
	str += length;

	return(str - str_in);
}

irom unsigned int gpios_dump_string(const gpio_config_t *cfgs, unsigned int size, char *string)
{
	return(dump(cfgs, 0, size, string, false));
}

irom unsigned int gpios_dump_html(unsigned int size, char *string)
{
	return(dump(&config->gpios, 0, size, string, true));
}

irom attr_pure gpio_mode_t gpios_mode(unsigned int gpio_name, bool plain_only)
{
	gpio_t *gpio;
	const gpio_config_entry_t *cfg;

	if(!(gpio = find_gpio(gpio_name)))
		return(gpio_mode_error);

	if(plain_only && gpio->flags.rtc_gpio)
		return(gpio_mode_error);

	cfg = get_config(gpio);

	return(cfg->mode);
}

irom bool gpios_trigger_output(unsigned int gpio_name)
{
	gpio_t *gpio;
	const gpio_config_entry_t *cfg;

	if(!(gpio = find_gpio(gpio_name)))
		return(false);

	cfg = get_config(gpio);

	switch(cfg->mode)
	{
		case(gpio_output):
		{
			set_output(gpio, true);
			break;
		}

		case(gpio_timer):
		{
			trigger_timer(gpio, true);
			break;
		}

		case(gpio_pwm):
		{
			pwm_set_duty(0xffff, gpio->pwm.channel);
			pwm_start();

			break;
		}

		default:
		{
			return(false);
		}
	}

	return(true);
}

irom bool gpios_set_wlan_trigger(unsigned int gpio_name)
{
	gpio_t *gpio;
	const gpio_config_entry_t *cfg;

	if(!(gpio = find_gpio(gpio_name)))
		return(false);

	cfg = get_config(gpio);

	if(cfg->mode != gpio_output)
		return(false);

	if(gpio->flags.rtc_gpio)
		return(false);

	wifi_status_led_install(gpio->index, gpio->io_mux, gpio->io_func);

	return(true);
}

irom app_action_t application_function_gpio_mode(application_parameters_t ap)
{
	gpio_mode_t mode;
	unsigned int gpio_index;
	gpio_t *gpio;
	gpio_config_entry_t *new_gpio_config;

	if(ap.nargs < 2)
	{
		dump(&config->gpios, 0, ap.size, ap.dst, false);
		return(app_action_normal);
	}

	gpio_index = string_to_int((*ap.args)[1]);

	if(!(gpio = find_gpio(gpio_index)))
	{
		snprintf(ap.dst, ap.size, "gpio-mode: invalid gpio %u\n", gpio_index);
		return(app_action_error);
	}

	if(ap.nargs < 3)
	{
		dump(&config->gpios, gpio, ap.size, ap.dst, false);
		return(app_action_normal);
	}

	if((mode = gpio_mode_from_string((*ap.args)[2])) == gpio_mode_error)
	{
		snprintf(ap.dst, ap.size, "gpio-mode: invalid mode %s\n", (*ap.args)[2]);
		return(app_action_error);
	}

	config_read_alt(tmpconfig);
	new_gpio_config = &tmpconfig->gpios.entry[gpio->id];

	switch(mode)
	{
		case(gpio_counter):
		{
			if(gpio->flags.rtc_gpio)
			{
				snprintf(ap.dst, ap.size, "%s", "gpio-mode: counter mode invalid for gpio 16\n");
				return(app_action_error);
			}

			if(ap.nargs != 5)
			{
				snprintf(ap.dst, ap.size, "gpio-mode(counter): <reset on get> <debounce ms>\n");
				return(app_action_error);
			}

			new_gpio_config->counter.reset_on_get = (uint8_t)!!string_to_int((*ap.args)[3]);
			new_gpio_config->counter.debounce = string_to_int((*ap.args)[4]);

			break;
		}

		case(gpio_output):
		{
			if(ap.nargs != 4)
			{
				snprintf(ap.dst, ap.size, "gpio-mode(output): <startup value>\n");
				return(app_action_error);
			}

			new_gpio_config->output.startup_state = (uint8_t)!!string_to_int((*ap.args)[3]);

			break;
		}

		case(gpio_timer):
		{
			gpio_direction_t direction;
			unsigned int delay;
			unsigned int repeat;
			unsigned int autotrigger;

			if(ap.nargs != 7)
			{
				snprintf(ap.dst, ap.size, "gpio-mode: timer direction:up/down delay:ms repeat:0/1 autotrigger:0/1\n");
				return(app_action_error);
			}

			if(!strcmp((*ap.args)[3], "up"))
				direction = gpio_up;
			else if(!strcmp((*ap.args)[3], "down"))
				direction = gpio_down;
			else
			{
				snprintf(ap.dst, ap.size, "gpio-mode(timer): direction invalid: %s\n", (*ap.args)[3]);
				return(app_action_error);
			}

			delay = string_to_int((*ap.args)[4]);
			repeat = string_to_int((*ap.args)[5]);
			autotrigger = string_to_int((*ap.args)[6]);

			if(delay < 10)
			{
				snprintf(ap.dst, ap.size, "gpio-mode(timer): delay too small: %d ms, >= 10 ms\n", delay);
				return(app_action_error);
			}

			new_gpio_config->timer.direction = direction;
			new_gpio_config->timer.delay = (uint32_t)delay;
			new_gpio_config->timer.repeat = (uint8_t)!!repeat;
			new_gpio_config->timer.autotrigger = (uint8_t)!!autotrigger;

			break;
		}

		case(gpio_pwm):
		{
			unsigned int min_duty;
			unsigned int max_duty;
			unsigned int delay;

			min_duty = 0;
			max_duty = 0;
			delay = 0;

			if(gpio->flags.rtc_gpio)
			{
				snprintf(ap.dst, ap.size, "%s", "gpio-mode: pwm mode not supported for this gpio\n");
				return(app_action_error);
			}

			if(ap.nargs > 3)
				min_duty = string_to_int((*ap.args)[3]);

			if(ap.nargs > 4)
				max_duty = string_to_int((*ap.args)[4]);

			if(ap.nargs > 5)
				delay = string_to_int((*ap.args)[5]);

			if(min_duty > 65535)
			{
				snprintf(ap.dst, ap.size, "gpio-mode(pwm): min_duty too large: %u > 65535\n", min_duty);
				return(app_action_error);
			}

			if(max_duty > 65535)
			{
				snprintf(ap.dst, ap.size, "gpio-mode(pwm): max_duty too large: %u > 65535\n", max_duty);
				return(app_action_error);
			}

			if(delay > 100)
			{
				snprintf(ap.dst, ap.size, "gpio-mode(pwm): delay too large: %u%% > 100%%\n", delay);
				return(app_action_error);
			}

			new_gpio_config->pwm.min_duty = (uint16_t)min_duty;
			new_gpio_config->pwm.max_duty = (uint16_t)max_duty;
			new_gpio_config->pwm.delay = (uint8_t)delay;

			break;
		}

		case(gpio_i2c):
		{
			gpio_i2c_t pin;

			if(gpio->flags.rtc_gpio)
			{
				snprintf(ap.dst, ap.size, "%s", "gpio-mode: i2c mode invalid for gpio 16\n");
				return(app_action_error);
			}

			if(ap.nargs != 4)
			{
				snprintf(ap.dst, ap.size, "gpio-mode(i2c): usage: i2c sda|scl\n");
				return(app_action_error);
			}

			if((pin = gpio_i2c_pin_from_string((*ap.args)[3])) == gpio_i2c_error)
			{
				snprintf(ap.dst, ap.size, "gpio-mode(i2c): usage: i2c sda|scl\n");
				return(app_action_error);
			}

			new_gpio_config->i2c.pin = pin;

			break;
		}

		default:
		{
		}
	}

	new_gpio_config->mode = mode;
	config_write_alt(tmpconfig);

	dump(&tmpconfig->gpios, gpio, ap.size, ap.dst, false);
	strlcat(ap.dst, "! gpio-mode: restart to activate new mode\n", ap.size);

	return(app_action_normal);
}

irom app_action_t application_function_gpio_get(application_parameters_t ap)
{
	unsigned int gpio_index;
	gpio_t *gpio;
	const gpio_config_entry_t *cfg;

	if(ap.nargs < 2)
	{
		snprintf(ap.dst, ap.size, "gpio-get: too little arguments: %u\n", ap.nargs - 1);
		return(app_action_error);
	}

	gpio_index = string_to_int((*ap.args)[1]);

	if(!(gpio = find_gpio(gpio_index)))
	{
		snprintf(ap.dst, ap.size, "gpio-get: invalid gpio %u\n", gpio_index);
		return(app_action_error);
	}

	cfg = get_config(gpio);

	switch(cfg->mode)
	{
		case(gpio_disabled):
		{
			snprintf(ap.dst, ap.size, "gpio-get: gpio %s is disabled\n", gpio->name);
			return(app_action_error);
		}

		case(gpio_input):
		{
			snprintf(ap.dst, ap.size, "gpio-get: gpio %s is %s\n", gpio->name, onoff(get_input(gpio)));
			return(app_action_normal);
		}

		case(gpio_counter):
		{
			snprintf(ap.dst, ap.size, "gpio-get: gpio %s is %u (state: %s)\n",
					gpio->name, gpio->counter.count, onoff(get_input(gpio)));

			if(cfg->counter.reset_on_get)
				gpio->counter.count = 0;

			gpio->counter.debounce = 0;

			return(app_action_normal);
		}

		case(gpio_output):
		case(gpio_timer):
		{
			snprintf(ap.dst, ap.size, "gpio-get: gpio %s is output\n", gpio->name);
			return(app_action_error);
		}

		case(gpio_pwm):
		{
			dump(&config->gpios, gpio, ap.size, ap.dst, false);
			return(app_action_normal);
		}

		case(gpio_i2c):
		{
			snprintf(ap.dst, ap.size, "gpio-get: gpio %s is reserved for i2c\n", gpio->name);
			return(app_action_error);
		}

		default:
		{
		}
	}

	snprintf(ap.dst, ap.size, "gpio-get: invalid mode %u\n", cfg->mode);
	return(app_action_error);
}

irom app_action_t application_function_gpio_set(application_parameters_t ap)
{
	unsigned int gpio_index;
	gpio_t *gpio;
	const gpio_config_entry_t *cfg;

	gpio_index = string_to_int((*ap.args)[1]);

	if(!(gpio = find_gpio(gpio_index)))
	{
		snprintf(ap.dst, ap.size, "gpio-set: invalid gpio %u\n", gpio_index);
		return(app_action_error);
	}

	cfg = get_config(gpio);

	switch(cfg->mode)
	{
		case(gpio_disabled):
		{
			snprintf(ap.dst, ap.size, "gpio-set: gpio %s is disabled\n", gpio->name);
			return(app_action_error);
		}

		case(gpio_input):
		{
			snprintf(ap.dst, ap.size, "gpio-set: gpio %s is input\n", gpio->name);
			return(app_action_error);
		}

		case(gpio_counter):
		{
			if(ap.nargs < 3)
				gpio->counter.count = 0;
			else
				gpio->counter.count = string_to_int((*ap.args)[2]);

			break;
		}

		case(gpio_output):
		{
			if(ap.nargs < 3)
			{
				snprintf(ap.dst, ap.size, "gpio-set: missing arguments\n");
				return(app_action_error);
			}

			set_output(gpio, !!string_to_int((*ap.args)[2]));

			break;
		}

		case(gpio_timer):
		{
			if(ap.nargs == 3)
				trigger_timer(gpio, !!string_to_int((*ap.args)[2]));
			else
				trigger_timer(gpio, !gpio->timer.delay);

			break;
		}

		case(gpio_pwm):
		{
			unsigned int min_duty;
			unsigned int max_duty;
			unsigned int delay;

			min_duty = 0;
			max_duty = 0;
			delay = 0;

			if(ap.nargs > 2)
				min_duty = string_to_int((*ap.args)[2]);

			if(ap.nargs > 3)
				max_duty = string_to_int((*ap.args)[3]);

			if(ap.nargs > 4)
				delay = string_to_int((*ap.args)[4]);

			if(min_duty > 65535)
			{
				snprintf(ap.dst, ap.size, "gpio-set(pwm): min_duty too large: %u > 65535\n", min_duty);
				return(app_action_error);
			}

			if(max_duty > 65535)
			{
				snprintf(ap.dst, ap.size, "gpio-set(pwm): max_duty too large: %u > 65535\n", max_duty);
				return(app_action_error);
			}

			if(delay > 100)
			{
				snprintf(ap.dst, ap.size, "gpio-set: gpio %s, delay %u%% > 100%%\n", gpio->name, delay);
				return(app_action_error);
			}

			gpio->pwm.min_duty = min_duty;
			gpio->pwm.max_duty = max_duty;
			gpio->pwm.delay_top = delay;
			gpio->pwm.direction = gpio_up;

			pwm_set_duty(min_duty, gpio->pwm.channel);
			pwm_start();

			break;
		}

		case(gpio_i2c):
		{
			snprintf(ap.dst, ap.size, "gpio-set: gpio %s is reserved for i2c\n", gpio->name);
			return(app_action_error);
		}

		default:
		{
			snprintf(ap.dst, ap.size, "gpio-set: cannot set gpio %u\n", cfg->mode);
			return(app_action_error);
		}
	}

	dump(&config->gpios, gpio, ap.size, ap.dst, false);
	return(app_action_normal);
}

irom app_action_t application_function_gpio_dump(application_parameters_t ap)
{
	dump(&config->gpios, 0, ap.size, ap.dst, false);

	return(app_action_normal);
}

irom app_action_t application_function_analog_read(application_parameters_t ap)
{
	snprintf(ap.dst, ap.size, "analog-read: value: %u\n", analog_sampling_value);

	return(app_action_normal);
}
