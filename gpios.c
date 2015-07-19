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

typedef enum __attribute__ ((__packed__))
{
	rtcgpio_input,
	rtcgpio_output
} rtcgpio_setup_t;

_Static_assert(sizeof(rtcgpio_setup_t) == 1, "sizeof(rtcgpio_setup_t) != 1");

typedef struct
{
	const	gpio_id_t	id;
	const	char		*name;
	const	uint8_t		index;
	const struct
	{
		unsigned int rtc_gpio;
	} flags;

	const	uint32_t	io_mux;
	const	uint32_t	io_func;

	struct
	{
		uint32_t count;
		uint32_t debounce;
	} counter;

	struct
	{
		uint32_t delay;
	} bounce;

	struct
	{
		uint8_t	channel;
	} pwm;
} gpio_trait_t;

typedef struct
{
	gpio_mode_t		mode;
	const char		*name;
	void			(*init_fn)(gpio_trait_t *);
} gpio_mode_trait_t;

static void gpio_init_disabled(gpio_trait_t *);
static void gpio_init_input(gpio_trait_t *);
static void gpio_init_counter(gpio_trait_t *);
static void gpio_init_output(gpio_trait_t *);
static void gpio_init_bounce(gpio_trait_t *);
static void gpio_init_pwm(gpio_trait_t *);
static void gpio_init_i2c(gpio_trait_t *);

static gpio_trait_t *find_gpio(gpio_id_t);
static void gpio_config_init(gpio_t *gpio);

static struct
{
	unsigned int pwm_subsystem_active:1;
} gpio_flags;

static gpio_mode_trait_t gpio_mode_trait[gpio_mode_size] =
{
	{ gpio_disabled,	"disabled",		gpio_init_disabled },
	{ gpio_input,		"input",		gpio_init_input },
	{ gpio_counter,		"counter",		gpio_init_counter },
	{ gpio_output,		"output",		gpio_init_output },
	{ gpio_bounce,		"bounce",		gpio_init_bounce },
	{ gpio_pwm,			"pwm",			gpio_init_pwm },
	{ gpio_i2c,			"i2c",			gpio_init_i2c },
};

static gpio_trait_t gpio_traits[gpio_size] =
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
		.id = gpio_16,
		.name = "gpio16",
		.index = 16,
		.flags = {
			.rtc_gpio = 1,
		}
	}
};

irom static gpio_t *get_config(const gpio_trait_t *gpio)
{
	return(&config->gpios[gpio->id]);
}

iram static void pc_int_handler(uint32_t pc, void *arg)
{
	gpio_trait_t *gpio;
	gpio_t *cfg;
	uint8_t current;

	gpio_intr_ack(pc);

	for(current = 0; current < gpio_size; current++)
	{
		gpio = &gpio_traits[current];

		if(pc & (1 << gpio->index))
		{
			cfg = get_config(gpio);

			gpio->counter.count++;
			gpio->counter.debounce = cfg->counter.debounce;
		}
	}
}

irom static void select_pin_function(const gpio_trait_t *gpio)
{
	if(gpio->flags.rtc_gpio)
		return;

	pin_func_select(gpio->io_mux, gpio->io_func);
	PIN_PULLUP_DIS(gpio->io_mux);
}

