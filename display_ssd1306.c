#include "display.h"
#include "display_ssd1306.h"
#include "i2c.h"
#include "config.h"
#include "sys_time.h"
#include "dispatch.h"
#include "display_font_6x8.h"

#include <stdint.h>
#include <stdbool.h>

typedef struct
{
	unsigned int	unicode;
	unsigned int	internal;
} unicode_map_t;

enum
{
	display_width = 128,
	display_height_32 = 32,
	display_height_64 = 64,
	display_text_width = 21,
	display_font_width = 6,
	display_font_height = 8,

	mapeof = 0xffffffff,
};

enum
{
	display_address = 0x3c,

	display_control_command =	0b00000000,
	display_control_data =		0b01000000,

	reg_set_current_col_low =	0x00,
	reg_set_current_col_high =	0x10,
	reg_address_mode =			0x20,
	reg_set_col_conf =			0x21,
	reg_set_page_conf =			0x22,
	reg_deactivate_scroll =		0x2e,
	reg_activeate_scroll =		0x2f,
	reg_display_start_line =	0x40,
	reg_contrast =				0x81,
	reg_charge_pump =			0x8d,
	reg_remap_segment_off =		0xa0,
	reg_remap_segment_on =		0xa1,
	reg_test_mode_off =			0xa4,
	reg_test_mode_on =			0xa5,
	reg_display_normal =		0xa6,
	reg_display_inverse =		0xa7,
	reg_mux_ratio =				0xa8,
	reg_display_off =			0xae,
	reg_display_on =			0xaf,
	reg_set_current_page =		0xb0,
	reg_remap_common_off =		0xc0,
	reg_remap_common_on =		0xc8,
	reg_start_line =			0xd3,
	reg_set_oscillator =		0xd5,
	reg_pin_layout =			0xda,

	reg_pin_layout_normal =		0x02,
	reg_pin_layout_alt =		0x12,
	reg_address_mode_hor =		0b00000000,
	reg_address_mode_vert =		0b00000001,
	reg_address_mode_page =		0b00000010,
	reg_set_oscillator_default=	0b10000000,
	reg_charge_pump_off =		0b00010000,
	reg_charge_pump_on =		0b00010100,
};

