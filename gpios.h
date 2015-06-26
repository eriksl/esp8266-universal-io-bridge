#ifndef gpios_h
#define gpios_h

#include "application-parameters.h"

#include <stdint.h>

typedef enum
{
	gpio_2 = 0,
	gpios_amount = 1,
	gpios_pwm_amount = 1
} gpio_id_t;

typedef enum
{
	gpio_up,
	gpio_down,
} gpio_direction_t;

typedef enum
{
	gpio_disabled,
	gpio_input,
	gpio_output,
	gpio_bounce,
	gpio_pwm,
	gpio_mode_error,
	gpio_mode_size = gpio_mode_error
} gpio_mode_t;

typedef struct
{
	gpio_mode_t mode;

	struct
	{
		uint8_t startup_state;
	} output;

	struct
	{
		uint8_t direction;
		uint32_t delay;
		uint8_t repeat;
		uint8_t autotrigger;
	} bounce;

	struct
	{
		uint32_t startup_duty;
	} pwm;
} gpio_t;

void gpios_init(void);
void gpios_periodic(void);
void gpios_config_init(gpio_t *);
void gpios_dump_string(const gpio_t *, uint16_t, char *);

uint8_t application_function_gpio_get(application_parameters_t);
uint8_t application_function_gpio_set(application_parameters_t);
uint8_t application_function_gpio_mode(application_parameters_t);
uint8_t application_function_gpio_dump(application_parameters_t);

#endif
