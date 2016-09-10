#ifndef io_aux_h
#define io_aux_h

#include "io.h"
#include "util.h"

#include <stdint.h>

typedef enum
{
	io_aux_pin_rtc = 0,
	io_aux_pin_adc,
	io_aux_pin_error,
	io_aux_pin_size = io_aux_pin_error
} io_aux_pin_t;

assert_size(io_aux_pin_t, 4);

io_error_t	io_aux_init(const struct io_info_entry_T *);
io_error_t	io_aux_init_pin_mode(string_t *, const struct io_info_entry_T *, io_data_pin_entry_t *, const io_config_pin_entry_t *, int);
io_error_t	io_aux_get_pin_info(string_t *, const struct io_info_entry_T *, io_data_pin_entry_t *, const io_config_pin_entry_t *, int);
io_error_t	io_aux_read_pin(string_t *, const struct io_info_entry_T *, io_data_pin_entry_t *, const io_config_pin_entry_t *, int, int *);
io_error_t	io_aux_write_pin(string_t *, const struct io_info_entry_T *, io_data_pin_entry_t *, const io_config_pin_entry_t *, int, int);

#endif