roflash static const unicode_map_t unicode_map[] =
{
    {   0x00c7, 0x80    },  //  #LATIN CAPITAL LETTER C WITH CEDILLA
    {   0x00fc, 0x81    },  //  #LATIN SMALL LETTER U WITH DIAERESIS
    {   0x00e9, 0x82    },  //  #LATIN SMALL LETTER E WITH ACUTE
    {   0x00e2, 0x83    },  //  #LATIN SMALL LETTER A WITH CIRCUMFLEX
    {   0x00e4, 0x84    },  //  #LATIN SMALL LETTER A WITH DIAERESIS
    {   0x00e0, 0x85    },  //  #LATIN SMALL LETTER A WITH GRAVE
    {   0x00e5, 0x86    },  //  #LATIN SMALL LETTER A WITH RING ABOVE
    {   0x00e7, 0x87    },  //  #LATIN SMALL LETTER C WITH CEDILLA
    {   0x00ea, 0x88    },  //  #LATIN SMALL LETTER E WITH CIRCUMFLEX
    {   0x00eb, 0x89    },  //  #LATIN SMALL LETTER E WITH DIAERESIS
    {   0x00e8, 0x8a    },  //  #LATIN SMALL LETTER E WITH GRAVE
    {   0x00ef, 0x8b    },  //  #LATIN SMALL LETTER I WITH DIAERESIS
    {   0x00ee, 0x8c    },  //  #LATIN SMALL LETTER I WITH CIRCUMFLEX
    {   0x00ec, 0x8d    },  //  #LATIN SMALL LETTER I WITH GRAVE
    {   0x00c4, 0x8e    },  //  #LATIN CAPITAL LETTER A WITH DIAERESIS
    {   0x00c5, 0x8f    },  //  #LATIN CAPITAL LETTER A WITH RING ABOVE
    {   0x00c9, 0x90    },  //  #LATIN CAPITAL LETTER E WITH ACUTE
    {   0x00e6, 0x91    },  //  #LATIN SMALL LIGATURE AE
    {   0x00c6, 0x92    },  //  #LATIN CAPITAL LIGATURE AE
    {   0x00f4, 0x93    },  //  #LATIN SMALL LETTER O WITH CIRCUMFLEX
    {   0x00f6, 0x94    },  //  #LATIN SMALL LETTER O WITH DIAERESIS
    {   0x00f2, 0x95    },  //  #LATIN SMALL LETTER O WITH GRAVE
    {   0x00fb, 0x96    },  //  #LATIN SMALL LETTER U WITH CIRCUMFLEX
    {   0x00f9, 0x97    },  //  #LATIN SMALL LETTER U WITH GRAVE
    {   0x00ff, 0x98    },  //  #LATIN SMALL LETTER Y WITH DIAERESIS
    {   0x00d6, 0x99    },  //  #LATIN CAPITAL LETTER O WITH DIAERESIS
    {   0x00dc, 0x9a    },  //  #LATIN CAPITAL LETTER U WITH DIAERESIS
    {   0x00a2, 0x9b    },  //  #CENT SIGN
    {   0x00a3, 0x9c    },  //  #POUND SIGN
    {   0x00a5, 0x9d    },  //  #YEN SIGN
    {   0x20a7, 0x9e    },  //  #PESETA SIGN
    {   0x0192, 0x9f    },  //  #LATIN SMALL LETTER F WITH HOOK
    {   0x00e1, 0xa0    },  //  #LATIN SMALL LETTER A WITH ACUTE
    {   0x00ed, 0xa1    },  //  #LATIN SMALL LETTER I WITH ACUTE
    {   0x00f3, 0xa2    },  //  #LATIN SMALL LETTER O WITH ACUTE
    {   0x00fa, 0xa3    },  //  #LATIN SMALL LETTER U WITH ACUTE
    {   0x00f1, 0xa4    },  //  #LATIN SMALL LETTER N WITH TILDE
    {   0x00d1, 0xa5    },  //  #LATIN CAPITAL LETTER N WITH TILDE
    {   0x00aa, 0xa6    },  //  #FEMININE ORDINAL INDICATOR
    {   0x00ba, 0xa7    },  //  #MASCULINE ORDINAL INDICATOR
    {   0x00bf, 0xa8    },  //  #INVERTED QUESTION MARK
    {   0x2310, 0xa9    },  //  #REVERSED NOT SIGN
    {   0x00ac, 0xaa    },  //  #NOT SIGN
    {   0x00bd, 0xab    },  //  #VULGAR FRACTION ONE HALF
    {   0x00bc, 0xac    },  //  #VULGAR FRACTION ONE QUARTER
    {   0x00a1, 0xad    },  //  #INVERTED EXCLAMATION MARK
    {   0x00ab, 0xae    },  //  #LEFT-POINTING DOUBLE ANGLE QUOTATION MARK
    {   0x00bb, 0xaf    },  //  #RIGHT-POINTING DOUBLE ANGLE QUOTATION MARK
    {   0x2591, 0xb0    },  //  #LIGHT SHADE
    {   0x2592, 0xb1    },  //  #MEDIUM SHADE
    {   0x2593, 0xb2    },  //  #DARK SHADE
    {   0x2502, 0xb3    },  //  #BOX DRAWINGS LIGHT VERTICAL
    {   0x2524, 0xb4    },  //  #BOX DRAWINGS LIGHT VERTICAL AND LEFT
    {   0x2561, 0xb5    },  //  #BOX DRAWINGS VERTICAL SINGLE AND LEFT DOUBLE
    {   0x2562, 0xb6    },  //  #BOX DRAWINGS VERTICAL DOUBLE AND LEFT SINGLE
    {   0x2556, 0xb7    },  //  #BOX DRAWINGS DOWN DOUBLE AND LEFT SINGLE
    {   0x2555, 0xb8    },  //  #BOX DRAWINGS DOWN SINGLE AND LEFT DOUBLE
    {   0x2563, 0xb9    },  //  #BOX DRAWINGS DOUBLE VERTICAL AND LEFT
    {   0x2551, 0xba    },  //  #BOX DRAWINGS DOUBLE VERTICAL
    {   0x2557, 0xbb    },  //  #BOX DRAWINGS DOUBLE DOWN AND LEFT
    {   0x255d, 0xbc    },  //  #BOX DRAWINGS DOUBLE UP AND LEFT
    {   0x255c, 0xbd    },  //  #BOX DRAWINGS UP DOUBLE AND LEFT SINGLE
    {   0x255b, 0xbe    },  //  #BOX DRAWINGS UP SINGLE AND LEFT DOUBLE
    {   0x2510, 0xbf    },  //  #BOX DRAWINGS LIGHT DOWN AND LEFT
    {   0x2514, 0xc0    },  //  #BOX DRAWINGS LIGHT UP AND RIGHT
    {   0x2534, 0xc1    },  //  #BOX DRAWINGS LIGHT UP AND HORIZONTAL
    {   0x252c, 0xc2    },  //  #BOX DRAWINGS LIGHT DOWN AND HORIZONTAL
    {   0x251c, 0xc3    },  //  #BOX DRAWINGS LIGHT VERTICAL AND RIGHT
    {   0x2500, 0xc4    },  //  #BOX DRAWINGS LIGHT HORIZONTAL
    {   0x253c, 0xc5    },  //  #BOX DRAWINGS LIGHT VERTICAL AND HORIZONTAL
    {   0x255e, 0xc6    },  //  #BOX DRAWINGS VERTICAL SINGLE AND RIGHT DOUBLE
    {   0x255f, 0xc7    },  //  #BOX DRAWINGS VERTICAL DOUBLE AND RIGHT SINGLE
    {   0x255a, 0xc8    },  //  #BOX DRAWINGS DOUBLE UP AND RIGHT
    {   0x2554, 0xc9    },  //  #BOX DRAWINGS DOUBLE DOWN AND RIGHT
    {   0x2569, 0xca    },  //  #BOX DRAWINGS DOUBLE UP AND HORIZONTAL
    {   0x2566, 0xcb    },  //  #BOX DRAWINGS DOUBLE DOWN AND HORIZONTAL
    {   0x2560, 0xcc    },  //  #BOX DRAWINGS DOUBLE VERTICAL AND RIGHT
    {   0x2550, 0xcd    },  //  #BOX DRAWINGS DOUBLE HORIZONTAL
    {   0x256c, 0xce    },  //  #BOX DRAWINGS DOUBLE VERTICAL AND HORIZONTAL
    {   0x2567, 0xcf    },  //  #BOX DRAWINGS UP SINGLE AND HORIZONTAL DOUBLE
    {   0x2568, 0xd0    },  //  #BOX DRAWINGS UP DOUBLE AND HORIZONTAL SINGLE
    {   0x2564, 0xd1    },  //  #BOX DRAWINGS DOWN SINGLE AND HORIZONTAL DOUBLE
    {   0x2565, 0xd2    },  //  #BOX DRAWINGS DOWN DOUBLE AND HORIZONTAL SINGLE
    {   0x2559, 0xd3    },  //  #BOX DRAWINGS UP DOUBLE AND RIGHT SINGLE
    {   0x2558, 0xd4    },  //  #BOX DRAWINGS UP SINGLE AND RIGHT DOUBLE
    {   0x2552, 0xd5    },  //  #BOX DRAWINGS DOWN SINGLE AND RIGHT DOUBLE
    {   0x2553, 0xd6    },  //  #BOX DRAWINGS DOWN DOUBLE AND RIGHT SINGLE
    {   0x256b, 0xd7    },  //  #BOX DRAWINGS VERTICAL DOUBLE AND HORIZONTAL SINGLE
    {   0x256a, 0xd8    },  //  #BOX DRAWINGS VERTICAL SINGLE AND HORIZONTAL DOUBLE
    {   0x2518, 0xd9    },  //  #BOX DRAWINGS LIGHT UP AND LEFT
    {   0x250c, 0xda    },  //  #BOX DRAWINGS LIGHT DOWN AND RIGHT
    {   0x2588, 0xdb    },  //  #FULL BLOCK
    {   0x2584, 0xdc    },  //  #LOWER HALF BLOCK
    {   0x258c, 0xdd    },  //  #LEFT HALF BLOCK
    {   0x2590, 0xde    },  //  #RIGHT HALF BLOCK
    {   0x2580, 0xdf    },  //  #UPPER HALF BLOCK
    {   0x03b1, 0xe0    },  //  #GREEK SMALL LETTER ALPHA
    {   0x00df, 0xe1    },  //  #LATIN SMALL LETTER SHARP S
    {   0x0393, 0xe2    },  //  #GREEK CAPITAL LETTER GAMMA
    {   0x03c0, 0xe3    },  //  #GREEK SMALL LETTER PI
    {   0x03a3, 0xe4    },  //  #GREEK CAPITAL LETTER SIGMA
    {   0x03c3, 0xe5    },  //  #GREEK SMALL LETTER SIGMA
    {   0x00b5, 0xe6    },  //  #MICRO SIGN
    {   0x03c4, 0xe7    },  //  #GREEK SMALL LETTER TAU
    {   0x03a6, 0xe8    },  //  #GREEK CAPITAL LETTER PHI
    {   0x0398, 0xe9    },  //  #GREEK CAPITAL LETTER THETA
    {   0x03a9, 0xea    },  //  #GREEK CAPITAL LETTER OMEGA
    {   0x03b4, 0xeb    },  //  #GREEK SMALL LETTER DELTA
    {   0x221e, 0xec    },  //  #INFINITY
    {   0x03c6, 0xed    },  //  #GREEK SMALL LETTER PHI
    {   0x03b5, 0xee    },  //  #GREEK SMALL LETTER EPSILON
    {   0x2229, 0xef    },  //  #INTERSECTION
    {   0x2261, 0xf0    },  //  #IDENTICAL TO
    {   0x00b1, 0xf1    },  //  #PLUS-MINUS SIGN
    {   0x2265, 0xf2    },  //  #GREATER-THAN OR EQUAL TO
    {   0x2264, 0xf3    },  //  #LESS-THAN OR EQUAL TO
    {   0x2320, 0xf4    },  //  #TOP HALF INTEGRAL
    {   0x2321, 0xf5    },  //  #BOTTOM HALF INTEGRAL
    {   0x00f7, 0xf6    },  //  #DIVISION SIGN
    {   0x2248, 0xf7    },  //  #ALMOST EQUAL TO
    {   0x00b0, 0xf8    },  //  #DEGREE SIGN
    {   0x2219, 0xf9    },  //  #BULLET OPERATOR
    {   0x00b7, 0xfa    },  //  #MIDDLE DOT
    {   0x221a, 0xfb    },  //  #SQUARE ROOT
    {   0x207f, 0xfc    },  //  #SUPERSCRIPT LATIN SMALL LETTER N
    {   0x00b2, 0xfd    },  //  #SUPERSCRIPT TWO
    {   0x25a0, 0xfe    },  //  #BLACK SQUARE
    {   0x00a0, 0xff    },  //  #NO-BREAK SPACE
	{	mapeof,	0x00	},	// EOF
};

