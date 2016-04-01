#include "display.h"
#include "display_lcd.h"
#include "io.h"
#include "config.h"

enum
{
	udg_amount = 8,
	udg_byte_amount = 8,
};

typedef struct
{
	int	io;
	int pin;
} lcd_io_t;

typedef struct
{
	uint8_t utf8_2;
	uint8_t pattern[udg_byte_amount];
} udg_t;

static const udg_t udg[udg_amount] = 
{
	{
		0xa9,	// é	0
		{
			0b00000100,
			0b00001000,
			0b00001110,
			0b00010001,
			0b00011111,
			0b00010000,
			0b00001110,
			0b00000000,
		}
	},
	{
		0xa8,	// è	1
		{
			0b00001000,
			0b00000100,
			0b00001110,
			0b00010001,
			0b00011111,
			0b00010000,
			0b00001110,
			0b00000000,
		}
	},
	{
		0xaa,	// ê	2
		{
			0b00000100,
			0b00001010,
			0b00001110,
			0b00010001,
			0b00011111,
			0b00010000,
			0b00001110,
			0b00000000,
		}
	},
	{
		0xab,	// ë	3
		{
			0b00001010,
			0b00000000,
			0b00001110,
			0b00010001,
			0b00011111,
			0b00010000,
			0b00001110,
			0b00000000,
		}
	},
	{
		0xbc,	// ü	4
		{
			0b00001010,
			0b00000000,
			0b00010001,
			0b00010001,
			0b00010001,
			0b00010011,
			0b00001101,
			0b00000000,
		}
	},
	{
		0xa7,	// ç	5
		{
			0b00000000,
			0b00000000,
			0b00001110,
			0b00010000,
			0b00010000,
			0b00010101,
			0b00001110,
			0b00000100,
		}
	},
	{
		0xb1,	// ǹ	6
		{
			0b00001000,
			0b00000100,
			0b00010110,
			0b00011001,
			0b00010001,
			0b00010001,
			0b00010001,
			0b00000000,
		}
	},
	{
		0xaf,	// ï	7
		{
			0b00001010,
			0b00000000,
			0b00001100,
			0b00000100,
			0b00000100,
			0b00000100,
			0b00001110,
			0b00000000,
		}
	}
};

static bool inited = false;
static lcd_io_t lcd_io_pins[io_lcd_size];
static char buffer[4][20];

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

irom bool_t display_lcd_init(void)
{
	io_config_pin_entry_t *pin_config;
	int io, pin, ix, byte;

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

	send_byte(0b01000000, false);		// start writing to CGRAM @ 0

	for(ix = 0; ix < udg_amount; ix++)
		for(byte = 0; byte < udg_byte_amount; byte++)
			send_byte(udg[ix].pattern[byte], true);

	inited = true;

	return(true);
}

irom bool_t display_lcd_set(int brightness, const char *tag, const char *text)
{
	int cmd = -1;
	int bl = 0;
	char current;
	int y, x, ix;
	bool utf_two_byte_started;

	if(!inited)
		return(false);

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

	for(y = 0; y < 4; y++)
		for(x = 0; x < 20; x++)
			buffer[y][x] = ' ';

	x = 0;
	y = 0;
	utf_two_byte_started = false;

	for(;;)
	{
		if(tag && ((current = *tag++) == '\0'))
		{
			tag = (char *)0;
			x = 0;
			y = 1;
			utf_two_byte_started = false;
		}

		if(!tag && ((current = *text++) == '\0'))
			break;

		if(utf_two_byte_started)
		{
			if((current & 0b11000000) == 0b10000000) // valid second byte of a two-byte sequence
			{
				for(ix = 0; ix < udg_amount; ix++)
				{
					if(udg[ix].utf8_2 == current)
					{
						current = ix;
						break;
					}
				}
			}

			if(current >= 0x80)
				current = '~';
		}

		if((current & 0b11100000) == 0b11000000) // UTF-8, start of two byte sequence
		{
			utf_two_byte_started = true;
			continue;
		}

		utf_two_byte_started = false;

		if(current == '\r')
		{
			x = 0;

			continue;
		}

		if(current == '\n')
		{
			x = 0;
			tag = (char *)0;

			if(y < 4)
				y++;

			continue;
		}

		if(x < 20)
		{
			buffer[y][x] = current;
			x++;
		}
	}

	if(!send_byte(0x80 + 0x00, false))
		return(false);

	for(x = 0; x < 20; x++)
		if(!send_byte(buffer[0][x], true))
			return(false);

	for(x = 0; x < 20; x++)
		if(!send_byte(buffer[2][x], true))
			return(false);

	if(!send_byte(0x80 + 0x40, false))
		return(false);

	for(x = 0; x < 20; x++)
		if(!send_byte(buffer[1][x], true))
			return(false);

	for(x = 0; x < 20; x++)
		if(!send_byte(buffer[3][x], true))
			return(false);

	return(true);
}
