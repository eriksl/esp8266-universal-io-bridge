#include "display.h"
#include "display_cfa634.h"
#include "config.h"
#include "io.h"
#include "uart.h"

static bool_t inited = false;

static const display_map_t cfa634_map[] =
{
	{	0x00c4, 91	},	// Ä
	{	0x00d6, 92	},	// Ö
	{	0x00d1, 93	},	// Ñ
	{	0x00dc, 94	},	// Ü
	{	0x00e4, 123	},	// ä
	{	0x00f6, 124	},	// ö
	{	0x00f1, 125	},	// ñ
	{	0x00fc, 126	},	// ü
	{	0x00e0, 127	},	// à
	{	0x03bc, 143	},	// μ
	{	0x03b1, 156	},	// α
	{	0x03b5, 157	},	// ε
	{	0x03b4, 158 },	// δ
	{	0x0040, 160 },	// @
	{	0x0024, 162 },	// $
	{	0x00e8,	164	}, 	// è
	{	0x00e9,	165	}, 	// é
	{	0x00f9,	166	}, 	// ù
	{	0x00ec,	167	}, 	// ì
	{	0x00f2,	168	}, 	// ò
	{	0x00c7,	169	}, 	// Ç
	{	0x03c4,	179	}, 	// τ
	{	0x03bb,	180	}, 	// λ
	{	0x03a9,	181	}, 	// Ω
	{	0x03c0,	182	}, 	// π
	{	0x03a8,	183	}, 	// Ψ
	{	0x03a3,	184	}, 	// Σ
	{	0x03a6,	185	}, 	// Φ
	{	0x039e,	186	}, 	// Ξ
	{	0x03b2,	190	}, 	// β
	{	0x00c9,	191	}, 	// É
	{	0x0393,	192	}, 	// Γ
	{	0x039b,	193	}, 	// Λ
	{	0x03a0,	194	}, 	// Π
	{	0x00c8,	197	}, 	// È
	{	0x00ca,	198	}, 	// Ê
	{	0x00ea,	199	}, 	// ê
	{	0x00e7,	200	}, 	// ç
	{	0x007e,	206	}, 	// ~
	{	0x00c1,	226	}, 	// Á
	{	0x00d3,	228	}, 	// Ó
	{	0x00da,	229	}, 	// Ú
	{	0x00e1,	231	}, 	// á
	{	0x00ed,	232	}, 	// í
	{	0x00f3,	233	}, 	// ó
	{	0x00fa,	234	}, 	// ú
	{	0x00d4,	236	}, 	// Ô
	{	0x00f4,	237	}, 	// ô
	{	0x005b,	250	}, 	// [
	{	0x005c,	251	}, 	// backslash
	{	0x005d,	252	}, 	// ]
	{	0x007b,	253	}, 	// {
	{	0x007c,	254	}, 	// |
	{	0x007d,	255	}, 	// }
};

static const display_udg_t cfa634_udg[] =
{
	{
		0x00eb,	// ë	0
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
		0x00ef,	// ï	1
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

irom bool_t display_cfa634_init(void)
{
	unsigned int ix, byte, x, y;

	if(!config_flags_get().flag.enable_cfa634)
		return(false);

	if(io_config[0][1].mode != io_pin_uart)
		return(false);

	for(ix = 0; ix < (sizeof(cfa634_udg) / sizeof(*cfa634_udg)); ix++)
	{
		msleep(10);

		uart_send(0, 25);	// send UDG
		uart_send(0, ix);

		for(byte = 0; byte < display_common_udg_byte_size; byte++)
			uart_send(0, display_common_udg[ix].pattern[byte]);

		uart_flush(0);
	}

	inited = true;

	for(y = 0; y < display_common_buffer_rows; y++)
		for(x = 0; x < display_common_buffer_columns; x++)
			display_common_buffer[y][x] = ' ';

	for(ix = 0; ix < display_common_buffer_rows; ix++)
		display_common_row_status.row[ix].dirty = 1;

	return(display_cfa634_bright(1));
}

attr_const irom bool_t display_cfa634_bright(int brightness)
{
	static const unsigned int values[5] = { 0, 55, 65, 70, 75 };

	if((brightness < 0) || (brightness > 4))
		return(false);

	uart_send(0, 15); // set contrast
	uart_send(0, values[brightness]);
	uart_flush(0);

	msleep(10);

	return(true);
}

irom bool_t display_cfa634_set(const char *tag, const char *text)
{
	if(!inited)
		return(false);

	return(display_common_set(tag, text,
				sizeof(cfa634_map) / sizeof(*cfa634_map), cfa634_map,
				sizeof(cfa634_udg) / sizeof(*cfa634_udg), cfa634_udg));
}

irom bool_t display_cfa634_show(void)
{
	int x, y;
	uint8_t c;

	if(!inited)
		return(false);

	for(y = 0; y < display_common_buffer_rows; y++)
		if(display_common_row_status.row[y].dirty)
			break;

	if(y >= display_common_buffer_rows)
		return(false);

	uart_send(0, 3);	// restore blanked display
	uart_send(0, 20);	// scroll off
	uart_send(0, 24);	// wrap off

	uart_send(0, 17);	// goto column,row
	uart_send(0, 0);
	uart_send(0, y);

	uart_flush(0);

	for(x = 0; x < display_common_buffer_columns; x++)
	{
		c = display_common_buffer[y][x];

		if((c < 32) || ((c > 128) && (c < 136)))
		{
			uart_send(0, 30);	// send data directly to LCD controller
			uart_send(0, 1);
		}

		uart_send(0, c);
	}

	uart_flush(0);

	display_common_row_status.row[y].dirty = 0;

	msleep(10);

	return(true);
}