static bool display_inited = false;
static bool display_logmode = false;
static bool display_standout = false;
static bool display_disable_text = false;
static bool display_picture_valid = false;
static unsigned int display_height;
static unsigned int display_x, display_y;
static unsigned int display_buffer_current;
static unsigned int display_picture_load_flash_sector;

attr_result_used attr_inline unsigned int text_height(void)
{
	if(config_flags_match(flag_ssd_height_32))
		return(4);
	else
		if(display_logmode)
			return(8);
		else
			return(4);
}

static attr_result_used bool send_command1(unsigned int cmd)
{
	if(i2c_send2(display_address, display_control_command, cmd) != i2c_error_ok)
		return(false);

	return(true);
}

static attr_result_used bool send_command2(unsigned int cmd1, unsigned int cmd2)
{
	if(i2c_send3(display_address, display_control_command, cmd1, cmd2) != i2c_error_ok)
		return(false);

	return(true);
}

static attr_result_used bool display_data_flush(void)
{
	if((display_buffer_current > 1) && i2c_send(display_address, display_buffer_current, display_buffer) != i2c_error_ok)
		return(false);

	display_buffer_current = 0;
	display_buffer[display_buffer_current++] = display_control_data;

	return(true);
}

attr_inline attr_result_used bool display_data_output(unsigned int byte)
{
	if(((display_buffer_current + 1) >= display_buffer_size) && !display_data_flush())
		return(false);

	display_buffer[display_buffer_current++] = (uint8_t)byte;

	return(true);
}

