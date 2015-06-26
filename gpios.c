#include "gpios.h"

#include "application-parameters.h"
#include "util.h"
#include "config.h"

#include <user_interface.h>
#include <osapi.h>
#include <gpio.h>
#include <pwm.h>

typedef struct
{
	ETSTimer	timer;
	uint8_t		armed;
} gpio_timer_t;

typedef struct
{
			uint8_t		channel;
	const	uint32_t	io_mux;
	const	uint32_t	io_func;
} gpio_pwm_t;

typedef struct
{
	const	gpio_id_t		id;
	const	char			*name;
	const	uint8_t			index;
			gpio_t			*config;
			gpio_timer_t	timer;
			gpio_pwm_t		pwm;
} gpio_trait_t;

typedef struct
{
	uint8_t			mode;
	const char		*name;
	void			(*init_fn)(gpio_trait_t *);
} gpio_mode_to_initfn_t;

enum
{
	gpios_amount = 1,
	gpios_pwm_amount = 1
};

static void gpio_init_disabled(gpio_trait_t *);
static void gpio_init_input(gpio_trait_t *);
static void gpio_init_output(gpio_trait_t *);
static void gpio_init_bounce(gpio_trait_t *);
static void gpio_init_pwm(gpio_trait_t *);

static gpio_trait_t *find_gpio(uint8_t);

static void clear_timer(gpio_trait_t *);

static gpio_mode_to_initfn_t gpio_mode_to_initfn[gpio_mode_size] =
{
	{ gpio_disabled,	"disabled",		gpio_init_disabled },
	{ gpio_input,		"input",		gpio_init_input },
	{ gpio_output,		"output",		gpio_init_output },
	{ gpio_bounce,		"bounce",		gpio_init_bounce },
	{ gpio_pwm,			"pwm",			gpio_init_pwm },
};

static gpio_trait_t gpio_traits[gpios_amount] =
{
	{
		.id = gpio_2,
		.name = "gpio2",
		.index = 2,
		.config = &config.gpios.gpio_2,
		.pwm =
		{
			.io_mux = PERIPHS_IO_MUX_GPIO2_U,
			.io_func = FUNC_GPIO2,
		},
	},
};

ICACHE_FLASH_ATTR void gpios_init(const gpios_t *gpios)
{
	uint8_t ix, pwmchannel;
	gpio_trait_t *gpio;
	uint32_t pwm_io_info[gpios_pwm_amount][3];
	uint32_t pwm_duty_init[gpios_pwm_amount];

	gpio_init();

	for(ix = 0, pwmchannel = 0; ix < gpios_amount; ix++)
	{
		gpio = &gpio_traits[ix];
		gpio->timer.armed = 0;

		if((gpio->config->mode == gpio_pwm) && (pwmchannel < gpios_pwm_amount))
		{
			gpio->pwm.channel = pwmchannel;
			pwm_io_info[pwmchannel][0] = gpio->pwm.io_mux;
			pwm_io_info[pwmchannel][1] = gpio->pwm.io_func;
			pwm_io_info[pwmchannel][2] = gpio->index;
			pwm_duty_init[pwmchannel] = 0;
			pwmchannel++;
		}
	}

	if(pwmchannel > 0)
		pwm_init(3000, pwm_duty_init, pwmchannel, pwm_io_info);

	for(ix = 0; ix < gpios_amount; ix++)
		gpio_mode_to_initfn[gpio->config->mode].init_fn(&gpio_traits[ix]);
}

ICACHE_FLASH_ATTR void gpios_config_init(gpios_t *gpios)
{
	gpios->gpio_2.mode = gpio_disabled;
	gpios->gpio_2.output.startup_state = 0;
	gpios->gpio_2.bounce.direction = gpio_up;
	gpios->gpio_2.bounce.delay = 0;
	gpios->gpio_2.bounce.repeat = 0;
	gpios->gpio_2.bounce.autotrigger = 0;
	gpios->gpio_2.pwm.startup_duty = 0;
}

ICACHE_FLASH_ATTR static void set_output(const gpio_trait_t *gpio, uint8_t onoff)
{
	gpio_output_set(onoff ? (1 << gpio->index) : 0x00,
					!onoff ? (1 << gpio->index) : 0x00,
					0x00, 0x00);
}

ICACHE_FLASH_ATTR static uint8_t get_input(const gpio_trait_t *gpio)
{
	return(!!(gpio_input_get() & (1 << gpio->index)));
}

ICACHE_FLASH_ATTR static void clear_timer(gpio_trait_t *gpio)
{
	if(gpio->timer.armed)
	{
		os_timer_disarm(&gpio->timer.timer);
		gpio->timer.armed = 0;
	}
}

