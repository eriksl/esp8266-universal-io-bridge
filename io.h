#ifndef io_h
#define io_h

#include "util.h"
#include "config.h"
#include "application.h"

#include <stdint.h>
#include <stdbool.h>

typedef enum
{
	io_ok = 0,
	io_error,
} io_error_t;

assert_size(io_error_t, 4);

enum
{
	max_pins_per_io = 16,
	max_triggers_per_pin = 2,
};

typedef enum
{
	io_id_first = 0,
	io_id_gpio = 0,
	io_id_aux,
	io_id_mcp_20,
	io_id_mcp_21,
	io_id_mcp_22,
	io_id_pcf_3a,
	io_id_ledpixel,
	io_id_pcf_26,
	io_id_mcp_spi,
	io_id_size,
} io_id_t;

assert_size(io_id_t, 4);

typedef struct attr_packed
{
	unsigned int io:4;
	unsigned int pin:4;
} config_io_t;

assert_size(config_io_t, 1);

typedef enum
{
	io_dir_none,
	io_dir_down,
	io_dir_up,
} io_direction_t;

assert_size(io_direction_t, 4);

typedef enum
{
	io_trigger_none,
	io_trigger_off,
	io_trigger_on,
	io_trigger_down,
	io_trigger_up,
	io_trigger_toggle,
	io_trigger_stop,
	io_trigger_start,
	io_trigger_size,
	io_trigger_error = io_trigger_size
} io_trigger_t;

assert_size(io_trigger_t, 4);

typedef enum
{
	io_pin_disabled = 0,
	io_pin_input_digital,
	io_pin_counter,
	io_pin_output_digital,
	io_pin_timer,
	io_pin_input_analog,
	io_pin_output_pwm1,
	io_pin_i2c,
	io_pin_uart,
	io_pin_lcd,
	io_pin_trigger,
	io_pin_ledpixel,
	io_pin_cfa634,
	io_pin_output_pwm2,
	io_pin_rotary_encoder,
	io_pin_spi,
	io_pin_pcint,
	io_pin_error,
	io_pin_size = io_pin_error,
} io_pin_mode_t;

assert_size(io_pin_mode_t, 4);

typedef enum
{
	io_flag_static_none =			0 << 0,
	io_flag_static_autostart =		1 << 0,
	io_flag_static_repeat =			1 << 1,
	io_flag_static_pullup =			1 << 2,
	io_flag_static_reset_on_read =	1 << 3,
	io_flag_static_extended =		1 << 4,
	io_flag_static_grb =			1 << 5,
	io_flag_static_linear =			1 << 6,
	io_flag_static_fill8 =			1 << 7,
	io_flag_static_invert =			1 << 8,
} io_pin_flag_static_t;

assert_size(io_pin_flag_static_t, 4);

typedef union
{
	io_pin_flag_static_t	io_pin_flags_static;
	unsigned int			intvalue;
} io_pin_flags_static_to_int_t;

typedef enum
{
	io_flag_dynamic_none =		0 << 0,
	io_flag_dynamic_suspended =	1 << 0,
} io_pin_flag_dynamic_t;

assert_size(io_pin_flag_dynamic_t, 4);

assert_size(io_pin_flags_static_to_int_t, 4);

typedef enum
{
	io_i2c_sda,
	io_i2c_scl,
	io_i2c_error,
	io_i2c_size = io_i2c_error,
} io_i2c_t;

assert_size(io_i2c_t, 4);

typedef enum
{
	io_pin_ll_disabled = 0,
	io_pin_ll_input_digital,
	io_pin_ll_counter,
	io_pin_ll_output_digital,
	io_pin_ll_input_analog,
	io_pin_ll_output_pwm1,
	io_pin_ll_i2c,
	io_pin_ll_uart,
	io_pin_ll_output_pwm2,
	io_pin_ll_spi,
	io_pin_ll_i2s,
	io_pin_ll_error,
	io_pin_ll_size = io_pin_ll_error
} io_pin_ll_mode_t;

assert_size(io_pin_ll_mode_t, 4);

typedef enum
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

assert_size(io_lcd_mode_t, 4);

typedef enum
{
	io_renc_unset = 0,
	io_renc_1a,
	io_renc_1b,
	io_renc_2a,
	io_renc_2b,
	io_renc_error,
	io_renc_size = io_renc_error,
} io_renc_pin_t;

assert_size(io_renc_pin_t, 4);

typedef enum
{
	caps_input_digital =	1 << 0,
	caps_counter =			1 << 1,
	caps_output_digital =	1 << 2,
	caps_input_analog =		1 << 3,
	caps_output_pwm1 =		1 << 4,
	caps_output_pwm2 =		1 << 5,
	caps_i2c =				1 << 6,
	caps_ledpixel =			1 << 7,
	caps_uart =				1 << 8,
	caps_pullup =			1 << 9,
	caps_rotary_encoder =	1 << 10,
	caps_spi =				1 << 11,
	caps_pin_change_int =	1 << 12,
} io_caps_t;

assert_size(io_caps_t, 4);

typedef struct attr_packed
{
	uint32_t		value;
	uint32_t		saved_value;
	uint32_t		speed;
	io_direction_t	direction:2;
	unsigned int	previous:1;
} io_data_pin_entry_t;

assert_size(io_data_pin_entry_t, 13);

