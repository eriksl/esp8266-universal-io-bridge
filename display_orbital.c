#include "display.h"
#include "display_orbital.h"
#include "i2c.h"
#include "config.h"

#include <stdint.h>
#include <stdbool.h>

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

enum
{
	display_address = 0x28,
	display_text_width = 20,
	display_text_height = 4,

	command_prefix =		0xfe,
	command_timeout =		0x42,
	command_line_wrap_off =	0x44,
	command_display_on =	0x46,
	command_goto =			0x47,
	command_scroll_on =		0x51,
	command_scroll_off =	0x52,
	command_cursor_off =	0x54,
	command_clear_display =	0x58,
	command_brightness =	0x59,
	command_udg =			0x4e,

	mapeof = 0xffffffff,
};

roflash static const unicode_map_t unicode_map[] =
{
	{	0x005c,	0x8c	},	//	backslash
	{	0x007e,	0x8e	},	//	~
	{	0x258f,	0x10	},	//	▏ 
	{	0x258e,	0x11	},	//	▎
	{	0x258d,	0x12	},	//	▍
	{	0x258c,	0x13	},	//	▌
	{	0x258b,	0x14	},	//	▋
	{	0x258c,	0x15	},	//	▌
	{	0x258d,	0x16	},	//	▍
	{	0x258e,	0x17	},	//	▎
	{	0x258f,	0x18	},	//	▏ 
	{	0x266a,	0x19	},	//	♪ 
	{	0x2103,	0x1a	},	//	℃ 
	{	0x2109,	0x1b	},	//	℉ 
	{	0x25bc,	0x1c	},	//	▼ 
	{	0x25b6,	0x1d	},	//	▶ 
	{	0x25c0,	0x1e	},	//	◀ 
	{	0x25b2,	0x1f	},	//	▲ 
	{	0x00a5,	0x5c	},	//	¥ 
	{	0x2192, 0x7e	},	//	→ 
	{	0x2190, 0x7f	},	//	← 
	{	0x00c4,	0x80	},	//	Ä
	{	0x00c2,	0x82	},	//	Â
	{	0x00c5,	0x82	},	//	Å
	{	0x00e1,	0x83	},	//	á
	{	0x00e5,	0x84	},	//	å
	{	0x0395,	0x85	},	//	Ε
	{	0x00d6,	0x86	},	//	Ö
	{	0x00f8,	0x89	},	//	ø
	{	0x00dc,	0x8a	},	//	Ü
	{	0x2260,	0x8d	},	//	≠
	{	0x00a7,	0x8f	},	//	§
	{	0x00c6,	0x90	},	//	Æ
	{	0x00e6,	0x91	},	//	æ
	{	0x00a3,	0x92	},	//	£
	{	0x20a7,	0x93	},	//	₧
	{	0x25cf,	0x94	},	//	● 
	{	0x25cb,	0x95	},	//	○ 
	{	0x25c6,	0x96	},	//	◆ 
	{	0x25c7,	0x97	},	//	◇ 
	{	0x00a6,	0x98	},	//	¦
	{	0x00e7,	0x99	},	//	ç
	{	0x25ff,	0x9a	},	//	◿ 
	{	0x2264,	0x9b	},	//	≤ 
	{	0x2265,	0x9c	},	//	≥ 
	{	0x23ce,	0x9d	},	//	⏎ 
	{	0x2191,	0x9e	},	//	↑ 
	{	0x2193,	0x9f	},	//	↓ 
	{	0x03b3,	0xd8	},	//	γ 
	{	0x00b0, 0xdf	},	//	°
	{	0x03b1, 0xe0	},	//	α
	{	0x00e4, 0xe1	},	//	ä
	{	0x03b2, 0xe2	},	//	β
	{	0x03b5, 0xe3	},	//	ε
	{	0x00b5, 0xe4	},	//	µ
	{	0x03bc, 0xe4	},	//	μ
	{	0x03c3, 0xe5	},	//	σ
	{	0x03c1, 0xe6	},	//	ρ
	{	0x00a2, 0xec	},	//	¢
	{	0x00f1, 0xee	},	//	ñ
	{	0x00f6,	0xef	},	//	ö
	{	0x03b8, 0xf2	},	//	θ
	{	0x03a9, 0xf4	},	//	Ω
	{	0x00fc,	0xf5	},	//	ü
	{	0x03a3, 0xf6	},	//	Σ
	{	0x03c0, 0xf7	},	//	π
	{	0x00f7, 0xfd	},	//	÷

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
		mapeof,	0,	//	EOF
		{
		}
	}
};

static bool display_inited;
static bool display_logmode;
static bool display_scroll_pending;
static unsigned int display_x, display_y;
static unsigned int display_buffer_current;

static bool attr_result_used display_data_flush(void)
{
	if((display_buffer_current > 0) && i2c_send(display_address, display_buffer_current, display_buffer) != i2c_error_ok)
		return(false);

	display_buffer_current = 0;

	return(true);
}

attr_inline bool display_data_output(unsigned int byte)
{
	if(((display_buffer_current + 1) >= display_buffer_size) && !display_data_flush())
		return(false);

	display_buffer[display_buffer_current++] = (uint8_t)byte;

	return(true);
}

attr_inline bool display_command1(unsigned int byte)
{
	if(!display_data_flush() || !display_data_output(command_prefix) || !display_data_output(byte) || !display_data_flush())
		return(false);

	return(true);
}

