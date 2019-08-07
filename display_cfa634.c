#include "display.h"
#include "display_cfa634.h"
#include "config.h"
#include "io.h"
#include "io_gpio.h"
#include "uart.h"

#include <stdint.h>
#include <stdbool.h>

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

static bool		detected = false;
static unsigned int uart;

roflash static const unicode_map_t unicode_map[] =
{
	{	0x0024,	0xa2	},	//	$
	{	0x0040,	0xa0	},	//	@
	{	0x005b,	0xfa	},	//	[
	{	0x005c,	0xfb	},	//	backslash
	{	0x005d,	0xfc	},	//	]
	{	0x005e,	0x0d	},	//	^
	{	0x005f,	0xc4	},	//	_
	{	0x0060,	0x87	},	//	`
	{	0x007b,	0xfd	},	//	{
	{	0x007c,	0xfe	},	//	|
	{	0x007d,	0xff	},	//	}
	{	0x007e,	0xce	},	//	~
	{	0x007f,	0xd6	},	//	del
	{	0x25b6,	0x10	},	//	▶ 
	{	0x25c0,	0x11	},	//	◀ 
	{	0x00ab,	0x14	},	//	«
	{	0x00bb,	0x15	},	//	»
	{	0x2196,	0x16	},	//	↖ 
	{	0x2197,	0x17	},	//	↗ 
	{	0x2199,	0x18	},	//	↙ 
	{	0x2198,	0x19	},	//	↘ 
	{	0x25b2,	0x1a	},	//	▲ 
	{	0x25bc,	0x1b	},	//	▼ 
	{	0x23ce,	0x1c	},	//	⏎ 
	{	0x258b,	0x1f	},	//	▋
	{	0x00a4,	0x24	},	//	¤ 
	{	0x00a1,	0x40	},	//	¡
	{	0x00c4,	0x5b	},	//	Ä
	{	0x00d6, 0x5c	},	//	Ö
	{	0x00d1, 0x5d	},	//	Ñ
	{	0x00dc, 0x5e	},	//	Ü
	{	0x00a7,	0x5f	},	//	§
	{	0x00bf,	0x60	},	//	¿
	{	0x00e4, 0x7b	},	//	ä
	{	0x00f6, 0x7c	},	//	ö
	{	0x00f1, 0x7d	},	//	ñ
	{	0x00fc, 0x7e	},	//	ü
	{	0x00e0, 0x7f	},	//	à
	{	0x2070, 0x80	},	//	⁰
	{	0x00b9, 0x81	},	//	¹
	{	0x00b2, 0x82	},	//	²
	{	0x00b3, 0x83	},	//	³
	{	0x2074, 0x84	},	//	⁴
	{	0x2075, 0x85	},	//	⁵
	{	0x2076, 0x86	},	//	⁶
	{	0x2077, 0x87	},	//	⁷
	{	0x2078, 0x88	},	//	⁸
	{	0x2079, 0x89	},	//	⁹
	{	0x00bd,	0x8a	},	//	½
	{	0x00bc,	0x8b	},	//	¼
	{	0x00b1,	0x8c	},	//	±
	{	0x2265,	0x8d	},	//	≥ 
	{	0x2264,	0x8e	},	//	≤ 
	{	0x00b5, 0x8f	},	//	µ
	{	0x03bc, 0x8f	},	//	μ
	{	0x266a,	0x90	},	//	♪ 
	{	0x266b,	0x91	},	//	♫ 
	{	0x1f514,0x92	},	//	🔔 
	{	0x1f56d,0x92	},	//	🕭 
	{	0x2665,	0x93	},	//	♥ 
	{	0x25c6,	0x94	},	//	◆ 
	{	0x1f4f6,0x95	},	//	📶
	{	0x201c,	0x98	},	//	“
	{	0x201d,	0x99	},	//	”
	{	0x03b1, 0x9c	},	//	α
	{	0x03b5, 0x9d	},	//	ε
	{	0x03b4, 0x9e	},	//	δ
	{	0x221e, 0x9f	},	//	∞
	{	0x00a3,	0xa1	},	//	£
	{	0x00a5,	0xa3	},	//	¥
	{	0x00e8,	0xa4	}, 	//	è
	{	0x00e9,	0xa5	}, 	//	é
	{	0x00f9,	0xa6	}, 	//	ù
	{	0x00ec,	0xa7	}, 	//	ì
	{	0x00f2,	0xa8	}, 	//	ò
	{	0x00e7,	0xa9	}, 	//	ç
	{	0x00d8,	0xab	}, 	//	Ø
	{	0x00f8,	0xac	}, 	//	ø
	{	0x0226,	0xae	}, 	//	Ȧ
	{	0x0227,	0xaf	}, 	//	ȧ
	{	0x00a2, 0xb1	},	//	¢
	{	0x03c4, 0xb3	},	//	τ
	{	0x03bb, 0xb4	},	//	λ
	{	0x03a9, 0xb5	},	//	Ω
	{	0x03c0,	0xb6	}, 	//	π
	{	0x03a8,	0xb7	}, 	//	Ψ
	{	0x03a3,	0xb8	}, 	//	Σ
	{	0x03b8,	0xb9	}, 	//	θ
	{	0x039e,	0xba	}, 	//	Ξ
	{	0x25cf,	0xbb	},	//	● 
	{	0x00c6,	0xbc	},	//	Æ
	{	0x00e6,	0xbd	},	//	æ
	{	0x03b2,	0xbe	}, 	//	β
	{	0x00c9,	0xbf	}, 	//	É
	{	0x0393,	0xc0	}, 	//	Γ
	{	0x039b,	0xc1	}, 	//	Λ
	{	0x03a0,	0xc2	}, 	//	Π
	{	0x03d2,	0xc3	}, 	//	ϒ
	{	0x00c8,	0xc5	}, 	//	È
	{	0x00ca,	0xc6	}, 	//	Ê
	{	0x00ea,	0xc7	}, 	//	ê
	{	0x00e7,	0xc8	}, 	//	ç
	{	0x25c7,	0xcf	},	//	◇ 
	{	0x258b,	0xd0	},	//	▋
	{	0x258c,	0xd1	},	//	▌
	{	0x258d,	0xd2	},	//	▍
	{	0x258e,	0xd3	},	//	▎
	{	0x258f,	0xd4	},	//	▏ 
	{	0x258b,	0xd6	},	//	▋
	{	0x258c,	0xd7	},	//	▌
	{	0x258d,	0xd8	},	//	▍
	{	0x258e,	0xd9	},	//	▎
	{	0x258f,	0xda	},	//	▏ 
	{	0x20a7,	0xdb	},	//	₧
	{	0x2191,	0xde	},	//	↑ 
	{	0x2192,	0xdf	},	//	→ 
	{	0x2193,	0xe0	},	//	↓ 
	{	0x2190,	0xe1	},	//	←
	{	0x00c1,	0xe2	}, 	//	Á
	{	0x00cd,	0xe3	}, 	//	Í
	{	0x00d3,	0xe4	}, 	//	Ó
	{	0x00da,	0xe5	}, 	//	Ú
	{	0x00e1,	0xe7	}, 	//	á
	{	0x00ed,	0xe8	}, 	//	í
	{	0x00f3,	0xe9	}, 	//	ó
	{	0x00fa,	0xea	}, 	//	ú
	{	0x00d4,	0xec	}, 	//	Ô
	{	0x00f4,	0xed	}, 	//	ô
	{	mapeof, 0xff	},	//	EOF
};

