#include "io_ledpixel_i2s.h"
#include "io_ledpixel.h"
#include "util.h"
#include "sys_string.h"
#include "i2s.h"
#include "io_gpio.h"

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

static bool detected = false;

static ledpixel_data_pin_t ledpixel_data_pin[max_pins_per_io];

// from an idea by Charles Lohr: https://raw.githubusercontent.com/cnlohr/esp8266ws2812i2s/master/user/ws2812_i2s.c

static void send_byte(unsigned int byte_value_in)
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

	i2s_send(sizeof(buffer), buffer);
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
	}

	i2s_send(sizeof(zero_sample), zero_sample); // the last "sample" (4 bytes) get repeated until the transmitter is stopped
	i2s_flush();
}

bool io_ledpixel_i2s_pre_init(unsigned int io, unsigned int pin)
{
	if(io != io_id_gpio)
		return(false);

	if(io_gpio_get_i2s_from_pin(pin) != gpio_i2s_pin_output_data)
		return(false);

	detected = true;

	return(true);
}

io_error_t io_ledpixel_i2s_init(const struct io_info_entry_T *info)
{
	if(!detected)
		return(io_error);

	if(!i2s_init())
	{
		detected = false;
		return(io_error);
	}

	return(io_ok);
}

void io_ledpixel_i2s_post_init(const struct io_info_entry_T *info)
{
	send_all(true);
}

attr_pure unsigned int io_ledpixel_i2s_pin_max_value(const struct io_info_entry_T *info, io_data_pin_entry_t *data, const io_config_pin_entry_t *pin_config, unsigned int pin)
{
	unsigned int value = 0;

	if(pin_config->llmode == io_pin_ll_output_pwm1)
		value = ledpixel_data_pin[pin].extended ? 0xffffffff : 0x00ffffff;

	return(value);
}

io_error_t io_ledpixel_i2s_init_pin_mode(string_t *error_message, const struct io_info_entry_T *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin)
{
	ledpixel_data_pin[pin].enabled = pin_config->llmode == io_pin_ll_output_pwm1;
	ledpixel_data_pin[pin].extended = !!(pin_config->flags & io_flag_extended);
	ledpixel_data_pin[pin].grb = !!(pin_config->flags & io_flag_grb);
	ledpixel_data_pin[pin].fill8 = !!(pin_config->flags & io_flag_fill8);
	ledpixel_data_pin[pin].value = 0;

	return(io_ok);
}

io_error_t io_ledpixel_i2s_read_pin(string_t *error_message, const struct io_info_entry_T *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin, unsigned int *value)
{
	*value = ledpixel_data_pin[pin].value;

	return(io_ok);
}

io_error_t io_ledpixel_i2s_write_pin(string_t *error_message, const struct io_info_entry_T *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin, unsigned int value)
{
	ledpixel_data_pin[pin].value = value;

	send_all(false);

	return(io_ok);
}
