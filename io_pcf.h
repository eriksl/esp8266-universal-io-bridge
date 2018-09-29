#ifndef io_pcf_h
#define io_pcf_h

#include "util.h"
#include "io.h"

#include <stdint.h>

typedef enum
{
	io_pcf_instance_3a = 0,
	io_pcf_instance_size
} io_pcf_instance_t;

io_error_t		io_pcf_init(const struct io_info_entry_T *);
unsigned int	io_pcf_pin_max_value(const struct io_info_entry_T *info, io_data_pin_entry_t *data, const io_config_pin_entry_t *pin_config, unsigned int pin);
io_error_t		io_pcf_init_pin_mode(string_t *, const struct io_info_entry_T *, io_data_pin_entry_t *, const io_config_pin_entry_t *, int);
io_error_t		io_pcf_read_pin(string_t *, const struct io_info_entry_T *, io_data_pin_entry_t *, const io_config_pin_entry_t *, int, uint32_t *);
io_error_t		io_pcf_write_pin(string_t *, const struct io_info_entry_T *, io_data_pin_entry_t *, const io_config_pin_entry_t *, int, uint32_t);

#endif
