#include "display.h"
#include "display_lcd.h"
#include "io.h"
#include "config.h"

enum
{
	buffer_rows = 4,
	buffer_columns = 20,
	buffer_status_rows = buffer_rows + 1,
	udg_amount = 8,
	udg_byte_amount = 8,
	map_amount = 15,
};

typedef struct
{
	int	io;
	int pin;
} lcd_io_t;

typedef struct
{
	uint16_t utf16;
	uint8_t to;
} map_t;

typedef struct
{
	uint16_t utf16;
	uint8_t pattern[udg_byte_amount];
} udg_t;

typedef struct
{
	struct
	{
		unsigned int dirty:1;
	} row[buffer_status_rows];
} row_status_t;

static const map_t map[map_amount] =
{
	{	0x00b0, 0xdf },	// °
	{	0x03b1, 0xe0 },	// α
	{	0x00e4, 0xe1 },	// ä
	{	0x03b2, 0xe2 },	// β
	{	0x03b5, 0xe3 },	// ε
	{	0x03bc, 0xe4 },	// μ
	{	0x03c3, 0xe5 },	// σ
	{	0x03c1, 0xe6 },	// ρ
	{	0x00f1, 0xee },	// ñ
	{	0x00f6, 0xef },	// ö
	{	0x03b8, 0xf2 },	// θ
	{	0x221e, 0xf3 },	// ∞ FIXME: this cannot work with 2-byte UTF-8
	{	0x03a9, 0xf4 },	// Ω
	{	0x03a3, 0xf6 },	// Σ
	{	0x03c0, 0xf7 },	// π
};

