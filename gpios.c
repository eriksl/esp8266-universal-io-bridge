#include "gpios.h"

#include "application.h"
#include "util.h"
#include "config.h"

#include <user_interface.h>
#include <gpio.h>

typedef struct
{
	gpio_id_t	id;
	const char	*name;
	gpio_t		*config;
	uint8_t		bitindex;
} gpio_trait_t;

static void get_gpio_to_disable(const gpio_trait_t *gpio);
static void set_gpio_to_input(const gpio_trait_t *gpio);
static void set_gpio_to_output(const gpio_trait_t *gpio, uint8_t startup_value);
static void set_gpio_to_bounce(const gpio_trait_t *gpio, uint8_t direction, uint32_t delay);
static void set_gpio_to_pwm(const gpio_trait_t *gpio, uint16_t duty);

static const gpio_trait_t gpio_traits[] =
{
	{
		gpio_2,
		"gpio2",
		&config.gpios.gpio_2,
		0x02
	},
	{
		gpio_id_error,
		"error",
		0,
		0xff
	}
};

ICACHE_FLASH_ATTR void gpios_init(const gpios_t *gpios)
{
	uint8_t ix;
	const gpio_trait_t *gpio;

	gpio_init();

	for(ix = 0; (gpio = &gpio_traits[ix])->id != gpio_id_error; ix++)
	{
		switch(gpio->config->mode)
		{
			case(gpio_disable):
			{
				get_gpio_to_disable(gpio);
				break;
			}

			case(gpio_input):
			{
				set_gpio_to_input(gpio);
				break;
			}

			case(gpio_output):
			{
				set_gpio_to_output(gpio, gpio->config->output.startup_value);
				break;
			}

			case(gpio_bounce):
			{
				set_gpio_to_bounce(gpio, gpio->config->bounce.direction, gpio->config->bounce.delay);
				break;
			}

			case(gpio_pwm):
			{
				set_gpio_to_pwm(gpio, gpio->config->pwm.duty);
				break;
			}

			default:
			{
			}
		}
	}
}

ICACHE_FLASH_ATTR void gpios_config_init(gpios_t *gpios)
{
	gpios->gpio_2.mode = gpio_disable;
	gpios->gpio_2.output.startup_value = 0;
	gpios->gpio_2.bounce.direction = gpio_up;
	gpios->gpio_2.bounce.delay = 0;
	gpios->gpio_2.pwm.duty = 0;
}

ICACHE_FLASH_ATTR static void set_output(const gpio_trait_t *gpio, uint8_t onoff)
{
	gpio_output_set(onoff ? (1 << gpio->bitindex) : 0x00, 
					!onoff ? (1 << gpio->bitindex) : 0x00,
					0x00, 0x00);
}

ICACHE_FLASH_ATTR static uint8_t get_input(const gpio_trait_t *gpio)
{
	return(!!(gpio_input_get() & (1 << gpio->bitindex)));
}

ICACHE_FLASH_ATTR static const gpio_trait_t *find_gpio_by_name(const char *name)
{
	uint8_t ix;
	const gpio_trait_t *trait;

	for(ix = 0; (trait = &gpio_traits[ix])->id != gpio_id_error; ix++)
		if(!strcmp(name, trait->name))
			break;

	if(trait->id == gpio_id_error)
		return(0);

	return(trait);
}

