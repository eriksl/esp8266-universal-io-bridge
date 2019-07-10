#include "display.h"
#include "display_orbital.h"
#include "i2c.h"
#include "config.h"

#include <stdint.h>
#include <stdbool.h>

static bool inited = false;

enum
{
	mapeof = 0xffffffff,
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
	{	0x25b2,	0x1f	},	//	▲ 
	{	0x00a5,	0x5c	},	//	¥ 
	{	0x2192, 0x7e	},	//	→ 
	{	0x2190, 0x7f	},	//	← 
	{	0x00c4,	0x80	},	//	Ä
	{	0x0026,	0x81	},	//	Ȧ
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

static unsigned int x, y;

bool display_orbital_init(void)
{
	unsigned int ix, byte;
	const udg_map_t *udg_map_ptr;

	if(!config_flags_match(flag_enable_orbital))
		return(false);

	for(ix = 10; ix > 0; ix--)
	{
		if(i2c_send1(0x28, 0x21) == i2c_error_ok)
			break;
		msleep(20);
	}

	if(ix == 0)
		return(false);

	for(udg_map_ptr = udg_map; udg_map_ptr->unicode != mapeof; udg_map_ptr++)
	{
		if(i2c_send3(0x28, 0xfe, 0x4e, udg_map_ptr->internal) != i2c_error_ok)
			return(false);

		for(byte = 0; byte < 8; byte++)
			if(i2c_send1(0x28, udg_map_ptr->pattern[byte]) != i2c_error_ok)
				return(false);
	}

	inited = true;

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
		if(i2c_send3(0x28, 0xfe, 0x42, 0x00) != i2c_error_ok)
			return(false);
	}
	else
	{
		if(i2c_send2(0x28, 0xfe, 0x46) != i2c_error_ok)
			return(false);
	}

	if(i2c_send3(0x28, 0xfe, 0x59, brightness_map[brightness][1]) != i2c_error_ok)
		return(false);

	return(true);
}

void display_orbital_begin(void)
{
	if(!inited)
		log("! display orbital not inited\n");

	x = y = 0;

	i2c_send2(0x28, 0xfe, 0x44);		// line wrap off
	i2c_send2(0x28, 0xfe, 0x52);		// scroll off
	i2c_send2(0x28, 0xfe, 0x54);		// cursor off
	i2c_send4(0x28, 0xfe, 0x47, 1, 1);	// set position
}

void display_orbital_output(unsigned int unicode)
{
	const unicode_map_t *unicode_map_ptr;
	const udg_map_t *udg_map_ptr;
	bool mapped;

	if(unicode == '\n')
	{
		if(y < 4)
		{
			while(x++ < 20)
				i2c_send1(0x28, ' ');

			if(y < 3)
				i2c_send4(0x28, 0xfe, 0x47, 1, y + 2);	// set position
		}

		x = 0;
		y++;

		return;
	}

	if((y < 4) && (x < 20))
	{
		mapped = false;

		for(unicode_map_ptr = unicode_map; unicode_map_ptr->unicode != mapeof; unicode_map_ptr++)
			if(unicode_map_ptr->unicode == unicode)
			{
				unicode = unicode_map_ptr->internal;
				mapped = true;
				break;
			}

		if(!mapped)
			for(udg_map_ptr = udg_map; udg_map_ptr->unicode != mapeof; udg_map_ptr++)
				if((udg_map_ptr->unicode == unicode))
				{
					unicode = udg_map_ptr->internal;
					mapped = true;
					break;
				}

		if(mapped || ((unicode >= ' ') && (unicode <= '}')))
			i2c_send1(0x28, unicode & 0xff);
		else
			i2c_send1(0x28, ' ');
	}

	x++;
}

void display_orbital_end(void)
{
	if(x > 19)
	{
		x = 0;
		y++;
	}

	for(; y < 4; y++, x = 0)
	{
		i2c_send4(0x28, 0xfe, 0x47, x + 1, y + 1);	// set position

		while(x++ < 20)
			i2c_send1(0x28, ' ');
	}
}
