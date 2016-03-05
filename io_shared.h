#ifndef io_shared_h
#define io_shared_h

#include "io_config.h"
#include "io.h"

typedef struct
{
	unsigned int input_digital:1;
	unsigned int counter:1;
	unsigned int output_digital:1;
	unsigned int input_analog:1;
	unsigned int output_analog:1;
	unsigned int i2c:1;
	unsigned int pullup:1;
} io_caps_t;

assert_size(io_caps_t, 4);

typedef struct
{
	unsigned int counter_triggered:1;
} io_flags_t;

assert_size(io_flags_t, 4);

typedef struct
{
	io_direction_t	direction;
	uint16_t		delay;
} io_data_pin_entry_t;

assert_size(io_data_pin_entry_t, 4);

typedef struct
{
	unsigned int detected:1;
	io_data_pin_entry_t pin[max_pins_per_io];

} io_data_entry_t;

typedef io_data_entry_t io_data_t[io_id_size];

typedef const struct io_info_entry_T
{
	uint8_t address;
	uint8_t pins;
	io_caps_t caps;
	const char *name;
	io_error_t	(* const init_fn)			(					const struct io_info_entry_T *);
	void		(* const periodic_fn)		(					const struct io_info_entry_T *, io_data_entry_t *, io_flags_t *);
	io_error_t	(* const init_pin_mode_fn)	(string_t *error,	const struct io_info_entry_T *, io_data_pin_entry_t *, const io_config_pin_entry_t *, int);
	io_error_t	(* const get_pin_info_fn)	(string_t *error,	const struct io_info_entry_T *, io_data_pin_entry_t *, const io_config_pin_entry_t *, int);
	io_error_t	(* const read_pin_fn)		(string_t *error,	const struct io_info_entry_T *, io_data_pin_entry_t *, const io_config_pin_entry_t *, int, int *);
	io_error_t	(* const write_pin_fn)		(string_t *error,	const struct io_info_entry_T *, io_data_pin_entry_t *, const io_config_pin_entry_t *, int, int);
} io_info_entry_t;

typedef const io_info_entry_t io_info_t[io_id_size];

#endif