ICACHE_FLASH_ATTR static uint8_t gpio_mode_from_string(const char *mode)
{
	if(!strcmp(mode, "disable"))
		return(gpio_disable);
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

ICACHE_FLASH_ATTR static void get_gpio_to_disable(const gpio_trait_t *gpio)
{
	gpio->config->mode = gpio_disable;

	gpio_output_set(0, 0, 0, 1 << gpio->bitindex);
}

ICACHE_FLASH_ATTR static void set_gpio_to_input(const gpio_trait_t *gpio)
{
	gpio->config->mode = gpio_input;

	gpio_output_set(0, 0, 0, 1 << gpio->bitindex);
}

ICACHE_FLASH_ATTR static void set_gpio_to_output(const gpio_trait_t *gpio, uint8_t startup_value)
{
	gpio->config->mode = gpio_output;
	gpio->config->output.startup_value = startup_value;

	gpio_output_set(0, 0, 1 << gpio->bitindex, 0);
	set_output(gpio, startup_value);
}

ICACHE_FLASH_ATTR static void set_gpio_to_bounce(const gpio_trait_t *gpio, uint8_t direction, uint32_t delay)
{
	gpio->config->mode = gpio_bounce;
	gpio->config->bounce.direction = direction;
	gpio->config->bounce.delay = delay;

	gpio_output_set(0, 0, 1 << gpio->bitindex, 0);
}

ICACHE_FLASH_ATTR static void set_gpio_to_pwm(const gpio_trait_t *gpio, uint16_t duty)
{
	gpio->config->mode = gpio_pwm;
	gpio->config->pwm.duty = duty;

	gpio_output_set(0, 0, 1 << gpio->bitindex, 0);
}

ICACHE_FLASH_ATTR static void dump(const gpio_trait_t *gpio_in, uint16_t size, char *str)
{
	uint8_t ix;
	uint16_t length;
	const gpio_trait_t *gpio;

	for(ix = 0; (gpio = &gpio_traits[ix])->id != gpio_id_error; ix++)
	{
		if(!gpio_in || (gpio_in->id == gpio->id))
		{
			length = snprintf(str, size, "> %s: ", gpio->name);
			size -= length;
			str += length;

			switch(gpio->config->mode)
			{
				case(gpio_disable):
				{
					length = snprintf(str, size, "disabled");
					break;
				}

				case(gpio_input):
				{
					length = snprintf(str, size, "input: %s", onoff(get_input(gpio)));
					break;
				}

				case(gpio_output):
				{
					length = snprintf(str, size, "output: %s, startup: %s", onoff(get_input(gpio)), onoff(gpio->config->output.startup_value));
					break;
				}

				case(gpio_bounce):
				{
					length = snprintf(str, size, "bounce, direction: %s, delay: %u ms",
							gpio->config->bounce.direction == gpio_up ? "up" : "down",
							gpio->config->bounce.delay);
					break;
				}

				case(gpio_pwm):
				{
					length = snprintf(str, size, "pwm, duty: %u", gpio->config->pwm.duty);
					break;
				}

				default:
				{
					length = snprintf(str, size, "unknown mode");
					break;
				}
			}

			size -= length;
			str += length;
			strlcpy(str, "\n", size);
		}
	}
}

ICACHE_FLASH_ATTR void gpios_dump_string(uint16_t size, char *string)
{
	dump(0, size, string);
}

ICACHE_FLASH_ATTR uint8_t application_function_gpio_mode(application_parameters_t ap)
{
	uint8_t mode;
	const gpio_trait_t *gpio;

	if(ap.nargs < 2)
	{
		dump(0, ap.size, ap.dst);
		return(1);
	}

	if(!(gpio = find_gpio_by_name((*ap.args)[1])))
	{
		snprintf(ap.dst, ap.size, "gpio-mode: invalid gpio %s\n", (*ap.args)[1]);
		return(1);
	}

	if(ap.nargs < 3)
	{
		dump(gpio, ap.size, ap.dst);
		return(1);
	}

	mode = gpio_mode_from_string((*ap.args)[2]);

	switch(mode)
	{
		case(gpio_disable):
		{
			get_gpio_to_disable(gpio);
			break;
		}

		case(gpio_input):
		{
			set_gpio_to_input(gpio);
			break;
		}

		case(gpio_output):
		{
			if(ap.nargs != 4)
			{
				snprintf(ap.dst, ap.size, "gpio-mode(output): startup value value\n");
				return(1);
			}

			set_gpio_to_output(gpio, !!atoi((*ap.args)[3]));
			break;
		}

		case(gpio_bounce):
		{
			uint8_t direction;
			uint32_t delay;

			if(ap.nargs != 5)
			{
				snprintf(ap.dst, ap.size, "gpio-mode(bounce): direction or delay missing\n");
				return(1);
			}

			if(!strcmp((*ap.args)[3], "up"))
				direction = gpio_up;
			else if(!strcmp((*ap.args)[3], "down"))
				direction = gpio_down;
			else
			{
				snprintf(ap.dst, ap.size, "gpio-mode(bounce): direction invalid: %s\n", (*ap.args)[3]);
				return(1);
			}

			delay = atoi((*ap.args)[4]);

			set_gpio_to_bounce(gpio, direction, delay);
			break;
		}

		case(gpio_pwm):
		{
			uint16_t duty;

			if(ap.nargs != 4)
			{
				snprintf(ap.dst, ap.size, "gpio-mode(pwm): invalid number of arguments %u, must be 3\n", ap.nargs - 1);
				return(1);
			}

			duty = atoi((*ap.args)[3]);

			if(duty > 1023)
			{
				snprintf(ap.dst, ap.size, "gpio-mode(pwm): invalid duty: %u, duty must be < 1024\n", duty);
				return(1);
			}

			set_gpio_to_pwm(gpio, duty);
			break;
		}

		default:
		{
			snprintf(ap.dst, ap.size, "gpio-mode: invalid mode %s\n", (*ap.args)[2]);
			return(1);
		}
	}

	dump(gpio, ap.size, ap.dst);

	return(1);
}

ICACHE_FLASH_ATTR uint8_t application_function_gpio_get(application_parameters_t ap)
{
	const gpio_trait_t *gpio;

	if(ap.nargs < 2)
	{
		snprintf(ap.dst, ap.size, "gpio-get: too little arguments: %u\n", ap.nargs - 1);
		return(1);
	}

	if(!(gpio = find_gpio_by_name((*ap.args)[1])))
	{
		snprintf(ap.dst, ap.size, "gpio-get: invalid gpio %s\n", (*ap.args)[1]);
		return(1);
	}

	switch(gpio->config->mode)
	{
		case(gpio_disable):
		{
			snprintf(ap.dst, ap.size, "gpio-get: gpio %s is disabled\n", gpio->name);
			return(1);
		}

		case(gpio_input):
		{
			snprintf(ap.dst, ap.size, "gpio-get: gpio %s is %s\n", gpio->name, onoff(get_input(gpio)));
			return(1);
		}

		case(gpio_output):
		case(gpio_bounce):
		case(gpio_pwm):
		{
			snprintf(ap.dst, ap.size, "gpio-get: gpio %s is output\n", gpio->name);
			return(1);
		}

		default:
		{
		}
	}

	snprintf(ap.dst, ap.size, "gpio-get: invalid mode %s\n", (*ap.args)[2]);
	return(1);
}

ICACHE_FLASH_ATTR uint8_t application_function_gpio_set(application_parameters_t ap)
{
	const gpio_trait_t *gpio;

	if(ap.nargs < 3)
	{
		snprintf(ap.dst, ap.size, "gpio-set: too little arguments: %u\n", ap.nargs - 1);
		return(1);
	}

	if(!(gpio = find_gpio_by_name((*ap.args)[1])))
	{
		snprintf(ap.dst, ap.size, "gpio-set: invalid gpio %s\n", (*ap.args)[1]);
		return(1);
	}

	switch(gpio->config->mode)
	{
		case(gpio_disable):
		{
			snprintf(ap.dst, ap.size, "gpio-set: gpio %s is disabled\n", gpio->name);
			return(1);
		}

		case(gpio_input):
		{
			snprintf(ap.dst, ap.size, "gpio-set: gpio %s is input\n", gpio->name);
			return(1);
		}

		case(gpio_output):
		{
			set_output(gpio, !!atoi((*ap.args)[2]));
			break;
		}

		case(gpio_bounce):
		{
			snprintf(ap.dst, ap.size, "gpio-set: gpio %s is bounce not implemented\n", gpio->name);
			return(1);
		}

		case(gpio_pwm):
		{
			snprintf(ap.dst, ap.size, "gpio-set: gpio %s is pwm not implemented\n", gpio->name);
			return(1);
		}

		default:
		{
			snprintf(ap.dst, ap.size, "gpio-set: invalid mode %s\n", (*ap.args)[2]);
			return(1);
		}
	}

	snprintf(ap.dst, ap.size, "gpio-set: gpio %s is set\n", gpio->name);
	return(1);
}

ICACHE_FLASH_ATTR uint8_t application_function_gpio_dump(application_parameters_t ap)
{
	dump(0, ap.size, ap.dst);

	return(1);
}