attr_inline bool display_command2(unsigned int byte1, unsigned int byte2)
{
	if(!display_data_flush() || !display_data_output(command_prefix) || !display_data_output(byte1) || !display_data_output(byte2) || !display_data_flush())
		return(false);

	return(true);
}

attr_inline bool display_command3(unsigned int byte1, unsigned int byte2, unsigned int byte3)
{
	if(!display_data_flush() || !display_data_output(command_prefix) || !display_data_output(byte1) || !display_data_output(byte2) || !display_data_output(byte3) || !display_data_flush())
		return(false);

	return(true);
}

static bool attr_result_used text_goto(int x, int y)
{
	if(x >= 0)
		display_x = x;

	if(y >= 0)
		display_y = y;

	if((display_x >= display_text_width) || (display_y >= display_text_height))
		return(true);

	if(!display_command3(command_goto, display_x + 1, display_y + 1))
		return(false);

	return(true);
}

static bool attr_result_used text_send(unsigned int byte)
{
	if(display_logmode && display_scroll_pending)
	{
		display_scroll_pending = false;

		if(!display_command3(command_goto, display_text_width + 1, display_text_height - 1 + 1))
			return(false);

		if(!display_data_output(' ')) // make display scroll
			return(false);

		if(!text_goto(0, display_text_height - 1))
			return(false);
	}

	if((display_x < display_text_width) && (display_y < display_text_height) && !display_data_output(byte))
		return(false);

	display_x++;

	return(true);
}

static bool attr_result_used text_newline(void)
{
	unsigned int x, y;

	y = display_y + 1;

	if(display_y < display_text_height)
	{
		for(x = display_x; x < display_text_width; x++)
			if(!text_send(' '))
				return(false);
	}

	if(display_logmode && (y >= display_text_height))
	{
		y = display_text_height - 1;
		display_scroll_pending = true;
	}

	if(!text_goto(0, y))
		return(false);

	return(true);
}

bool display_orbital_init(void)
{
	unsigned int ix, byte;
	const udg_map_t *udg_map_ptr;

	if(!config_flags_match(flag_enable_orbital))
		return(false);

	for(ix = 10; ix > 0; ix--)
	{
		if(i2c_send1(display_address, '!') == i2c_error_ok)
			break;
		msleep(20);
	}

	if(ix == 0)
		return(false);

	for(udg_map_ptr = udg_map; udg_map_ptr->unicode != mapeof; udg_map_ptr++)
	{
		if(!display_command2(command_udg, udg_map_ptr->internal))
			return(false);

		for(byte = 0; byte < 8; byte++)
			if(!display_data_output(udg_map_ptr->pattern[byte]))
				return(false);

		if(!display_data_flush())
			return(false);
	}

	if(!display_command1(command_clear_display))
		return(false);

	if(!text_goto(0, 0))
		return(false);

	display_inited = true;

	return(display_orbital_bright(1));
}

bool display_orbital_bright(int brightness)
{
	roflash static const unsigned int brightness_map[5][2] =
	{
		{	0,	3	},
		{	1,	3	},
		{	1,	2	},
		{	1,	1	},
		{	1,	0	},
	};

	if(brightness > 4)
		return(false);

	if(brightness_map[brightness][0])
	{
		if(!display_command2(command_timeout, 0x00))
			return(false);
	}
	else
	{
		if(!display_command1(command_display_on))
			return(false);
	}

	if(!display_command2(command_brightness, brightness_map[brightness][1]))
		return(false);

	return(true);
}

static bool attr_result_used display_setup(void)
{
	if(!display_command1(command_cursor_off))
		return(false);

	if(!display_command1(command_line_wrap_off))
		return(false);

	if(!display_command1(display_logmode ? command_scroll_on : command_scroll_off))
		return(false);

	return(true);
}

bool display_orbital_begin(int slot, bool logmode)
{
	if(!display_inited)
	{
		log("! display orbital not inited\n");
		return(false);
	}

	display_logmode = logmode;

	if(!display_setup())
		return(false);

	if(!text_goto(0, 0))
		return(false);

	return(true);
}

bool display_orbital_output(unsigned int unicode)
{
	const unicode_map_t *unicode_map_ptr;
	const udg_map_t *udg_map_ptr;

	if(unicode == '\n')
		return(text_newline());

	if((display_y < display_text_height) && (display_x < display_text_width))
	{
		for(unicode_map_ptr = unicode_map; unicode_map_ptr->unicode != mapeof; unicode_map_ptr++)
			if(unicode_map_ptr->unicode == unicode)
			{
				unicode = unicode_map_ptr->internal;
				if(!text_send(unicode))
					return(false);
				return(true);
			}

		for(udg_map_ptr = udg_map; udg_map_ptr->unicode != mapeof; udg_map_ptr++)
			if((udg_map_ptr->unicode == unicode))
			{
				unicode = udg_map_ptr->internal;
				if(!text_send(unicode))
					return(false);
				return(true);
			}

		if((unicode < ' ') || (unicode > '}'))
			unicode = ' ';

		if(!text_send(unicode))
			return(false);
	}

	return(true);
}

bool display_orbital_end(void)
{
	while(display_y < display_text_height)
		if(!text_newline())
			break;

	if(!display_data_flush())
		return(false);

	return(true);
}
