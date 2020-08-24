#ifndef io_ledpixel_h
#define io_ledpixel_h

#include <stdint.h>
#include <stdbool.h>
#include <io.h>

typedef enum
{
	ledpixel_invalid,
	ledpixel_uart_0,
	ledpixel_uart_1,
	ledpixel_i2s
} io_ledpixel_mode_t;

io_ledpixel_mode_t	io_ledpixel_mode(unsigned int io, unsigned int pin);
bool				io_ledpixel_pre_init(unsigned int io, unsigned int pin);
io_error_t			io_ledpixel_init(const struct io_info_entry_T *);
void				io_ledpixel_post_init(const struct io_info_entry_T *);
unsigned int		io_ledpixel_pin_max_value(const struct io_info_entry_T *info, io_data_pin_entry_t *data,
						const io_config_pin_entry_t *pin_config, unsigned int pin);
io_error_t			io_ledpixel_init_pin_mode(string_t *error_message, const struct io_info_entry_T *info,
						io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin);
io_error_t			io_ledpixel_read_pin(string_t *, const struct io_info_entry_T *, io_data_pin_entry_t *, const io_config_pin_entry_t *, int, unsigned int *);
io_error_t			io_ledpixel_write_pin(string_t *, const struct io_info_entry_T *, io_data_pin_entry_t *, const io_config_pin_entry_t *, int, unsigned int);
io_error_t			io_ledpixel_pinmask(unsigned int mask);

#endif
