#include "display.h"
#include "display_lcd.h"
#include "io.h"
#include "config.h"

typedef struct
{
	int	io;
	int pin;
} lcd_io_t;

static lcd_io_t lcd_io_pins[io_lcd_size];
static bool inited = false;
static int current_position;

irom static bool set_pin(io_lcd_mode_t pin_use, int value)
{
	int io, pin;

	io = lcd_io_pins[pin_use].io;
	pin = lcd_io_pins[pin_use].pin;

	if(io_write_pin((string_t *)0, io, pin, value) != io_ok)
		return(false);

	return(true);
}

irom static bool send_nibble(int nibble, bool data)
{
	if(!set_pin(io_lcd_rs, data))
		return(false);

	if(!set_pin(io_lcd_d4, !!(nibble & (1 << 0))))
		return(false);

	if(!set_pin(io_lcd_d5, !!(nibble & (1 << 1))))
		return(false);

	if(!set_pin(io_lcd_d6, !!(nibble & (1 << 2))))
		return(false);

	if(!set_pin(io_lcd_d7, !!(nibble & (1 << 3))))
		return(false);

	if(!set_pin(io_lcd_e, false))
		return(false);

	if(!set_pin(io_lcd_e, true))
		return(false);

	return(true);
}

irom static bool send_byte(int byte, bool data)
{
	if(!send_nibble((byte & 0xf0) >> 4, data))
		return(false);

	if(!send_nibble((byte & 0x0f) >> 0, data))
		return(false);

	return(true);
}

irom static bool write_byte(uint8_t character)
{
	switch(current_position)
	{
		case(0):
		{
			if(!send_byte(0x80 | (0 + 0), false))
				return(false);

			break;
		}

		case(20):
		{
			if(!send_byte(0x80 | (0 + 64), false))
				return(false);

			break;
		}

		case(40):
		{
			if(!send_byte(0x80 | (20 + 0), false))
				return(false);

			break;
		}

		case(60):
		{
			if(!send_byte(0x80 | (20 + 64), false))
				return(false);

			break;
		}

		default:
		{
			break;
		}
	}

	if(!send_byte(character, true))
		return(false);

	if(++current_position > 79)
		current_position = 0;

	return(true);
}

irom bool_t display_lcd_init(void)
{
	io_config_pin_entry_t *pin_config;
	int io, pin;

	for(pin = 0; pin < io_lcd_size; pin++)
	{
		lcd_io_pins[pin].io = -1;
		lcd_io_pins[pin].pin = -1;
	}

	for(io = 0; io < io_id_size; io++)
	{
		for(pin = 0; pin < max_pins_per_io; pin++)
		{
			pin_config = &config.io_config[io][pin];

			if(pin_config->mode == io_pin_lcd)
			{
				lcd_io_pins[pin_config->shared.lcd.pin_use].io = io;
				lcd_io_pins[pin_config->shared.lcd.pin_use].pin = pin;
			}
		}
	}

	for(pin = 0; pin < io_lcd_size; pin++)
	{
		if((lcd_io_pins[pin].io == -1) || (lcd_io_pins[pin].pin == -1))
			return(false);
	}

	for(pin = 2; pin >= 0; pin--)		// set to 4-bit mode trickery
	{									// apparently this needs to be done twice sometimes
		send_nibble(0b0011, false);
		send_nibble(0b0011, false);
		send_nibble(0b0010, false);
	}

	send_byte(0b00101000, false);		// set 4 bit mode / two lines / 5x8 font
	send_byte(0b00000001, false);		// clear screen
	send_byte(0b00000110, false);		// cursor move direction = LTR / no display shift
	send_byte(0b00001100, false);		// display on, cursor off, blink off

	inited = true;
	current_position = 0;

	return(true);
}

irom bool_t display_lcd_set(int brightness, const char *str)
{
	current_position = 0;
	int cmd = -1;
	int bl = 0;

	switch(brightness)
	{
		case(0):
		{
			cmd = 0b00001000;	// display off, cursor off, blink off
			bl = 0;

			break;
		}

		case(1):
		{
			cmd = 0b00001100;	// display on, cursor off, blink off
			bl = 8192;

			break;
		}

		case(2):
		{
			cmd = 0b00001100;	// display on, cursor off, blink off
			bl = 16384;

			break;
		}

		case(3):
		{
			cmd = 0b00001100;	// display on, cursor off, blink off
			bl = 32768;

			break;
		}

		case(4):
		{
			cmd = 0b00001100;	// display on, cursor off, blink off
			bl = 65535;

			break;
		}

		default:
		{
			break;
		}
	}

	if(cmd != -1)
	{
		if(!send_byte(cmd, false))
			return(false);

		if(!set_pin(io_lcd_bl, bl) != io_ok)
			return(false);
	}

	for(; *str; str++)
		if(!write_byte(*str))
			return(false);

	return(true);
}