irom void gpios_init(void)
{
	uint8_t current, pwmchannel;
	gpio_trait_t *gpio;
	gpio_t *cfg;
	uint32_t pwm_io_info[gpio_pwm_size][3];
	uint32_t pwm_duty_init[gpio_pwm_size];
	uint32_t state_change_mask;
	int8_t sda, scl;

	sda = scl = -1;

	gpio_init();

	state_change_mask = 0;

	for(current = 0, pwmchannel = 0; current < gpio_size; current++)
	{
		gpio = &gpio_traits[current];
		cfg = get_config(gpio);

		select_pin_function(gpio);

		if(cfg->mode == gpio_counter)
			state_change_mask |= (1 << gpio->index);

		if((cfg->mode == gpio_pwm) && (pwmchannel < gpio_pwm_size))
		{
			gpio->pwm.channel = pwmchannel;
			pwm_io_info[pwmchannel][0] = gpio->io_mux;
			pwm_io_info[pwmchannel][1] = gpio->io_func;
			pwm_io_info[pwmchannel][2] = gpio->index;
			pwm_duty_init[pwmchannel] = cfg->pwm.startup_duty;
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
		gpio_mode_trait[config->gpios[current].mode].init_fn(&gpio_traits[current]);

	if((sda > 0) && (scl > 0))
		i2c_init(sda, scl);
}

irom static void gpio_config_init(gpio_t *gpio)
{
	gpio->mode = gpio_disabled;
	gpio->counter.debounce = 100;
	gpio->counter.reset_on_get = false;
	gpio->output.startup_state = 0;
	gpio->bounce.direction = gpio_up;
	gpio->bounce.delay = 0;
	gpio->bounce.repeat = 0;
	gpio->bounce.autotrigger = 0;
	gpio->pwm.startup_duty = 0;
	gpio->i2c.pin = gpio_i2c_sda;
}

irom void gpios_config_init(gpio_t *gpios)
{
	int current;

	for(current = 0; current < gpio_size; current++)
		gpio_config_init(&gpios[current]);
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

irom static void set_output(const gpio_trait_t *gpio, bool_t onoff)
{
	if(gpio->flags.rtc_gpio)
		rtcgpio_output_set(onoff);
	else
		gpio_output_set(onoff ? (1 << gpio->index) : 0x00,
						!onoff ? (1 << gpio->index) : 0x00,
						0x00, 0x00);
}

irom static bool_t get_input(const gpio_trait_t *gpio)
{
	if(gpio->flags.rtc_gpio)
		return(rtcgpio_input_get());

	return(!!(gpio_input_get() & (1 << gpio->index)));
}

iram static inline void arm_counter(const gpio_trait_t *gpio)
{
	// no use in specifying POSEDGE or NEGEDGE here (bummer),
	// they act exactly like ANYEDGE, I assume that's an SDK bug

	gpio_pin_intr_state_set(gpio->index, GPIO_PIN_INTR_ANYEDGE);
}

iram void gpios_periodic(void)
{
	uint8_t current;
	gpio_trait_t *gpio;
	const gpio_t *cfg;

	for(current = 0; current < gpio_size; current++)
	{
		gpio = &gpio_traits[current];
		cfg = get_config(gpio);

		if((cfg->mode == gpio_counter) && (gpio->counter.debounce > 0))
		{
			if(gpio->counter.debounce >= 10)
				gpio->counter.debounce -= 10; // 10 ms per tick
			else
				gpio->counter.debounce = 0;

			if(gpio->counter.debounce == 0)
				arm_counter(gpio);
		}

		if((cfg->mode == gpio_bounce) && (gpio->bounce.delay > 0))
		{
			if(gpio->bounce.delay >= 10)
				gpio->bounce.delay -= 10; // 10 ms per tick
			else
				gpio->bounce.delay = 0;

			if(gpio->bounce.delay == 0)
			{
				set_output(gpio, !get_input(gpio));

				if(cfg->bounce.repeat)
					gpio->bounce.delay = cfg->bounce.delay;
			}
		}
	}
}

irom static void trigger_bounce(gpio_trait_t *gpio, bool_t onoff)
{
	const gpio_t *cfg = get_config(gpio);

	if(onoff)
	{
		set_output(gpio, cfg->bounce.direction == gpio_up ? 1 : 0);
		gpio->bounce.delay = cfg->bounce.delay;
	}
	else
	{
		set_output(gpio, cfg->bounce.direction == gpio_up ? 0 : 1);
		gpio->bounce.delay = 0;
	}
}

irom  static void trigger_pwm(const gpio_trait_t *gpio, uint32_t duty)
{
	pwm_set_duty(duty, gpio->pwm.channel);
	pwm_start();
}

irom static gpio_trait_t *find_gpio(gpio_id_t index)
{
	uint8_t current;
	gpio_trait_t *gpio;

	for(current = 0; current < gpio_size; current++)
	{
		gpio = &gpio_traits[current];

		if(gpio->index == index)
			return(gpio);
	}

	return(0);
}

irom static gpio_mode_t gpio_mode_from_string(const char *mode)
{
	const  gpio_mode_trait_t *entry;
	uint8_t current;

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

irom static void gpio_init_disabled(gpio_trait_t *gpio)
{
}

irom static void gpio_init_input(gpio_trait_t *gpio)
{
	if(gpio->flags.rtc_gpio)
		rtcgpio_config(rtcgpio_input);
	else
		gpio_output_set(0, 0, 0, 1 << gpio->index);
}

irom static void gpio_init_counter(gpio_trait_t *gpio)
{
	gpio_output_set(0, 0, 0, 1 << gpio->index);
	arm_counter(gpio);
}

irom static void gpio_init_output(gpio_trait_t *gpio)
{
	const gpio_t *cfg = get_config(gpio);

	if(gpio->flags.rtc_gpio)
		rtcgpio_config(rtcgpio_output);
	else
		gpio_output_set(0, 0, 1 << gpio->index, 0);

	set_output(gpio, cfg->output.startup_state);
}

irom static void gpio_init_bounce(gpio_trait_t *gpio)
{
	const gpio_t *cfg = get_config(gpio);

	gpio->bounce.delay = 0;

	gpio_init_output(gpio);

	if(cfg->bounce.direction == gpio_up)
		gpio_output_set(0, 1 << gpio->index, 0, 0);
	else
		gpio_output_set(1 << gpio->index, 0, 0, 0);

	if(cfg->bounce.autotrigger)
		trigger_bounce(gpio, 1);
}

irom static void gpio_init_pwm(gpio_trait_t *gpio)
{
}

irom static void gpio_init_i2c(gpio_trait_t *gpio)
{
	uint32_t pin = GPIO_PIN_ADDR(GPIO_ID_PIN(gpio->index));

	/* set to open drain */
	GPIO_REG_WRITE(pin, GPIO_REG_READ(pin) | GPIO_PIN_PAD_DRIVER_SET(GPIO_PAD_DRIVER_ENABLE));

	gpio_output_set(1 << gpio->index, 0, 1 << gpio->index, 0);
}

irom static void dump(const gpio_t *cfgs, const gpio_trait_t *gpio_in, uint16_t size, char *str)
{
	uint8_t current;
	uint16_t length;
	const gpio_trait_t *gpio;
	const gpio_t *cfg;

	for(current = 0; current < gpio_size; current++)
	{
		gpio = &gpio_traits[current];
		cfg = &cfgs[current];

		if(!gpio_in || (gpio_in->id == gpio->id))
		{
			length = snprintf(str, size, "> gpio: %u, name: %s, mode: ", gpio->index, gpio->name);
			size -= length;
			str += length;
			length = 0;

			switch(cfg->mode)
			{
				case(gpio_disabled):
				{
					length = snprintf(str, size, "disabled");
					break;
				}

				case(gpio_input):
				{
					length = snprintf(str, size, "input, state: %s", onoff(get_input(gpio)));
					break;
				}

				case(gpio_counter):
				{
					length = snprintf(str, size, "counter, state: %s, counter: %u, debounce: %u/%u, reset on get: %s",
							onoff(get_input(gpio)), gpio->counter.count,
							cfg->counter.debounce, gpio->counter.debounce,
							onoff(cfg->counter.reset_on_get));
					break;
				}

				case(gpio_output):
				{
					length = snprintf(str, size, "output, state: %s, startup: %s",
							onoff(get_input(gpio)), onoff(cfg->output.startup_state));
					break;
				}

				case(gpio_bounce):
				{
					length = snprintf(str, size, "bounce, direction: %s, delay: %u ms, repeat: %s, autotrigger: %s, active: %s, current state: %s",
							cfg->bounce.direction == gpio_up ? "up" : "down",
							cfg->bounce.delay, onoff(cfg->bounce.repeat),
							onoff(cfg->bounce.autotrigger), onoff(gpio->bounce.delay > 0),
							onoff(get_input(gpio)));
					break;
				}

				case(gpio_pwm):
				{
					if(gpio_flags.pwm_subsystem_active)
						length = snprintf(str, size, "pwm, frequency: %u Hz, startup duty: %u, current duty: %u",
								1000000 / pwm_get_period(), cfg->pwm.startup_duty, pwm_get_duty(gpio->pwm.channel));
					else
						length = snprintf(str, size, "pwm, startup duty: %u", cfg->pwm.startup_duty);

					break;
				}

				case(gpio_i2c):
				{
					length = snprintf(str, size, "i2c, pin: %s", cfg->i2c.pin == gpio_i2c_sda ? "sda" : "scl");

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

			length = snprintf(str, size, "\n");
			str += length;
			size -= length;
		}
	}
}

irom void gpios_dump_string(const gpio_t *gpio_cfgs, uint16_t size, char *string)
{
	dump(gpio_cfgs, 0, size, string);
}

irom app_action_t application_function_gpio_mode(application_parameters_t ap)
{
	gpio_mode_t mode;
	uint8_t gpio_index;
	gpio_trait_t *gpio;
	gpio_t *new_gpio_config;

	if(ap.nargs < 2)
	{
		dump(&config->gpios[0], 0, ap.size, ap.dst);
		return(app_action_normal);
	}

	gpio_index = atoi((*ap.args)[1]);

	if(!(gpio = find_gpio(gpio_index)))
	{
		snprintf(ap.dst, ap.size, "gpio-mode: invalid gpio %u\n", gpio_index);
		return(app_action_error);
	}

	if(ap.nargs < 3)
	{
		dump(&config->gpios[0], gpio, ap.size, ap.dst);
		return(app_action_normal);
	}

	if((mode = gpio_mode_from_string((*ap.args)[2])) == gpio_mode_error)
	{
		snprintf(ap.dst, ap.size, "gpio-mode: invalid mode %s\n", (*ap.args)[2]);
		return(app_action_error);
	}

	config_read_alt(tmpconfig);
	new_gpio_config = &tmpconfig->gpios[gpio->id];

	switch(mode)
	{
		case(gpio_counter):
		{
			if(gpio->flags.rtc_gpio)
			{
				snprintf(ap.dst, ap.size, "%s", "gpio-mode: counter mode invalid for gpio 16\n");
				return(app_action_error);
			}

			if(ap.nargs > 3)
			{
				new_gpio_config->counter.reset_on_get = !!atoi((*ap.args)[3]);

				if(ap.nargs > 4)
					new_gpio_config->counter.debounce = atoi((*ap.args)[4]);
			}

			break;
		}

		case(gpio_output):
		{
			if(ap.nargs != 4)
			{
				snprintf(ap.dst, ap.size, "gpio-mode(output): <startup value>\n");
				return(app_action_error);
			}

			new_gpio_config->output.startup_state = !!atoi((*ap.args)[3]);

			break;
		}

		case(gpio_bounce):
		{
			gpio_direction_t direction;
			uint32_t delay;
			bool_t repeat;
			bool_t autotrigger;

			if(ap.nargs != 7)
			{
				snprintf(ap.dst, ap.size, "gpio-mode: bounce direction:up/down delay:ms repeat:0/1 autotrigger:0/1\n");
				return(app_action_error);
			}

			if(!strcmp((*ap.args)[3], "up"))
				direction = gpio_up;
			else if(!strcmp((*ap.args)[3], "down"))
				direction = gpio_down;
			else
			{
				snprintf(ap.dst, ap.size, "gpio-mode(bounce): direction invalid: %s\n", (*ap.args)[3]);
				return(app_action_error);
			}

			delay = atoi((*ap.args)[4]);

			if(delay < 100)
			{
				snprintf(ap.dst, ap.size, "gpio-mode(bounce): delay too small: %d ms, >= 100 ms\n", delay);
				return(app_action_error);
			}

			repeat = atoi((*ap.args)[5]);
			autotrigger = !!atoi((*ap.args)[6]);

			new_gpio_config->bounce.direction = direction;
			new_gpio_config->bounce.delay = delay;
			new_gpio_config->bounce.repeat = repeat;
			new_gpio_config->bounce.autotrigger = autotrigger;

			break;
		}

		case(gpio_pwm):
		{
			uint16_t startup_duty;

			if(gpio->flags.rtc_gpio)
			{
				snprintf(ap.dst, ap.size, "%s", "gpio-mode: pwm mode invalid for gpio 16\n");
				return(app_action_error);
			}

			if(ap.nargs == 3)
				startup_duty = 0;
			else
				startup_duty = atoi((*ap.args)[3]);

			new_gpio_config->pwm.startup_duty = startup_duty;

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

	dump(&tmpconfig->gpios[gpio->id], gpio, ap.size, ap.dst);
	strlcat(ap.dst, "! gpio-mode: restart to activate new mode\n", ap.size);

	return(app_action_normal);
}

irom app_action_t application_function_gpio_get(application_parameters_t ap)
{
	uint8_t gpio_index;
	gpio_trait_t *gpio;
	const gpio_t *cfg;

	if(ap.nargs < 2)
	{
		snprintf(ap.dst, ap.size, "gpio-get: too little arguments: %u\n", ap.nargs - 1);
		return(app_action_error);
	}

	gpio_index = atoi((*ap.args)[1]);

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
		case(gpio_bounce):
		case(gpio_pwm):
		{
			snprintf(ap.dst, ap.size, "gpio-get: gpio %s is output\n", gpio->name);
			return(app_action_error);
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
	uint8_t gpio_index;
	gpio_trait_t *gpio;
	const gpio_t *cfg;

	gpio_index = atoi((*ap.args)[1]);

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
				gpio->counter.count = atoi((*ap.args)[2]);

			break;
		}

		case(gpio_output):
		{
			if(ap.nargs < 3)
			{
				snprintf(ap.dst, ap.size, "gpio-set: missing arguments\n");
				return(app_action_error);
			}

			set_output(gpio, !!atoi((*ap.args)[2]));

			break;
		}

		case(gpio_bounce):
		{
			if(ap.nargs == 3)
				trigger_bounce(gpio, !!atoi((*ap.args)[2]));
			else
				trigger_bounce(gpio, !gpio->bounce.delay);

			break;
		}

		case(gpio_pwm):
		{
			uint32_t duty;

			if(ap.nargs == 3)
				duty = atoi((*ap.args)[2]);
			else
				duty = cfg->pwm.startup_duty;

			trigger_pwm(gpio, duty);

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

	dump(&config->gpios[0], gpio, ap.size, ap.dst);
	return(app_action_normal);
}

irom app_action_t application_function_gpio_dump(application_parameters_t ap)
{
	dump(&config->gpios[0], 0, ap.size, ap.dst);

	return(app_action_normal);
}