roflash static const udg_map_t udg_map[] =
{
	{	0x00eb,	0,	// ë
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
	{	0x00ef,	1,	// ï
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
	},
	{	0x03c3,	2,	// σ
		{
			0b00000000,
			0b00000011,
			0b00001110,
			0b00010001,
			0b00010001,
			0b00001110,
			0b00000000,
			0b00000000,
		}
	},
	{	0x03c1,	3,	// ρ
		{
			0b00000000,
			0b00000000,
			0b00001110,
			0b00010001,
			0b00011110,
			0b00010000,
			0b00010000,
			0b00000000,
		}
	},
	{
		0x00a9,	4,	//	©
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
		0x20ac,	5,	//	€
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
	{	mapeof,	6,	// EOF,
		{ },
	}
};

bool display_cfa634_setup(unsigned int io, unsigned int pin)
{
	if((io != io_id_gpio) || (pin >= max_pins_per_io))
		return(false);

	uart = io_gpio_get_uart_from_pin(pin);

	if((uart != 0) && (uart != 1))
		return(false);

	detected = true;

	return(true);
}

bool display_cfa634_init(void)
{
	const udg_map_t *map;
	unsigned int ix, byte;

	if(!detected)
		return(false);

	uart_baudrate(uart, 19200);
	uart_data_bits(uart, 8);
	uart_stop_bits(uart, 1);
	uart_parity(uart, parity_none);

	for(map = udg_map, ix = 0; map->unicode != mapeof; map++, ix++)
	{
		uart_send(uart, 25);	// send UDG
		uart_send(uart, map->internal);

		uart_flush(uart);
		msleep(1);

		for(byte = 0; byte < 8; byte++)
		{
			uart_send(uart, map->pattern[byte] & 0xff);
			uart_flush(uart);
			msleep(1);
		}
	}

	return(display_cfa634_bright(1));
}

attr_const bool display_cfa634_bright(int brightness)
{
	static const unsigned int values[5] = { 0, 55, 65, 70, 75 };

	if(!detected)
		return(false);

	if((brightness < 0) || (brightness > 4))
		return(false);

	uart_send(uart, 15); // set contrast
	uart_send(uart, values[brightness]);
	uart_flush(uart);

	return(true);
}

static unsigned int x, y;

void display_cfa634_begin(int slot, unsigned int slot_offset, bool wrapround)
{
	uart_send(uart, 3);	// restore blanked display
	uart_send(uart, 20);	// scroll off
	uart_send(uart, 24);	// wrap off

	uart_send(uart, 17);	// goto column,row
	uart_send(uart, 0);
	uart_send(uart, 0);

	x = y = 0;
}

void display_cfa634_output(unsigned int unicode)
{
	const unicode_map_t *unicode_map_ptr;
	const udg_map_t *udg_map_ptr;
	bool mapped;

	if(unicode == '\n')
	{
		if(y < 4)
		{
			while(x++ < 20)
				uart_send(uart, ' ');

			if(y < 3)
			{
				uart_send(uart, 17);	// goto column,row
				uart_send(uart, 0);
				uart_send(uart, y + 1);
			}
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

		if((unicode >= ' ') && (unicode <= '}'))
			uart_send(uart, unicode & 0xff);
		else
		{
			if(!mapped)
				uart_send(uart, ' ');
			else
			{
				if((unicode < ' ') || ((unicode >= 128) && (unicode <= 135)))
				{
					uart_send(uart, 30);	// prefix for sending data directly to LCD controller
					uart_send(uart, 1);
				}
				uart_send(uart, unicode & 0xff);
			}
		}
	}

	x++;
}

void display_cfa634_end(void)
{
	if(x > 19)
	{
		x = 0;
		y++;
	}

	for(; y < 4; y++, x = 0)
	{
		uart_send(uart, 17);	// goto column,row
		uart_send(uart, x);
		uart_send(uart, y);

		while(x++ < 20)
			uart_send(uart, ' ');
	}

	uart_flush(uart);
}
