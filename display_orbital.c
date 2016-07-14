#include "display.h"
#include "display_orbital.h"
#include "i2c.h"

enum
{
	buffer_rows = 4,
	buffer_columns = 20,
	udg_amount = 8,
	udg_byte_amount = 8,
	map_amount = 15,
};

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
	} row[buffer_rows];
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
		0x00e9,	// é	0
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
static uint8_t buffer[buffer_rows][buffer_columns];

irom bool_t display_orbital_init(void)
{
	unsigned int ix, byte, x, y;

	for(ix = 0; ix < udg_amount; ix++)
	{
		if(i2c_send_3(0x28, 0xfe, 0x4e, ix) != i2c_error_ok)
			return(false);

		for(byte = 0; byte < udg_byte_amount; byte++)
			if(i2c_send_1(0x28, udg[ix].pattern[byte]) != i2c_error_ok)
				return(false);
	}

	if(i2c_send_2(0x28, 0xfe, 0x44) != i2c_error_ok) // line wrap off
		return(false);

	if(i2c_send_2(0x28, 0xfe, 0x52) != i2c_error_ok) // scroll off
		return(false);

	if(i2c_send_2(0x28, 0xfe, 0x54) != i2c_error_ok) // cursor off
		return(false);

	inited = true;

	for(y = 0; y < buffer_rows; y++)
		for(x = 0; x < buffer_columns; x++)
			buffer[y][x] = ' ';

	for(ix = 0; ix < buffer_rows; ix++)
		row_status.row[ix].dirty = 1;

	return(display_orbital_bright(1));
}

irom bool_t display_orbital_bright(int brightness)
{
	switch(brightness)
	{
		case(0):
		{
			if(i2c_send_2(0x28, 0xfe, 0x46) != i2c_error_ok)
				return(false);

			if(i2c_send_3(0x28, 0xfe, 0x59, 0x03) != i2c_error_ok)
				return(false);

			break;
		}

		case(1):
		{
			if(i2c_send_3(0x28, 0xfe, 0x42, 0x00) != i2c_error_ok)
				return(false);

			if(i2c_send_3(0x28, 0xfe, 0x59, 0x03) != i2c_error_ok)
				return(false);

			break;
		}

		case(2):
		{
			if(i2c_send_3(0x28, 0xfe, 0x42, 0x00) != i2c_error_ok)
				return(false);

			if(i2c_send_3(0x28, 0xfe, 0x59, 0x02) != i2c_error_ok)
				return(false);

			break;
		}

		case(3):
		{
			if(i2c_send_3(0x28, 0xfe, 0x42, 0x00) != i2c_error_ok)
				return(false);

			if(i2c_send_3(0x28, 0xfe, 0x59, 0x01) != i2c_error_ok)
				return(false);

			break;
		}

		case(4):
		{
			if(i2c_send_3(0x28, 0xfe, 0x42, 0x00) != i2c_error_ok)
				return(false);

			if(i2c_send_3(0x28, 0xfe, 0x59, 0x00) != i2c_error_ok)
				return(false);

			break;
		}

		default:
		{
			return(false);
		}
	}

	return(true);
}

irom bool_t display_orbital_set(const char *tag, const char *text)
{
	unsigned int current, mapped, utf16;
	int y, x, ix;

	if(!inited)
		return(false);

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

	for(ix = 0; ix < buffer_rows; ix++)
		row_status.row[ix].dirty = 1;

	return(true);
}

irom bool_t display_orbital_show(void)
{
	int x, y;

	if(!inited)
		return(false);

	for(y = 0; y < buffer_rows; y++)
		if(row_status.row[y].dirty)
			break;

	if(y >= buffer_rows)
		return(false);

	if(i2c_send_4(0x28, 0xfe, 0x47, 1, y + 1) != i2c_error_ok)
		return(false);

	for(x = 0; x < buffer_columns; x++)
		if(i2c_send_1(0x28, buffer[y][x]) != i2c_error_ok)
			return(false);

	row_status.row[y].dirty = 0;

	return(true);
}