ICACHE_FLASH_ATTR static void bounce_timer_callback(void *arg)
{
	gpio_trait_t *gpio = (gpio_trait_t *)arg;

	set_output(gpio, !get_input(gpio));

	if(!gpio->config->bounce.repeat)
		clear_timer(gpio);
}

ICACHE_FLASH_ATTR static void trigger_bounce(gpio_trait_t *gpio, uint8_t onoff)
{
	clear_timer(gpio);

	if(onoff)
	{
		set_output(gpio, gpio->config->bounce.direction == gpio_up ? 1 : 0);
		os_timer_setfn(&gpio->timer.timer, bounce_timer_callback, (void *)gpio);
		os_timer_arm(&gpio->timer.timer, gpio->config->bounce.delay, gpio->config->bounce.repeat);
		gpio->timer.armed = 1;
	}
	else
		set_output(gpio, gpio->config->bounce.direction == gpio_up ? 0 : 1);
}

ICACHE_FLASH_ATTR  static void trigger_pwm(const gpio_trait_t *gpio, uint32_t duty)
{
	pwm_set_duty(duty, gpio->pwm.channel);
	pwm_start();
}

ICACHE_FLASH_ATTR static gpio_trait_t *find_gpio(uint8_t index)
{
	uint8_t current;
	gpio_trait_t *gpio;

	for(current = 0; current < gpios_amount; current++)
	{
		gpio = &gpio_traits[current];

		if(gpio->index == index)
			return(gpio);
	}

	return(0);
}

ICACHE_FLASH_ATTR static uint8_t gpio_mode_from_string(const char *mode)
{
	if(!strcmp(mode, "disable"))
		return(gpio_disabled);
	else if(!strcmp(mode, "input"))
		return(gpio_input);
	else if(!strcmp(mode, "output"))
		return(gpio_output);
	else if(!strcmp(mode, "bounce"))
		return(gpio_bounce);
	else if(!strcmp(mode, "pwm"))
		return(gpio_pwm);
	else
		return(gpio_mode_error);
}

ICACHE_FLASH_ATTR static void gpio_init_disabled(gpio_trait_t *gpio)
{
	clear_timer(gpio);

	gpio_output_set(0, 0, 0, 1 << gpio->index);
}

ICACHE_FLASH_ATTR static void gpio_init_input(gpio_trait_t *gpio)
{
	clear_timer(gpio);

	gpio_output_set(0, 0, 0, 1 << gpio->index);
}

ICACHE_FLASH_ATTR static void gpio_init_output(gpio_trait_t *gpio)
{
	clear_timer(gpio);

	gpio_output_set(0, 0, 1 << gpio->index, 0);

	if(gpio->config->output.startup_state)
		gpio_output_set(1 << gpio->index, 0, 0, 0);
	else
		gpio_output_set(0, 1 << gpio->index, 0, 0);
}

ICACHE_FLASH_ATTR static void gpio_init_bounce(gpio_trait_t *gpio)
{
	clear_timer(gpio);

	gpio_output_set(0, 0, 1 << gpio->index, 0);

	if(gpio->config->bounce.direction == gpio_up)
		gpio_output_set(0, 1 << gpio->index, 0, 0);
	else
		gpio_output_set(1 << gpio->index, 0, 0, 0);

	if(gpio->config->bounce.autotrigger)
		trigger_bounce(gpio, 1);
}

ICACHE_FLASH_ATTR static void gpio_init_pwm(gpio_trait_t *gpio)
{
	clear_timer(gpio);

	trigger_pwm(gpio, gpio->config->pwm.startup_duty);
}

ICACHE_FLASH_ATTR static void dump(const gpio_trait_t *gpio_in, uint16_t size, char *str)
{
	uint8_t ix;
	uint16_t length;
	const gpio_trait_t *gpio;

	for(ix = 0; ix < gpios_amount; ix++)
	{
		gpio = &gpio_traits[ix];

		if(!gpio_in || (gpio_in->id == gpio->id))
		{
			length = snprintf(str, size, "> gpio: %u, name: %s, mode: ", gpio->index, gpio->name);
			size -= length;
			str += length;

			switch(gpio->config->mode)
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

				case(gpio_output):
				{
					length = snprintf(str, size, "output, state: %s, startup: %s",
							onoff(get_input(gpio)), onoff(gpio->config->output.startup_state));
					break;
				}

				case(gpio_bounce):
				{
					length = snprintf(str, size, "bounce, direction: %s, delay: %u ms, repeat: %s, autotrigger: %s, active: %s, current state: %s",
							gpio->config->bounce.direction == gpio_up ? "up" : "down",
							gpio->config->bounce.delay, onoff(gpio->config->bounce.repeat),
							onoff(gpio->config->bounce.autotrigger), onoff(gpio->timer.armed),
							onoff(get_input(gpio)));
					break;
				}

				case(gpio_pwm):
				{
					length = snprintf(str, size, "pwm, frequency: %u Hz, startup duty: %u, current duty: %u",
							1000000 / pwm_get_period(), gpio->config->pwm.startup_duty, pwm_get_duty(gpio->pwm.channel));

					break;
				}

				default:
				{
					length = snprintf(str, size, "unknown mode");
					break;
				}
			}

			strlcpy(str + length, "\n", size - length);
		}
	}
}