typedef struct
{
	unsigned int detected:1;
	io_data_pin_entry_t pin[max_pins_per_io];
} io_data_entry_t;

assert_size(io_data_entry_t, sizeof(io_data_pin_entry_t) * max_pins_per_io + 4);

typedef io_data_entry_t io_data_t[io_id_size];

typedef struct attr_packed
{
	io_pin_mode_t			mode:5;
	io_pin_ll_mode_t		llmode:4;
	io_pin_flag_static_t 	static_flags:10;
	io_pin_flag_dynamic_t	dynamic_flags:1;
	io_direction_t			direction:2;
	unsigned int			speed:18;

	union
	{
		struct attr_packed
		{
			unsigned int lower_bound:20;
			unsigned int upper_bound:20;
		} output_pwm;

		struct attr_packed
		{
			io_i2c_t pin_mode;
		} i2c;

		struct attr_packed
		{
			io_lcd_mode_t pin_use;
		} lcd;

		struct attr_packed
		{
			io_renc_pin_t	pin_type:8;
			unsigned int	partner:8;
			struct attr_packed
			{
				int remote:8;
				int	io:8;
				int pin:8;
			} trigger_pin;
		} renc;

		struct attr_packed
		{
			config_io_t		io;
			io_trigger_t	action:8;
		} trigger[max_triggers_per_pin];
	} shared;
} io_config_pin_entry_t;

assert_size(io_config_pin_entry_t, 10);

typedef const struct io_info_entry_T
{
	attr_flash_align	io_id_t	id;
	attr_flash_align	uint32_t address;
	attr_flash_align	uint32_t instance;
	attr_flash_align	uint32_t pins;
	attr_flash_align	io_caps_t caps;
	attr_flash_align	const char *name;
	attr_flash_align	io_error_t	(* const init_fn)			(					const struct io_info_entry_T *);
	attr_flash_align	void		(* const post_init_fn)		(					const struct io_info_entry_T *);
	attr_flash_align	unsigned int(* const pin_max_value_fn)	(					const struct io_info_entry_T *, io_data_pin_entry_t *, const io_config_pin_entry_t *, unsigned int pin);
	attr_flash_align	void		(* const periodic_slow_fn)	(int io,			const struct io_info_entry_T *, io_data_entry_t *, unsigned int period);
	attr_flash_align	void		(* const periodic_fast_fn)	(int io,			const struct io_info_entry_T *, io_data_entry_t *, unsigned int period);
	attr_flash_align	void		(* const pin_change_int_fn)	(int io, 			const struct io_info_entry_T *, io_data_entry_t *);
	attr_flash_align	io_error_t	(* const init_pin_mode_fn)	(string_t *error,	const struct io_info_entry_T *, io_data_pin_entry_t *, const io_config_pin_entry_t *, int);
	attr_flash_align	io_error_t	(* const get_pin_info_fn)	(string_t *error,	const struct io_info_entry_T *, io_data_pin_entry_t *, const io_config_pin_entry_t *, int);
	attr_flash_align	io_error_t	(* const read_pin_fn)		(string_t *error,	const struct io_info_entry_T *, io_data_pin_entry_t *, const io_config_pin_entry_t *, int, unsigned int *);
	attr_flash_align	io_error_t	(* const write_pin_fn)		(string_t *error,	const struct io_info_entry_T *, io_data_pin_entry_t *, const io_config_pin_entry_t *, int, unsigned int);
	attr_flash_align	io_error_t	(* const set_mask_fn)		(string_t *error,	const struct io_info_entry_T *, unsigned int mask, unsigned int pins);
} io_info_entry_t;

assert_size(io_info_entry_t, 68);

extern io_config_pin_entry_t io_config[io_id_size][max_pins_per_io];

void			io_init(void);
void			io_pin_changed(unsigned int io, unsigned int pin, uint32_t pin_value_mask);
void			io_periodic_slow(unsigned int period);
void			io_periodic_fast(unsigned int period);
unsigned int	io_pin_max_value(unsigned int io, unsigned int pin);
io_error_t		io_read_pin(string_t *, unsigned int, unsigned int, unsigned int *);
io_error_t		io_write_pin(string_t *, unsigned int, unsigned int, unsigned int);
io_error_t		io_set_mask(string_t *error, int io, unsigned int mask, unsigned int pins);
io_error_t		io_trigger_pin(string_t *, unsigned int, unsigned int, io_trigger_t);
io_error_t		io_traits(string_t *, unsigned int io, unsigned int pin, io_pin_mode_t *mode, unsigned int *lower_bound, unsigned int *upper_bound, int *step, unsigned int *value);
void			io_config_dump(string_t *dst, int io_id, int pin_id, bool html);
void			io_string_from_ll_mode(string_t *, io_pin_ll_mode_t, int pad);

app_action_t application_function_io_mode(app_params_t *);
app_action_t application_function_io_read(app_params_t *);
app_action_t application_function_io_write(app_params_t *);
app_action_t application_function_io_trigger(app_params_t *);
app_action_t application_function_io_set_flag(app_params_t *);
app_action_t application_function_io_clear_flag(app_params_t *);
app_action_t application_function_io_set_mask(app_params_t *);
app_action_t application_function_io_suspend(app_params_t *);
app_action_t application_function_io_resume(app_params_t *);

#endif
