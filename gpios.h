#ifndef gpios_h
#define gpios_h

#include "application-parameters.h"
#include "util.h"

#include <stdint.h>
#include <assert.h>

typedef enum __attribute__ ((__packed__))
{
	gpio_0 = 0,
	gpio_1,		// UART TX use with care
	gpio_2,
	gpio_3,		// UART RX use with care
	gpio_4,
	gpio_5,
//	gpio_6,		// cannot use, used by flash memory
//	gpio_7,		// cannot use, used by flash memory
//	gpio_8,		// cannot use, used by flash memory
//	gpio_9,		// cannot use, used by flash memory
//	gpio_10,	// cannot use, used by flash memory
//	gpio_11,	// cannot use, used by flash memory
	gpio_12,
	gpio_13,
	gpio_14,
	gpio_15,
	gpio_16,
	gpio_error,
	gpio_size = gpio_error,
	gpio_pwm_size = 8,
}  gpio_id_t;

_Static_assert(sizeof(gpio_id_t) == 1, "sizeof(gpio_id_t) != 1");

typedef enum __attribute__ ((__packed__))
{
	gpio_up,
	gpio_down,
} gpio_direction_t;

_Static_assert(sizeof(gpio_direction_t) == 1, "sizeof(gpio_direction_t) != 1");

typedef enum __attribute__ ((__packed__))
{
	gpio_i2c_sda,
	gpio_i2c_scl,
	gpio_i2c_error,
	gpio_i2c_size = gpio_i2c_error
} gpio_i2c_t;

_Static_assert(sizeof(gpio_i2c_t) == 1, "sizeof(gpio_i2c_t) != 1");

typedef enum __attribute__ ((__packed__))
{
	gpio_disabled,
	gpio_input,
	gpio_counter,
	gpio_output,
	gpio_timer,
	gpio_pwm,
	gpio_i2c,
	gpio_mode_error,
	gpio_mode_size = gpio_mode_error
} gpio_mode_t;

_Static_assert(sizeof(gpio_mode_t) == 1, "sizeof(gpio_mode_t) != 1");

typedef struct
{
	gpio_mode_t mode;

	struct
	{
		uint32_t debounce;
		bool_t reset_on_get;
	} counter;

	struct
	{
		bool_t startup_state;
	} output;

	struct
	{
		gpio_direction_t direction;
		uint32_t delay;
		bool_t repeat;
		bool_t autotrigger;
	} timer;

	struct
	{
		uint16_t	min_duty;
		uint16_t	max_duty;
		uint8_t 	speed;
	} pwm;

	struct
	{
		gpio_i2c_t pin;
	} i2c;
} gpio_config_entry_t;

typedef struct
{
	gpio_config_entry_t	entry[gpio_size];
} gpio_config_t;

void gpios_init(void);
void gpios_periodic(void);
void gpios_config_init(gpio_config_t *);
void gpios_dump_string(const gpio_config_t *, unsigned int, char *);

app_action_t application_function_gpio_get(application_parameters_t);
app_action_t application_function_gpio_set(application_parameters_t);
app_action_t application_function_gpio_mode(application_parameters_t);
app_action_t application_function_gpio_dump(application_parameters_t);

#endif
