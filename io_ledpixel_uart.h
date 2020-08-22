#ifndef io_ledpixel_uart_h
#define io_ledpixel_uart_h

#include "io.h"
#include "util.h"

#include <stdint.h>
#include <stdbool.h>

bool			io_ledpixel_uart_pre_init(unsigned int io, unsigned int pin);
io_error_t		io_ledpixel_uart_init(const struct io_info_entry_T *);
void			io_ledpixel_uart_post_init(const struct io_info_entry_T *);
unsigned int	io_ledpixel_uart_pin_max_value(const struct io_info_entry_T *info, io_data_pin_entry_t *data, const io_config_pin_entry_t *pin_config, unsigned int pin);
io_error_t		io_ledpixel_uart_init_pin_mode(string_t *error_message, const struct io_info_entry_T *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin);
io_error_t		io_ledpixel_uart_read_pin(string_t *, const struct io_info_entry_T *, io_data_pin_entry_t *, const io_config_pin_entry_t *, int, unsigned int *);
io_error_t		io_ledpixel_uart_write_pin(string_t *, const struct io_info_entry_T *, io_data_pin_entry_t *, const io_config_pin_entry_t *, int, unsigned int);

#endif
