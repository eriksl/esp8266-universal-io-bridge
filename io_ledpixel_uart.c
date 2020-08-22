#include "io_ledpixel_uart.h"
#include "io_ledpixel.h"
#include "i2c.h"
#include "util.h"
#include "sys_string.h"
#include "uart.h"
#include "io_gpio.h"

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

static bool			detected = false;
static unsigned int	uart;

static ledpixel_data_pin_t ledpixel_data_pin[max_pins_per_io];

#if 0
static unsigned int simulate_uart(unsigned int in)
{
	unsigned int reversed = 0;
	unsigned int bit;

	for(bit = 0; bit < 6; bit++)
	{
		reversed <<= 1;
		reversed |= in & (1 << bit) ? 0b1 : 0b0;
	}

	reversed <<= 1;
	reversed |= 0x01;

	return(reversed ^ 0xff);
}
#endif

static void send_byte(unsigned int byte)
{
	// from an idea by nodemcu coders: https://github.com/nodemcu/nodemcu-firmware/blob/master/app/modules/ws2812.c

	static const unsigned int bit_pattern[4] =
	{				//		mirror		add start/stop	negate
		0b110111,	//	00	111-011		[0]111-011[1]	1000-1000
		0b000111,	//	01	111-000		[0]111-000[1]	1000-1110
		0b110100,	//	10	001-011		[0]001-011[1]	1110-1000
		0b000100,	//	11	001-000		[0]001-000[1]	1110-1110
	};

	unsigned int byte_bit_index;
	unsigned int by_six = 0;

	for(byte_bit_index = 0; byte_bit_index < 4; byte_bit_index++)
	{
		by_six = bit_pattern[(byte & 0b11000000) >> 6];
		byte <<= 2;
		uart_send(uart, by_six);
	}
}

static void send_all(bool force)
{
	unsigned int pin, fill;

	for(pin = 0; pin < max_pins_per_io; pin++)
	{
		if(!force && !ledpixel_data_pin[pin].enabled)
			break;

		for(fill = ledpixel_data_pin[pin].fill8 ? 8 : 1; fill > 0; fill--)
		{
			if(ledpixel_data_pin[pin].grb)
			{
				send_byte((ledpixel_data_pin[pin].value & 0x0000ff00) >>   8);
				send_byte((ledpixel_data_pin[pin].value & 0x00ff0000) >>  16);
			}
			else
			{
				send_byte((ledpixel_data_pin[pin].value & 0x00ff0000) >>  16);
				send_byte((ledpixel_data_pin[pin].value & 0x0000ff00) >>   8);
			}

			send_byte((ledpixel_data_pin[pin].value & 0x000000ff) >>  0);

			// some ws2812's have four leds (including a white one) and need an extra byte to be sent for it

			if(ledpixel_data_pin[pin].extended)
				send_byte((ledpixel_data_pin[pin].value & 0xff000000) >>  24);
		}

		uart_flush(uart);
	}
}

bool io_ledpixel_uart_pre_init(unsigned int io, unsigned int pin)
{
	if(io != io_id_gpio)
		return(false);

	uart = io_gpio_get_uart_from_pin(pin);

	if((uart != 0) && (uart != 1))
		return(false);

	detected = true;

	return(true);
}

io_error_t io_ledpixel_uart_init(const struct io_info_entry_T *info)
{
	if(!detected)
		return(io_error);

	uart_baudrate(uart, 3200000);
	uart_data_bits(uart, 6);
	uart_stop_bits(uart, 1);
	uart_parity(uart, parity_none);

	return(io_ok);
}

void io_ledpixel_uart_post_init(const struct io_info_entry_T *info)
{
	send_all(true);
}

attr_pure unsigned int io_ledpixel_uart_pin_max_value(const struct io_info_entry_T *info, io_data_pin_entry_t *data, const io_config_pin_entry_t *pin_config, unsigned int pin)
{
	unsigned int value = 0;

	if(pin_config->llmode == io_pin_ll_output_pwm1)
		value = ledpixel_data_pin[pin].extended ? 0xffffffff : 0x00ffffff;

	return(value);
}

io_error_t io_ledpixel_uart_init_pin_mode(string_t *error_message, const struct io_info_entry_T *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin)
{
	ledpixel_data_pin[pin].enabled = pin_config->llmode == io_pin_ll_output_pwm1;
	ledpixel_data_pin[pin].extended = !!(pin_config->flags & io_flag_extended);
	ledpixel_data_pin[pin].grb = !!(pin_config->flags & io_flag_grb);
	ledpixel_data_pin[pin].fill8 = !!(pin_config->flags & io_flag_fill8);
	ledpixel_data_pin[pin].value = 0;

	return(io_ok);
}

io_error_t io_ledpixel_uart_read_pin(string_t *error_message, const struct io_info_entry_T *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin, unsigned int *value)
{
	*value = ledpixel_data_pin[pin].value;

	return(io_ok);
}

io_error_t io_ledpixel_uart_write_pin(string_t *error_message, const struct io_info_entry_T *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin, unsigned int value)
{
	ledpixel_data_pin[pin].value = value;

	send_all(false);

	return(io_ok);
}
