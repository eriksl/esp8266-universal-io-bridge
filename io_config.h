#ifndef io_config_h
#define io_config_h

#include "util.h"

#include <stdint.h>

enum
{
	max_pins_per_io = 16
};

typedef enum attr_packed
{
	io_id_gpio = 0,
	io_id_aux,
	io_id_mcp_20,
	io_id_pcf_3a,
	io_id_size,
} io_id_t;

assert_size(io_id_t, 1);

typedef struct
{
	int8_t io;
	int8_t pin;
} config_io_t;

assert_size(config_io_t, 2);

typedef enum attr_packed
{
	io_dir_none,
	io_dir_down,
	io_dir_up,
} io_direction_t;

assert_size(io_direction_t, 1);

typedef enum attr_packed
{
	io_trigger_off,
	io_trigger_on,
	io_trigger_down,
	io_trigger_up,
	io_trigger_size,
	io_trigger_error = io_trigger_size
} io_trigger_t;

assert_size(io_trigger_t, 1);

typedef enum attr_packed
{
	io_pin_disabled = 0,
	io_pin_input_digital,
	io_pin_counter,
	io_pin_output_digital,
	io_pin_timer,
	io_pin_input_analog,
	io_pin_output_analog,
	io_pin_i2c,
	io_pin_uart,
	io_pin_lcd,
	io_pin_trigger,
	io_pin_error,
	io_pin_size = io_pin_error,
} io_pin_mode_t;

assert_size(io_pin_mode_t, 1);

typedef struct
{
	unsigned int autostart:1;
	unsigned int repeat:1;
	unsigned int pullup:1;
	unsigned int reset_on_read:1;
} io_pin_flag_t;

typedef union
{
	io_pin_flag_t	io_pin_flags;
	uint32_t		intvalue;
} io_pin_flag_to_int_t;

assert_size(io_pin_flag_t, 4);

typedef enum attr_packed
{
	io_i2c_sda,
	io_i2c_scl,
	io_i2c_error,
	io_i2c_size = io_i2c_error,
} io_i2c_t;

assert_size(io_i2c_t, 1);

typedef enum attr_packed
{
	io_pin_ll_disabled = 0,
	io_pin_ll_input_digital,
	io_pin_ll_counter,
	io_pin_ll_output_digital,
	io_pin_ll_input_analog,
	io_pin_ll_output_analog,
	io_pin_ll_i2c,
	io_pin_ll_uart,
	io_pin_ll_error,
	io_pin_ll_size = io_pin_ll_error
} io_pin_ll_mode_t;

assert_size(io_pin_ll_mode_t, 1);

typedef enum attr_packed
{
	io_lcd_rs = 0,
	io_lcd_rw,
	io_lcd_e,
	io_lcd_d0,
	io_lcd_d1,
	io_lcd_d2,
	io_lcd_d3,
	io_lcd_d4,
	io_lcd_d5,
	io_lcd_d6,
	io_lcd_d7,
	io_lcd_bl,
	io_lcd_error,
	io_lcd_size = io_lcd_error
} io_lcd_mode_t;

assert_size(io_lcd_mode_t, 1);

typedef struct
{
	io_pin_mode_t		mode;
	io_pin_ll_mode_t	llmode;
	io_pin_flag_t		flags;
	io_direction_t		direction;
	uint32_t			speed;

	union
	{
		struct
		{
			uint16_t		lower_bound;
			uint16_t		upper_bound;
		} output_analog;

		struct
		{
			io_i2c_t		pin_mode;
		} i2c;

		struct
		{
			io_lcd_mode_t	pin_use;
		} lcd;

		struct
		{
			config_io_t		io;
			io_trigger_t	trigger_mode;
		} trigger;
	} shared;
} io_config_pin_entry_t;

typedef io_config_pin_entry_t io_config_t[io_id_size][max_pins_per_io];

#endif
