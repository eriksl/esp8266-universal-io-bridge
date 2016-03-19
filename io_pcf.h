#ifndef io_pcf_h
#define io_pcf_h

#include "util.h"
#include "io_shared.h"

#include <stdint.h>

typedef enum
{
	io_pcf_instance_3a = 0,
	io_pcf_instance_size
} io_pcf_instance_t;

io_error_t	io_pcf_init(const struct io_info_entry_T *);
io_error_t	io_pcf_init_pin_mode(string_t *, const struct io_info_entry_T *, io_data_pin_entry_t *, const io_config_pin_entry_t *, int);
io_error_t	io_pcf_read_pin(string_t *, const struct io_info_entry_T *, io_data_pin_entry_t *, const io_config_pin_entry_t *, int, int *);
io_error_t	io_pcf_write_pin(string_t *, const struct io_info_entry_T *, io_data_pin_entry_t *, const io_config_pin_entry_t *, int, int);

#endif
