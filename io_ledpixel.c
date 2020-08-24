#include "io_ledpixel.h"
#include "util.h"
#include "sys_string.h"
#include "uart.h"
#include "i2s.h"
#include "io_gpio.h"

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <attribute.h>

typedef struct attr_packed
{
	unsigned int	enabled:1;
	unsigned int	extended:1;
	unsigned int	grb:1;
	unsigned int	fill8:1;
	unsigned int	value:32;
} ledpixel_data_pin_t;

assert_size(ledpixel_data_pin_t, 5);

typedef union attr_packed
{
	struct
	{
		unsigned int b:5;
		unsigned int g:5;
		unsigned int r:5;
		unsigned int flag:1;
		unsigned int pinmask:16;
	};
	unsigned int value;
} ledpixel_mask_value_t;

assert_size(ledpixel_mask_value_t, 4);

roflash static const unsigned int lut_5_8[32] =
{
	0,		//	0	0x00
	1,		//	1	0x01
	2,		//	2	0x02
	3,		//	3	0x03
	4,		//	4	0x04
	5,		//	5	0x05
	6,		//	6	0x06
	7,		//	7	0x07
	8,		//	8	0x08
	10,		//	9	0x0a
	12,		//	10	0x0c
	15,		//	11	0x0f
	18,		//	12	0x12
	21,		//	13	0x15
	25,		//	14	0x19
	28,		//	15	0x1c
	32,		//	16	0x20
	40,		//	17	0x28
	48,		//	18	0x30
	56,		//	19	0x38
	64,		//	20	0x40
	72,		//	21	0x48
	80,		//	22	0x50
	88,		//	23	0x58
	96,		//	24	0x60
	112,	//	25	0x70
	128,	//	26	0x80
	144,	//	27	0x90
	160,	//	28	0xa0
	192,	//	29	0xc0
	224,	//	30	0xe0
	255,	//	31	0xff
};

static bool	use_uart_0 = false;
static bool	use_uart_1 = false;
static bool	use_i2s = false;

static ledpixel_data_pin_t ledpixel_data_pin[max_pins_per_io];

static unsigned int lookup_5_to_8(unsigned int entry)
{
	if(entry >= sizeof(lut_5_8))
		return(0xff);

	return(lut_5_8[entry]);
}

static void io_ledpixel_value_mask_to_rgb(unsigned int value, unsigned int *pinmask, unsigned int *flag, unsigned int *rgb)
{
	ledpixel_mask_value_t	mask_value;
	unsigned int			r, g, b;

	mask_value.value = value;

	*pinmask = mask_value.pinmask;
	*flag = mask_value.flag;

	r = lookup_5_to_8(mask_value.r);
	g = lookup_5_to_8(mask_value.g);
	b = lookup_5_to_8(mask_value.b);

	*rgb = (r << 16) | (g << 8) | (b << 0);
}

static void send_byte_uart(unsigned int byte)
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

		if(use_uart_0)
			uart_send(0, by_six);

		if(use_uart_1)
			uart_send(1, by_six);
	}
}

static void send_byte_i2s(unsigned int byte_value_in)
{
	static const unsigned int off_pattern = 0b1000;
	static const unsigned int on_pattern = 0b1110;
	uint8_t buffer[4];
	unsigned int bit_in;
	unsigned int byte_out;
	unsigned int pattern;

	for(bit_in = 0, byte_out = 0; (bit_in < 8) && (byte_out < 4); bit_in++)
	{
		pattern = (byte_value_in & (1 << (7 - bit_in))) ? on_pattern : off_pattern;

		if(bit_in & 0x01) // after every second input bit the output nibble is full
		{
			buffer[byte_out] <<= 4;
			buffer[byte_out] |= pattern;
			byte_out++;
		}
		else
			buffer[byte_out] = pattern;
	}

										//	always send at least four bytes at once,
										//	to prevent padding zero bytes from being
	i2s_send(sizeof(buffer), buffer);	//	inserted as i2s uses a fifo of 32 bit words
}

static void send_byte(unsigned int byte)
{
	if(use_i2s)
		send_byte_i2s(byte);

	if(use_uart_0 || use_uart_1)
		send_byte_uart(byte);
}