static attr_result_used bool display_cursor_row_column(unsigned int row, unsigned int column)
{
	if(!display_data_flush())
		return(false);

	if(!send_command1(reg_set_current_page | (row & 0x07)))
		return(false);

	if(!send_command1(reg_set_current_col_low | ((column & 0x0f) >> 0)))
		return(false);

	if(!send_command1(reg_set_current_col_high | ((column & 0xf0) >> 4)))
		return(false);

	return(true);
}

static attr_result_used bool display_cursor(unsigned int x, unsigned int y)
{
	unsigned int row, column;

	if(!display_data_flush())
		return(false);

	row = y * display_font_height / 8;
	column = x * display_font_width;

	if(!display_cursor_row_column(row, column))
		return(false);

	return(true);
}

static attr_result_used bool text_goto(int x, int y)
{
	if(x >= 0)
		display_x = x;

	if(y >= 0)
		display_y = y;

	x = display_x;
	y = display_y;

	if(((unsigned int)x >= display_text_width) || ((unsigned int)y >= text_height()))
		return(true);

	if(!config_flags_match(flag_ssd_height_32) && !display_logmode)
		y *= 2;

	if(!display_cursor(x, y))
		return(false);

	return(true);
}

static attr_result_used bool text_send(unsigned int byte)
{
	const unsigned int *font_6x8 = (const unsigned int *)(const void *)display_font_6x8;
	const unsigned int *font_entry;
	unsigned int run, column, dot, output;
	unsigned int font_line[8];

	if((display_x >= display_text_width) || (display_y >= text_height()))
		goto text_invisible;

	font_entry = &font_6x8[byte * 8 /*sizeof(entry)*/ / 4 /*sizeof(int)*/];

	font_line[0] = (font_entry[1] & 0xff000000) >> 24;
	font_line[1] = (font_entry[1] & 0x00ff0000) >> 16;
	font_line[2] = (font_entry[1] & 0x0000ff00) >> 8;
	font_line[3] = (font_entry[1] & 0x000000ff) >> 0;
	font_line[4] = (font_entry[0] & 0xff000000) >> 24;
	font_line[5] = (font_entry[0] & 0x00ff0000) >> 16;
	font_line[6] = (font_entry[0] & 0x0000ff00) >> 8;
	font_line[7] = (font_entry[0] & 0x000000ff) >> 0;

	if(display_logmode || config_flags_match(flag_ssd_height_32))
	{
		for(column = 0; column < 6; column++)
		{
			for(dot = 0, output = 0; dot < 8; dot++)
				if(font_line[dot] & (1 << (6 - column)))
					output |= 1 << (7 - dot);

			if(display_standout)
				output ^= 0xff;

			if(!display_data_output(output))
				return(false);
		}
	}
	else
	{
		for(run = 0; run < 2; run++)
		{
			if(!display_cursor(display_x, (display_y * 2) + run))
				return(false);

			for(column = 0; column < 6; column++)
			{
				for(dot = 0, output = 0; dot < 8; dot++)
					if(font_line[(dot / 2) + ((1 - run) * 4)] & (1 << (6 - column)))
						output |= 1 << (7 - dot);

				if(display_standout)
					output ^= 0xff;

				if(!display_data_output(output))
					return(false);
			}
		}

		if(!display_cursor(display_x, (display_y * 2) + 0))
			return(false);
	}

text_invisible:
	display_x++;

	return(true);
}

