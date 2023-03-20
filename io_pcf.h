#ifndef io_pcf_h
#define io_pcf_h

#include "util.h"
#include "io.h"

#include <stdint.h>

typedef enum
{
	io_pcf_instance_3a = 0,
	io_pcf_instance_26,
	io_pcf_instance_size
} io_pcf_instance_t;

enum
{
	io_pcf_pin_size = 8,
};

io_error_t		io_pcf_init(const struct io_info_entry_T *);
void			io_pcf_periodic_fast(int io, const struct io_info_entry_T *, io_data_entry_t *, unsigned int rate_ms);
void			io_pcf_pins_changed(uint32_t pin_status_mask, uint16_t pin_value_mask, uint8_t io);
unsigned int	io_pcf_pin_max_value(const struct io_info_entry_T *info, io_data_pin_entry_t *data, const io_config_pin_entry_t *pin_config, unsigned int pin);
io_error_t		io_pcf_init_pin_mode(string_t *, const struct io_info_entry_T *, io_data_pin_entry_t *, const io_config_pin_entry_t *, int);
io_error_t		io_pcf_read_pin(string_t *, const struct io_info_entry_T *, io_data_pin_entry_t *, const io_config_pin_entry_t *, int, unsigned int *);
io_error_t		io_pcf_write_pin(string_t *, const struct io_info_entry_T *, io_data_pin_entry_t *, const io_config_pin_entry_t *, int, unsigned int);
io_error_t		io_pcf_set_mask(string_t *error_message, const struct io_info_entry_T *info, unsigned int mask, unsigned int pins);

#endif
