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

extern const io_info_entry_t io_info_entry_ledpixel;

io_error_t			io_ledpixel_pinmask(unsigned int mask);
io_ledpixel_mode_t	io_ledpixel_mode(unsigned int io, unsigned int pin);
bool				io_ledpixel_pre_init(unsigned int io, unsigned int pin);

#endif
