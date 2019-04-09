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

void			io_aux_periodic_fast(int io, const struct io_info_entry_T *, io_data_entry_t *, io_flags_t *);
io_error_t		io_aux_init(const struct io_info_entry_T *);
io_error_t		io_aux_init_pin_mode(string_t *, const struct io_info_entry_T *, io_data_pin_entry_t *, const io_config_pin_entry_t *, int);
unsigned int	io_aux_pin_max_value(const struct io_info_entry_T *info, io_data_pin_entry_t *data, const io_config_pin_entry_t *pin_config, unsigned int pin);
io_error_t		io_aux_get_pin_info(string_t *, const struct io_info_entry_T *, io_data_pin_entry_t *, const io_config_pin_entry_t *, int);
io_error_t		io_aux_read_pin(string_t *, const struct io_info_entry_T *, io_data_pin_entry_t *, const io_config_pin_entry_t *, int, unsigned int *);
io_error_t		io_aux_write_pin(string_t *, const struct io_info_entry_T *, io_data_pin_entry_t *, const io_config_pin_entry_t *, int, unsigned int);

#endif
