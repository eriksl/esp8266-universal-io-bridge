#include "display.h"
#include "display_seeed.h"
#include "i2c.h"
#include "config.h"
#include "sys_time.h"

#include <stdint.h>
#include <stdbool.h>

static bool inited = false;
static unsigned int x, y, slot_offset;

enum
{
	mapeof = 0xffffffff,
};

enum
{
	reg_FontModeRegAddr =			0x01,
	reg_CharXPosRegAddr =			0x02,
	reg_CharYPosRegAddr =			0x03,
	reg_CursorConfigRegAddr =		0x16,
	reg_DisRAMAddr =				0x20,
	reg_WriteRAM_XPosRegAddr =		0x23,
	reg_WriteRAM_YPosRegAddr =		0x24,
	reg_DisplayConfigRegAddr =		0x80,
	reg_WorkingModeRegAddr =		0x81,
	reg_BackLightConfigRegAddr =	0x82,
	reg_ContrastConfigRegAddr =		0x83,
};

enum
{
	font_6x8 =		0x00,
	font_6x12 =		0x01,
	font_8x16_1 =	0x02,
	font_8x16_2 =	0x03,
	font_10x20 =	0x04,
	font_12x24 =	0x05,
	font_16_32 =	0x06,
};

enum
{
	render_autonl_autoincr =		0b00000000,
	render_autonl_manualincr =		0b00010000,
	render_manualnl_autoincr =		0b00100000,
	render_manualnl_manualincr =	0b00110000,
};

enum
{
	charmode_white_inc_background =		0b00000000,
	charmode_white_excl_background =	0b01000000,
	charmode_black_inc_background =		0b10000000,
	charmode_black_excl_background =	0b11000000,
};

enum
{
	cursor_off =	0b00000000,
	cursor_on =		0b10000000,
};

enum
{
	display_mode_reverse =	0b00000000,
	display_mode_normal =	0b10000000,
};

enum
{
	workmode_char =				0b00000000,
	workmode_bitmap =			0b00000001,
	workmode_ram =				0b00000010,
	workmode_backlight_off =	0b00000000,
	workmode_backlight_on =		0b00000100,
	workmode_logo_off =			0b00000000,
	workmode_logo_on =			0b00001000,
	workmode_flip =				0b01000000,
	workmode_extra =			0b01010000,
};

typedef struct
{
	unsigned int	unicode;
	unsigned int	internal;
} unicode_map_t;

typedef struct
{
	unsigned int	unicode;
	unsigned int	internal;
	unsigned int	pattern[8];
} udg_map_t;

roflash static const unicode_map_t unicode_map[] =
{
	{	0x2190, 0x1c	},	//	← 
	{	0x2192, 0x1d	},	//	→ 
	{	0x2191,	0x1e	},	//	↑ 
	{	0x2193,	0x1f	},	//	↓ 
	{	mapeof,	0x00 }, // EOF
};

roflash static const udg_map_t udg_map[] =
{
	{
		0x00e8,	0,	//	è
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
		0x00e9,	1,	//	é
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
		0x00eb,	2,	//	ë
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
		0x00f4,	3,	//	ô
		{
			0b00000100,
			0b00001010,
			0b00001110,
			0b00010001,
			0b00010001,
			0b00010001,
			0b00001110,
			0b00000000,
		}
	},
	{
		0x03b4,	4,	//	δ
		{
			0b00001111,
			0b00001000,
			0b00000100,
			0b00001110,
			0b00010001,
			0b00010001,
			0b00001110,
			0b00000000,
		}
	},
	{
		0x03bb,	5,	//	λ
		{
			0b00000000,
			0b00010000,
			0b00011000,
			0b00001100,
			0b00001010,
			0b00010001,
			0b00010001,
			0b00000000,
		}
	},
	{
		0x00a9,	6,	//	©
		{
			0b00000100,
			0b00001010,
			0b00010111,
			0b00011001,
			0b00010111,
			0b00001010,
			0b00000100,
			0b00000000,
		}
	},
	{
		0x20ac,	7,	//	€
		{
			0b00000011,
			0b00000100,
			0b00011111,
			0b00000100,
			0b00011111,
			0b00000100,
			0b00000011,
			0b00000000,
		}
	},
	{
		mapeof, 0, // EOF
		{ },
	},
};

static unsigned int text_current = 0;
static uint8_t text_buffer[32];

static void text_flush(void)
{
	uint8_t i2c_buffer[sizeof(text_buffer) + 1];

	i2c_buffer[0] = reg_DisRAMAddr;
	memcpy(i2c_buffer + 1, text_buffer, text_current);

	uint64_t start = time_get_us();
	i2c_send(0x51, text_current + 1, i2c_buffer);
	stat_debug_2 = time_get_us() - start;

	text_current = 0;
}

static void text_send(unsigned int text)
{
	if((text_current + 1) >= sizeof(text_buffer))
		text_flush();

	text_buffer[text_current++] = (uint8_t)text;
}

