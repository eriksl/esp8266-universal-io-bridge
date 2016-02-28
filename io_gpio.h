#ifndef io_gpio_h
#define io_gpio_h

#include "util.h"
#include "io_shared.h"

#include <stdint.h>

void		io_gpio_periodic(const struct io_info_entry_T *, io_data_entry_t *, io_flags_t *);
io_error_t	io_gpio_init(const struct io_info_entry_T *);
io_error_t	io_gpio_init_pin_mode(string_t *, const struct io_info_entry_T *, io_data_pin_entry_t *, const io_config_pin_entry_t *, int);
io_error_t	io_gpio_get_pin_info(string_t *, const struct io_info_entry_T *, io_data_pin_entry_t *, const io_config_pin_entry_t *, int);
io_error_t	io_gpio_read_pin(string_t *, const struct io_info_entry_T *, io_data_pin_entry_t *, const io_config_pin_entry_t *, int, int *);
io_error_t	io_gpio_write_pin(string_t *, const struct io_info_entry_T *, io_data_pin_entry_t *, const io_config_pin_entry_t *, int, int);

#endif
