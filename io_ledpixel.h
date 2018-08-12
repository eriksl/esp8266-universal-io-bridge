#ifndef io_ledpixel_h
#define io_ledpixel_h

#include "io.h"
#include "util.h"

void		io_ledpixel_setup(unsigned int pin_in, unsigned int uart);
io_error_t	io_ledpixel_init(const struct io_info_entry_T *);
void		io_ledpixel_periodic(int io, const struct io_info_entry_T *, io_data_entry_t *, io_flags_t *);
io_error_t	io_ledpixel_init_pin_mode(string_t *error_message, const struct io_info_entry_T *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin);
io_error_t	io_ledpixel_read_pin(string_t *, const struct io_info_entry_T *, io_data_pin_entry_t *, const io_config_pin_entry_t *, int, uint32_t *);
io_error_t	io_ledpixel_write_pin(string_t *, const struct io_info_entry_T *, io_data_pin_entry_t *, const io_config_pin_entry_t *, int, uint32_t);

#endif