static attr_result_used bool text_newline(void)
{
	unsigned int x, y;

	if(display_logmode)
	{
		y = (display_y + 1) % text_height();
		if(!text_goto(0, y))
			return(false);
	}
	else
		y = display_y + 1;

	if(display_y < text_height())
		for(x = display_x; x < display_text_width; x++)
			if(!text_send(' '))
				return(false);

	if(!text_goto(0, y))
		return(false);

	return(true);
}

static attr_result_used bool clear_screen(void)
{
	static const unsigned int chunk_size = 32;
	unsigned int row, chunk;

	if(!display_data_flush())
		return(false);

	if(display_buffer_size < (chunk_size + 1))
		return(false);

	memset(display_buffer + 1, 0, chunk_size);

	for(row = 0; row < 8; row++)
	{
		if(!display_cursor_row_column(row, 0))
			return(false);

		for(chunk = 0; chunk < (display_width / chunk_size); chunk++)
			if(i2c_send(display_address, chunk_size + 1, display_buffer) != i2c_error_ok)
				return(false);
	}

	return(true);
}

static bool bright(int brightness)
{
	roflash static const unsigned int bright_to_internal[5][2] =
	{
		{	reg_display_off,	0,	},
		{	reg_display_on,		20,	},
		{	reg_display_on,		40,	},
		{	reg_display_on,		80	},
		{	reg_display_on,		255	},
	};

	if(brightness > 4)
		return(false);

	if(!send_command1(bright_to_internal[brightness][0]))
		return(false);

	if(!send_command2(reg_contrast, bright_to_internal[brightness][1]))
		return(false);

	return(true);
}