static void text_goto(unsigned int slot_in, unsigned int x_in, unsigned int y_in)
{
	text_flush();
	i2c_send3(0x51, reg_CharXPosRegAddr, x_in * 6, (slot_in * 4 * 8) + (y_in * 8));
}

static void udg_send(unsigned int udg)
{
	uint8_t ram_byte[9];
	unsigned int byte, bit;
	unsigned int gx, gy;

	for(byte = 0; byte < sizeof(ram_byte); byte++)
		ram_byte[byte] = 0;

	ram_byte[0] = reg_DisRAMAddr;

	for(byte = 0; byte < 8; byte++)
		for(bit = 0; bit < 6; bit++)
			if(udg_map[udg].pattern[byte] & (1 << (5 - bit)))
				ram_byte[bit + 1] |= 1 << byte;

	gx = x * 6;
	gy = (slot_offset * 4) + y;

	if(gx > 0)
		gx--;

	text_send(' ');
	text_flush();
	i2c_send2(0x51, reg_WorkingModeRegAddr, workmode_extra | workmode_ram | workmode_backlight_on | workmode_logo_off);
	i2c_send3(0x51, reg_WriteRAM_XPosRegAddr, gx, gy);
	i2c_send(0x51, 7, ram_byte);
	i2c_send2(0x51, reg_WorkingModeRegAddr, workmode_extra | workmode_char | workmode_backlight_on | workmode_logo_off);
}

bool display_seeed_init(void)
{
	if(!display_seeed_inverse(0))
		return(false);

	if(!display_seeed_bright(0))
		return(false);

	if(i2c_send2(0x51, reg_CursorConfigRegAddr, cursor_off | 0) != i2c_error_ok)
		return(false);

	if(i2c_send2(0x51, reg_DisplayConfigRegAddr, display_mode_normal) != i2c_error_ok)
		return(false);

	if(i2c_send2(0x51, reg_WorkingModeRegAddr, workmode_extra | workmode_char | workmode_backlight_on | workmode_logo_off) != i2c_error_ok)
		return(false);

	for(slot_offset = 0; slot_offset < 2; slot_offset++)
		for(y = 0; y < 4; y++)
		{
			text_goto(slot_offset, 0, y);

			for(x = 0; x < 21; x++)
				text_send(' ');
		}

	text_flush();

	inited = true;

	return(display_seeed_bright(1));
}

void display_seeed_begin(int select_slot, unsigned int select_slot_offset)
{
	if(!inited)
		log("! display seeed not inited\n");

	x = y = 0;
	slot_offset = select_slot_offset;

	msleep(1);
	text_goto(slot_offset, 0, 0);
	msleep(1);
}

void display_seeed_output(unsigned int unicode)
{
	const unicode_map_t *unicode_map_ptr;
	const udg_map_t *udg_map_ptr;

	if(unicode == '\n')
	{
		if(y < 4)
		{
			while(x++ < 21)
				text_send(' ');

			if(y < 3)
				text_goto(slot_offset, 0, y + 1);
		}

		x = 0;
		y++;

		return;
	}

	if((y < 4) && (x < 21))
	{
		for(unicode_map_ptr = unicode_map; unicode_map_ptr->unicode != mapeof; unicode_map_ptr++)
			if(unicode_map_ptr->unicode == unicode)
			{
				unicode = unicode_map_ptr->internal;
				text_send(unicode);
				goto end;
			}

		for(udg_map_ptr = udg_map; udg_map_ptr->unicode != mapeof; udg_map_ptr++)
			if((udg_map_ptr->unicode == unicode))
			{
				udg_send(udg_map_ptr->internal);
				goto end;
			}

		if((unicode >= ' ') && (unicode <= '}'))
			text_send(unicode);
		else
			text_send(' ');
	}

end:
	x++;
}

void display_seeed_end(void)
{
	if(x > 19)
	{
		x = 0;
		y++;
	}

	for(; y < 4; y++, x = 0)
	{
		text_goto(slot_offset, x, y);

		while(x++ < 21)
			text_send(' ');
	}

	text_flush();
}

bool display_seeed_bright(int brightness)
{
	roflash static const unsigned int bright_to_internal[5][2] =
	{
		{	0,	0	},
		{	20,	20	},
		{	20,	40	},
		{	20,	80	},
		{	20,	127	},
	};

	if(brightness > 4)
		return(false);

	if(i2c_send2(0x51, reg_ContrastConfigRegAddr, bright_to_internal[brightness][0]) != i2c_error_ok)
		return(false);

	if(i2c_send2(0x51, reg_BackLightConfigRegAddr, bright_to_internal[brightness][1]) != i2c_error_ok)
		return(false);

	return(true);
}

bool display_seeed_inverse(bool onoff)
{
	unsigned int value = render_manualnl_autoincr | font_6x8 | (onoff ? charmode_white_inc_background : charmode_black_inc_background);

	return(i2c_send2(0x51, reg_FontModeRegAddr, value) == i2c_error_ok);
}