ICACHE_FLASH_ATTR void gpios_dump_string(uint16_t size, char *string)
{
	dump(0, size, string);
}

ICACHE_FLASH_ATTR uint8_t application_function_gpio_mode(application_parameters_t ap)
{
	uint8_t prevmode, mode;
	uint8_t gpio_index;
	gpio_trait_t *gpio;

	if(ap.nargs < 2)
	{
		dump(0, ap.size, ap.dst);
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
		dump(gpio, ap.size, ap.dst);
		return(app_action_normal);
	}

	if((mode = gpio_mode_from_string((*ap.args)[2])) == gpio_mode_error)
	{
		snprintf(ap.dst, ap.size, "gpio-mode: invalid mode %s\n", (*ap.args)[2]);
		return(app_action_error);
	}

	switch(mode)
	{
		case(gpio_output):
		{
			if(ap.nargs != 4)
			{
				snprintf(ap.dst, ap.size, "gpio-mode(output): <startup value>\n");
				return(app_action_error);
			}

			gpio->config->output.startup_state = !!atoi((*ap.args)[3]);

			break;
		}

		case(gpio_bounce):
		{
			uint8_t direction;
			uint32_t delay;
			uint8_t repeat;
			uint8_t autotrigger;

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

			gpio->config->mode = gpio_bounce;
			gpio->config->bounce.direction = direction;
			gpio->config->bounce.delay = delay;
			gpio->config->bounce.repeat = repeat;
			gpio->config->bounce.autotrigger = autotrigger;

			break;
		}

		case(gpio_pwm):
		{
			uint16_t startup_duty;

			if(ap.nargs == 3)
				startup_duty = 0;
			else
				startup_duty = atoi((*ap.args)[3]);

			if(startup_duty > 65535)
			{
				snprintf(ap.dst, ap.size, "gpio-mode(pwm): invalid startup duty: %u, duty must be 0-65535\n", startup_duty);
				return(app_action_error);
			}

			gpio->config->pwm.startup_duty = startup_duty;

			break;
		}

		default:
		{
		}
	}

	prevmode = gpio->config->mode;
	gpio->config->mode = mode;
	gpio_mode_to_initfn[gpio->config->mode].init_fn(gpio);

	dump(gpio, ap.size, ap.dst);

	if(prevmode != mode)
		strlcat(ap.dst, "> write config and restart to activate new mode\n", ap.size);

	return(app_action_normal);
}

ICACHE_FLASH_ATTR uint8_t application_function_gpio_get(application_parameters_t ap)
{
	uint8_t gpio_index;
	const gpio_trait_t *gpio;

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

	switch(gpio->config->mode)
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

		case(gpio_output):
		case(gpio_bounce):
		case(gpio_pwm):
		{
			snprintf(ap.dst, ap.size, "gpio-get: gpio %s is output\n", gpio->name);
			return(app_action_error);
		}

		default:
		{
		}
	}

	snprintf(ap.dst, ap.size, "gpio-get: invalid mode %u\n", gpio->config->mode);
	return(app_action_error);
}

ICACHE_FLASH_ATTR uint8_t application_function_gpio_set(application_parameters_t ap)
{
	uint8_t gpio_index;
	gpio_trait_t *gpio;

	gpio_index = atoi((*ap.args)[1]);

	if(!(gpio = find_gpio(gpio_index)))
	{
		snprintf(ap.dst, ap.size, "gpio-set: invalid gpio %u\n", gpio_index);
		return(app_action_error);
	}

	switch(gpio->config->mode)
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
				trigger_bounce(gpio, !gpio->timer.armed);

			break;
		}

		case(gpio_pwm):
		{
			uint32_t duty;

			if(ap.nargs == 3)
				duty = atoi((*ap.args)[2]);
			else
				duty = gpio->config->pwm.startup_duty;

			trigger_pwm(gpio, duty);

			break;
		}

		default:
		{
			snprintf(ap.dst, ap.size, "gpio-set: invalid mode %u\n", gpio->config->mode);
			return(app_action_error);
		}
	}

	dump(gpio, ap.size, ap.dst);
	return(app_action_normal);
}

ICACHE_FLASH_ATTR uint8_t application_function_gpio_dump(application_parameters_t ap)
{
	dump(0, ap.size, ap.dst);

	return(app_action_normal);
}