static bool init(void)
{
	display_height = config_flags_match(flag_ssd_height_32) ? display_height_32 : display_height_64;

	if(!send_command1(reg_display_off))
		return(false);

	if(!send_command2(reg_mux_ratio, display_height - 1))
		return(false);

	if(!send_command2(reg_start_line, 0))
		return(false);

	if(!send_command1(reg_display_start_line))
		return(false);

	if(!send_command1(reg_remap_segment_on))
		return(false);

	if(!send_command1(reg_remap_common_on))
		return(false);

	if(!send_command2(reg_pin_layout, config_flags_match(flag_ssd_height_32) ? reg_pin_layout_normal : reg_pin_layout_alt))
		return(false);

	if(!send_command2(reg_address_mode, reg_address_mode_page))
		return(false);

	if(!text_goto(0, 0))
		return(false);

	if(!send_command1(reg_display_normal))
		return(false);

	if(!send_command1(reg_deactivate_scroll))
		return(false);

	if(!send_command2(reg_charge_pump, reg_charge_pump_on))
		return(false);

	if(!send_command1(reg_test_mode_off))
		return(false);

	if(!send_command1(reg_display_on))
		return(false);

	if(!clear_screen())	// this also inits the display buffer
		return(false);

	display_inited = true;

	return(bright(1));
}

static bool begin(unsigned int select_slot, bool logmode)
{
	if(!display_inited)
	{
		log("! display ssd1306 not inited\n");
		return(false);
	}

	if(display_disable_text)
		return(true);

	display_logmode = logmode;
	if(!text_goto(0, 0))
		return(false);

	return(true);
}

static bool output(unsigned int length, const unsigned int unicode[])
{
	const unicode_map_t *unicode_map_ptr;
	unsigned int current_index, current;

	if(display_disable_text)
		return(true);

	for(current_index = 0; current_index < length; current_index++)
	{
		current = unicode[current_index];

		if(current == '\n')
		{
			if(!text_newline())
				return(false);

			continue;
		}

		if((display_y >= text_height()) || (display_x >= display_text_width))
			return(true);

		for(unicode_map_ptr = unicode_map; unicode_map_ptr->unicode != mapeof; unicode_map_ptr++)
			if(unicode_map_ptr->unicode == current)
			{
				if(!text_send(unicode_map_ptr->internal))
					return(false);

				continue;
			}

		if((current < ' ') || (current > '}'))
			current = ' ';

		if(!text_send(current))
			return(false);
	}

	return(true);
}

