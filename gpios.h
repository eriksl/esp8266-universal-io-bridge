#ifndef gpios_h
#define gpios_h

#include "application.h"
#include "util.h"

#include <stdint.h>
#include <assert.h>

typedef enum __attribute__ ((__packed__)) // don't remvove!
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

typedef enum __attribute__ ((__packed__)) // don't remove!
{
	gpio_up,
	gpio_down,
} gpio_direction_t;

_Static_assert(sizeof(gpio_direction_t) == 1, "sizeof(gpio_direction_t) != 1"); // don't remove!

typedef enum __attribute__ ((__packed__)) // don't remove!
{
	gpio_i2c_sda,
	gpio_i2c_scl,
	gpio_i2c_error,
	gpio_i2c_size = gpio_i2c_error
} gpio_i2c_t;

_Static_assert(sizeof(gpio_i2c_t) == 1, "sizeof(gpio_i2c_t) != 1");

typedef enum __attribute__ ((__packed__)) // don't remove!
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
		uint8_t reset_on_get;
	} counter;

	struct
	{
		uint8_t startup_state;
	} output;

	struct
	{
		gpio_direction_t direction;
		uint32_t delay;
		uint8_t repeat;
		uint8_t autotrigger;
	} timer;

	struct
	{
		uint16_t	min_duty;
		uint16_t	max_duty;
		uint8_t 	delay;
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
void gpios_dump_string(string_t *dst, const gpio_config_t *);
void gpios_dump_html(string_t *dst, const gpio_config_t *);
bool gpios_trigger_output(int gpio_name);
bool gpios_set_wlan_trigger(int gpio_name);

app_action_t application_function_gpio_get(const string_t *, string_t *);
app_action_t application_function_gpio_set(const string_t *, string_t *);
app_action_t application_function_gpio_mode(const string_t *, string_t *);
app_action_t application_function_gpio_dump(const string_t *, string_t *);
app_action_t application_function_analog_read(const string_t *, string_t *);

#endif
