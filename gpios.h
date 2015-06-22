#ifndef gpios_h
#define gpios_h

#include "application-parameters.h"

#include <stdint.h>

typedef enum
{
	gpio_2,
} gpio_id_t;

typedef enum
{
	gpio_up,
	gpio_down,
} gpio_direction_t;

typedef enum
{
	gpio_disable,
	gpio_input,
	gpio_output,
	gpio_bounce,
	gpio_pwm,
	gpio_mode_error
} gpio_mode_t;

typedef struct
{
	gpio_mode_t mode;

	struct
	{
		uint8_t startup_value;
	} output;

	struct
	{
		uint8_t direction;
		uint32_t delay;
	} bounce;

	struct
	{
		uint16_t duty;
	} pwm;
} gpio_t;

typedef struct
{
	gpio_t gpio_2;
} gpios_t;

void gpios_init(const gpios_t *gpios);
void gpios_config_init(gpios_t *gpios);
void gpios_dump_string(uint16_t, char *);

uint8_t application_function_gpio_get(application_parameters_t);
uint8_t application_function_gpio_set(application_parameters_t);
uint8_t application_function_gpio_mode(application_parameters_t);
uint8_t application_function_gpio_dump(application_parameters_t);

#endif