static bool end(void)
{
	if(display_disable_text)
		return(true);

	while(display_y < text_height())
		if(!text_newline())
			break;

	if(!display_data_flush())
		return(false);

	return(true);
}

static bool standout(bool onoff)
{
	display_standout = onoff;

	return(true);
}

static const char pbm_header[] = "P4\n128 64\n";

static bool picture_load(unsigned int picture_load_index)
{
	bool success;
	string_t *buffer_string;
	char *buffer_cstr;
	unsigned int size;

	success = false;

	flash_buffer_request(fsb_display_picture, false, "lcd picture load", &buffer_string, &buffer_cstr, &size);

	if(!buffer_string)
		goto error2;

	display_picture_load_flash_sector = (picture_load_index ? PICTURE_FLASH_OFFSET_1 : PICTURE_FLASH_OFFSET_0) / SPI_FLASH_SEC_SIZE;

	if(spi_flash_read(display_picture_load_flash_sector * size, buffer_cstr, size) != SPI_FLASH_RESULT_OK)
	{
		log("display ssd1306: load picture: failed to read sector: 0x%x\n", display_picture_load_flash_sector);
		goto error1;
	}

	string_setlength(buffer_string, sizeof(pbm_header) - 1);

	if(!string_match_cstr(buffer_string, pbm_header))
	{
		log("display ssd1306: show picture: invalid image header\n");
		goto error1;
	}

	success = true;

error1:
	flash_buffer_release(fsb_display_picture, "seeed picture load");
error2:
	display_picture_valid = success;
	return(success);
}

static bool picture_valid(void)
{
	return(display_picture_valid);
}

static bool layer_select(unsigned int layer)
{
	bool success = false;
	unsigned int row, column, output, bit, offset, bitoffset;
	const uint8_t *bitmap;
	string_t *buffer_string;
	char *buffer_cstr;
	unsigned int size;

	if(layer == 0)
	{
		display_disable_text = false;
		return(true);
	}

	if(!display_picture_valid)
	{
		log("display ssd1306: load picture: invalid image\n");
		goto error2;
	}

	display_disable_text = true;

	flash_buffer_request(fsb_display_picture, false, "ssd layer select", &buffer_string, &buffer_cstr, &size);

	if(!buffer_string)
		goto error2;

	if(spi_flash_read(display_picture_load_flash_sector * size, buffer_cstr, size) != SPI_FLASH_RESULT_OK)
	{
		log("display ssd1306: load picture: failed to read sector: 0x%x\n", display_picture_load_flash_sector);
		goto error1;
	}

	string_setlength(buffer_string, size);
	bitmap = (const uint8_t *)buffer_cstr + (sizeof(pbm_header) - 1);

	for(row = 0; row < (display_height / 8); row++)
	{
		if(!display_cursor_row_column(row, 0))
			goto error1;

		for(column = 0; column < display_width; column++)
		{
			output = 0;

			for(bit = 0; bit < 8; bit++)
			{
				offset		= (row * 8) * (display_width / 8) * (2 / (display_height / display_height_32));
				offset		+= bit * (display_width / 8);
				offset		+= column / 8;
				bitoffset	= column % 8;

				if(bitmap[offset] & (1 << (7 - bitoffset)))
					output |= 1 << bit;
			}

			if(!display_data_output(output))
				goto error1;
		}
	}

	success = true;
error1:
	flash_buffer_release(fsb_display_picture, "ssd layer select");
error2:
	if(!display_data_flush())
		success = false;
	return(success);
}

roflash const display_info_t display_info_ssd1306 =
{
	{
		"SSD1306 / SH1106 OLED",
		{ 21, 4 },
		{ 128, 64 },
		1,
	},
	{
		init,
		begin,
		output,
		end,
		bright,
		standout,
		(void *)0,
		picture_load,
		layer_select,
		(void *)0,
		(void *)0,
		(void *)0,
		(void *)0,
		(void *)0,
		(void *)0,
		(void *)0,
		picture_valid,
	}
};