static const udg_t udg[udg_amount] = 
{
	{
		0x00e9,		// é	0
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
		0x00e8,	// è	1
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
		0x00ea,	// ê	2
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
		0x00eb,	// ë	3
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
		0x00fc,	// ü	4
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
		0x00e7,	// ç	5
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
		0x20ac,	// €	6 // FIXME: this cannot work with 2-byte UTF-8
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
		0x00ef,	// ï	7
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

static row_status_t row_status;
static bool_t inited = false;
static bool_t nibble_mode;
static int brightness = 0;
static lcd_io_t lcd_io_pin[io_lcd_size];
static uint8_t buffer[buffer_rows][buffer_columns];

irom static bool set_pin(io_lcd_mode_t pin_use, int value)
{
	int io, pin;

	io = lcd_io_pin[pin_use].io;
	pin = lcd_io_pin[pin_use].pin;

	if((io < 0) || (pin < 0))
		return(false);

	if(io_write_pin((string_t *)0, io, pin, value) != io_ok)
		return(false);

	return(true);
}

irom static bool send_byte_raw(int byte, bool data)
{
	if(!set_pin(io_lcd_rs, data))
		return(false);

	set_pin(io_lcd_rw, 0);
	set_pin(io_lcd_d0, !!(byte & (1 << 0)));
	set_pin(io_lcd_d1, !!(byte & (1 << 1)));
	set_pin(io_lcd_d2, !!(byte & (1 << 2)));
	set_pin(io_lcd_d3, !!(byte & (1 << 3)));

	if(!set_pin(io_lcd_d4, !!(byte & (1 << 4))))
		return(false);

	if(!set_pin(io_lcd_d5, !!(byte & (1 << 5))))
		return(false);

	if(!set_pin(io_lcd_d6, !!(byte & (1 << 6))))
		return(false);

	if(!set_pin(io_lcd_d7, !!(byte & (1 << 7))))
		return(false);

	if(!set_pin(io_lcd_e, false))
		return(false);

	if(!set_pin(io_lcd_e, true))
		return(false);

	return(true);
}

irom static bool send_byte(int byte, bool data)
{
	if(nibble_mode)
	{
		if(!send_byte_raw((byte & 0xf0) << 0, data))
			return(false);

		if(!send_byte_raw((byte & 0x0f) << 4, data))
			return(false);

		return(true);
	}

	return(send_byte_raw(byte, data));
}

irom bool_t display_lcd_init(void)
{
	io_config_pin_entry_t *pin_config;
	int io, pin, ix, byte, x, y;

	for(pin = 0; pin < io_lcd_size; pin++)
	{
		lcd_io_pin[pin].io = -1;
		lcd_io_pin[pin].pin = -1;
	}

	for(io = 0; io < io_id_size; io++)
	{
		for(pin = 0; pin < max_pins_per_io; pin++)
		{
			pin_config = &config.io_config[io][pin];

			if(pin_config->mode == io_pin_lcd)
			{
				lcd_io_pin[pin_config->shared.lcd.pin_use].io = io;
				lcd_io_pin[pin_config->shared.lcd.pin_use].pin = pin;
			}
		}
	}

	if((lcd_io_pin[io_lcd_rs].io < 0) || (lcd_io_pin[io_lcd_rs].pin < 0))
		return(false);

	if((lcd_io_pin[io_lcd_e].io < 0) || (lcd_io_pin[io_lcd_e].pin < 0))
		return(false);

	if((lcd_io_pin[io_lcd_d4].io < 0) || (lcd_io_pin[io_lcd_d4].pin < 0))
		return(false);

	if((lcd_io_pin[io_lcd_d5].io < 0) || (lcd_io_pin[io_lcd_d5].pin < 0))
		return(false);

	if((lcd_io_pin[io_lcd_d6].io < 0) || (lcd_io_pin[io_lcd_d6].pin < 0))
		return(false);

	if((lcd_io_pin[io_lcd_d7].io < 0) || (lcd_io_pin[io_lcd_d7].pin < 0))
		return(false);

	nibble_mode = false;

	if((lcd_io_pin[io_lcd_d0].io < 0) || (lcd_io_pin[io_lcd_d0].pin < 0))
		nibble_mode = true;

	if((lcd_io_pin[io_lcd_d1].io < 0) || (lcd_io_pin[io_lcd_d1].pin < 0))
		nibble_mode = true;

	if((lcd_io_pin[io_lcd_d2].io < 0) || (lcd_io_pin[io_lcd_d2].pin < 0))
		nibble_mode = true;

	if((lcd_io_pin[io_lcd_d3].io < 0) || (lcd_io_pin[io_lcd_d3].pin < 0))
		nibble_mode = true;

	// robust initialisation sequence,
	// from http://web.alfredstate.edu/weimandn/lcd/lcd_initialization/lcd_initialization_index.html

	msleep(50);

	for(pin = 3; pin > 0; pin--)
	{
		if(!send_byte_raw(0b00110000, false))	// 3 x special "reset" command, low nibble ignored
			return(false);

		msleep(5);
	}

	if(nibble_mode)
	{
		if(!send_byte_raw(0b00100000, false))	// set 4 bit mode, low nibble ignored
			return(false);

		msleep(2);

		if(!send_byte(0b00101000, false))		// set 4 bit mode / two lines / 5x8 font
			return(false);
	}
	else
		if(!send_byte(0b00111000, false))		// set 8 bit mode / two lines / 5x8 font
			return(false);

	if(!send_byte(0b00000001, false))			// clear screen
		return(false);

	msleep(2);

	if(!send_byte(0b00000110, false))			// cursor move direction = LTR / no display shift
		return(false);

	if(!send_byte(0b00001100, false))			// display on, cursor off, blink off
		return(false);

	if(!send_byte(0b01000000, false))			// start writing to CGRAM @ 0
		return(false);

	for(ix = 0; ix < udg_amount; ix++)
		for(byte = 0; byte < udg_byte_amount; byte++)
			if(!send_byte(udg[ix].pattern[byte], true))
				return(false);

	inited = true;

	for(y = 0; y < buffer_rows; y++)
		for(x = 0; x < buffer_columns; x++)
			buffer[y][x] = ' ';

	for(ix = 0; ix < buffer_status_rows; ix++)
		row_status.row[ix].dirty = 1;

	return(true);
}

irom bool_t display_lcd_set(int brightness_in, const char *tag, const char *text)
{
	unsigned int current, mapped, utf16;
	int y, x, ix;

	if(!inited)
		return(false);

	brightness = brightness_in;

	for(y = 0; y < buffer_rows; y++)
		for(x = 0; x < buffer_columns; x++)
			buffer[y][x] = ' ';

	x = 0;
	y = 0;
	utf16 = 0x00;

	for(;;)
	{
		if(tag && ((current = (uint8_t)*tag++) == '\0'))
		{
			tag = (char *)0;
			x = 0;
			y = 1;
			utf16 = 0x00;
		}

		if(!tag && ((current = (uint8_t)*text++) == '\0'))
			break;

		mapped = ~0UL;

		if(utf16)
		{
			if((current & 0xc0) == 0x80) // valid second byte of a two-byte sequence
			{
				utf16 |= current & 0x3f;

				for(ix = 0; ix < map_amount; ix++)
				{
					if(map[ix].utf16 == utf16)
					{
						mapped = map[ix].to;
						break;
					}
				}

				for(ix = 0; ix < udg_amount; ix++)
				{
					if((udg[ix].utf16 == utf16))
					{
						mapped = ix;
						break;
					}
				}
			}
		}

		utf16 = 0x0000;

		if(mapped != ~0UL)
			current = mapped;
		else
		{
			if((current & 0xe0) == 0xc0) // UTF-8, start of two byte sequence
			{
				utf16 = (current & 0x1f) << 6;
				continue;
			}

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

			if((current < ' ') || (current >= 0x80))
				current = ' ';
		}

		if((y < buffer_rows) && (x < buffer_columns))
			buffer[y][x++] = (uint8_t)(current & 0xff);
	}

	for(ix = 0; ix < buffer_status_rows; ix++)
		row_status.row[ix].dirty = 1;

	return(true);
}

irom bool_t display_lcd_show(void)
{
	int cmd = -1;
	int bl = 0;
	int x;

	if(!inited)
		return(false);

	if(row_status.row[0].dirty)
	{
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
				cmd = 0b00001000;	// display off, cursor off, blink off
				bl = 0;

				break;
			}
		}

		if(!send_byte(cmd, false))
			return(false);

		set_pin(io_lcd_bl, bl);		// backlight pin might be not configured, ignore error

		row_status.row[0].dirty = 0;

		return(true);
	}

	if(row_status.row[1].dirty)
	{
		if(!send_byte(0x80 + 0 + 0, false))
			return(false);

		for(x = 0; x < buffer_columns; x++)
			if(!send_byte(buffer[0][x], true))
				return(false);

		row_status.row[1].dirty = 0;

		return(true);
	}

	if(row_status.row[2].dirty)
	{
		if(!send_byte(0x80 + 0 + 64, false))
			return(false);

		for(x = 0; x < buffer_columns; x++)
			if(!send_byte(buffer[1][x], true))
				return(false);

		row_status.row[2].dirty = 0;

		return(true);
	}

	if(row_status.row[3].dirty)
	{
		if(!send_byte(0x80 + 20 + 0, false))
			return(false);

		for(x = 0; x < buffer_columns; x++)
			if(!send_byte(buffer[2][x], true))
				return(false);

		row_status.row[3].dirty = 0;

		return(true);
	}

	if(row_status.row[4].dirty)
	{
		if(!send_byte(0x80 + 20 + 64, false))
			return(false);

		for(x = 0; x < buffer_columns; x++)
			if(!send_byte(buffer[3][x], true))
				return(false);

		row_status.row[4].dirty = 0;

		return(true);
	}

	return(false);
}
