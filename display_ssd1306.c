#include "display.h"
#include "display_ssd1306.h"
#include "i2c.h"
#include "config.h"
#include "sys_time.h"
#include "dispatch.h"
#include "display_font_6x8.h"

#include <stdint.h>
#include <stdbool.h>

typedef enum attr_packed
{
	dm_text,
	dm_graphic,
} display_mode_t;

enum
{
	display_width = 128,
	display_height_32 = 32,
	display_height_64 = 64,
	display_text_width = 21,
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

static bool display_inited = false;
static bool display_logmode = false;
static bool display_standout = false;
static unsigned int display_height;
static unsigned int display_x, display_y;
static unsigned int display_buffer_current;

static display_mode_t display_mode;
assert_size(display_mode, 1);

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

	display_mode = dm_text;
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

	if(display_mode != dm_text)
	{
		if(!clear_screen())
			return(false);

		display_mode = dm_text;
	}

	display_logmode = logmode;
	if(!text_goto(0, 0))
		return(false);

	return(true);
}

static bool output(unsigned int length, const unsigned int unicode[])
{
	const unicode_map_6x8_t *unicode_map_ptr;
	unsigned int current_index, current;

	/* this is required for log mode */

	if(display_mode != dm_text)
	{
		if(!clear_screen())
			return(false);

		display_mode = dm_text;
	}

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

		for(unicode_map_ptr = unicode_map_6x8; unicode_map_ptr->unicode != unicode_map_6x8_eof; unicode_map_ptr++)
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

static bool plot(unsigned int pixel_amount, int x, int y, string_t *pixels)
{
	if((pixel_amount == 0) || (string_length(pixels) == 0))
		return(true);

	if(x != 0)
		return(false);

	if(y != 0)
		return(false);

	if((unsigned int)string_length(pixels) < (pixel_amount / 8))
		return(false);

	if(pixel_amount != 8192)
		return(false);

	if(display_mode != dm_graphic)
	{
		if(!clear_screen())
			return(false);
		display_mode = dm_graphic;
	}

	bool half_height = config_flags_match(flag_ssd_height_32);
	(void)half_height;

	string_setlength(pixels, pixel_amount / 8);

	int max_page, page, segment, bit;
	int index, byte_index, bit_index;
	int byte_in, byte_out;

	max_page = half_height ? 4 : 8;

	for(page = 0; page < max_page; page++)
	{
		if(!display_cursor_row_column(page, 0))
			return(false);

		for(segment = 0; segment < 128; segment++)
		{
			byte_out = 0;

			for(bit = 0; bit < 8; bit++)
			{
				x = segment;
				y = (page * 8) + bit;

				if(half_height)
					y *= 2;

				index = x + y * 128;
				byte_index = index / 8;
				bit_index = 7 - (index % 8);

				byte_in = string_at(pixels, byte_index);
				byte_in = byte_in & (1 << bit_index);

				if(byte_in)
					byte_out |= (1 << bit);
			}

			if(!display_data_output(byte_out))
				return(false);
		}

		if(!display_data_flush())
			return(false);
	}

	return(true);
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
		(void *)0,
		plot,
		(void *)0,
	}
};