static void send_all(bool force)
{
	static const uint8_t zero_sample[] = { 0, 0, 0, 0, 0, 0, 0, 0 };
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

		if(use_uart_0)
			uart_flush(0);

		if(use_uart_1)
			uart_flush(1);
	}

	if(use_i2s)
	{
		i2s_send(sizeof(zero_sample), zero_sample); // the last "sample" (4 bytes) get repeated until the transmitter is stopped
		i2s_flush();
	}
}

io_ledpixel_mode_t io_ledpixel_mode(unsigned int io, unsigned int pin)
{
	unsigned int uart;

	if(io != io_id_gpio)
		return(ledpixel_invalid);

	if(io_gpio_get_i2s_from_pin(pin) == gpio_i2s_pin_output_data)
		return(ledpixel_i2s);

	uart = io_gpio_get_uart_from_pin(pin);

	if(uart == 0)
		return(ledpixel_uart_0);

	if(uart == 1)
		return(ledpixel_uart_1);

	return(ledpixel_invalid);
}

bool io_ledpixel_pre_init(unsigned int io, unsigned int pin)
{
	switch(io_ledpixel_mode(io, pin))
	{
		case(ledpixel_i2s):
		{
			use_i2s = true;
			break;
		}

		case(ledpixel_uart_0):
		{
			use_uart_0 = true;
			break;
		}

		case(ledpixel_uart_1):
		{
			use_uart_1 = true;
			break;
		}

		default:
		{
			return(false);
		}
	}

	return(true);
}

io_error_t io_ledpixel_init(const struct io_info_entry_T *info)
{
	if(!use_uart_0 && !use_uart_1 && !use_i2s)
		return(io_error);

	if(use_uart_0)
	{
		uart_baudrate(0, 3200000);
		uart_data_bits(0, 6);
		uart_stop_bits(0, 1);
		uart_parity(0, parity_none);
	}

	if(use_uart_1)
	{
		uart_baudrate(1, 3200000);
		uart_data_bits(1, 6);
		uart_stop_bits(1, 1);
		uart_parity(1, parity_none);
	}

	if(use_i2s && !i2s_init())
		return(io_error);

	return(io_ok);
}

void io_ledpixel_post_init(const struct io_info_entry_T *info)
{
	send_all(true);
}

attr_pure unsigned int io_ledpixel_pin_max_value(const struct io_info_entry_T *info, io_data_pin_entry_t *data, const io_config_pin_entry_t *pin_config, unsigned int pin)
{
	unsigned int value = 0;

	if(pin_config->llmode == io_pin_ll_output_pwm1)
		value = ledpixel_data_pin[pin].extended ? 0xffffffff : 0x00ffffff;

	return(value);
}

io_error_t io_ledpixel_init_pin_mode(string_t *error_message, const struct io_info_entry_T *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin)
{
	ledpixel_data_pin[pin].enabled = pin_config->llmode == io_pin_ll_output_pwm1;
	ledpixel_data_pin[pin].extended = !!(pin_config->flags & io_flag_extended);
	ledpixel_data_pin[pin].grb = !!(pin_config->flags & io_flag_grb);
	ledpixel_data_pin[pin].fill8 = !!(pin_config->flags & io_flag_fill8);
	ledpixel_data_pin[pin].value = 0;

	return(io_ok);
}

io_error_t io_ledpixel_read_pin(string_t *error_message, const struct io_info_entry_T *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin, unsigned int *value)
{
	*value = ledpixel_data_pin[pin].value;

	return(io_ok);
}

io_error_t io_ledpixel_write_pin(string_t *error_message, const struct io_info_entry_T *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin, unsigned int value)
{
	ledpixel_data_pin[pin].value = value;

	send_all(false);

	return(io_ok);
}

io_error_t io_ledpixel_pinmask(unsigned int mask)
{
	unsigned int pin;
	unsigned int pinmask, flag, rgb;

	if(!use_uart_0 && !use_uart_1 && !use_i2s)
		return(io_error);

	io_ledpixel_value_mask_to_rgb(mask, &pinmask, &flag, &rgb);

	for(pin = 0; pin < max_pins_per_io; pin++)
	{
		if(!ledpixel_data_pin[pin].enabled)
			break;

		if(pinmask & (1 << pin))
			ledpixel_data_pin[pin].value = rgb;
	}

	send_all(false);

	return(io_ok);
}
