#include "display.h"
#include "display_ssd1306.h"
#include "i2c.h"
#include "config.h"
#include "sys_time.h"
#include "dispatch.h"
#include "font.h"

#include <stdint.h>
#include <stdbool.h>

typedef struct attr_packed
{
	unsigned int column:8;
	unsigned int row:8;
	unsigned int row_height:8;
} text_t;

assert_size(text_t, 3);

typedef struct attr_packed
{
	unsigned int	enabled:1;
	unsigned int	logmode:1;
	unsigned int	graphic_mode:1;
	unsigned int	x_size:8;
	unsigned int	y_size:8;
	unsigned int	buffer_current:8;
	unsigned int	buffer_size:8;
} display_t;

assert_size(display_t, 5);

enum
{
	ssd1306_address =			0x3c,
	ssd1306_control_command =	0b00000000,
	ssd1306_control_data =		0b01000000,
	ssd1306_page_height =		8,

	reg_address_mode =			0x20,
	reg_set_col_conf =			0x21,
	reg_set_page_conf =			0x22,
	reg_deactivate_scroll =		0x2e,
	reg_display_start_line =	0x40,
	reg_contrast =				0x81,
	reg_charge_pump =			0x8d,
	reg_remap_segment_on =		0xa1,
	reg_test_mode_off =			0xa4,
	reg_display_normal =		0xa6,
	reg_mux_ratio =				0xa8,
	reg_display_off =			0xae,
	reg_display_on =			0xaf,
	reg_remap_common_on =		0xc8,
	reg_start_line =			0xd3,
	reg_pin_layout =			0xda,

	reg_pin_layout_normal =		0x02,
	reg_pin_layout_alt =		0x12,

	reg_address_mode_horizontal =	0b00000000,
	reg_address_mode_vertical =		0b00000001,
	reg_address_mode_page =			0b00000010,

	reg_set_oscillator_default=		0b10000000,
	reg_charge_pump_on =			0b00010100,
};

static text_t text;
static display_t display;

static attr_result_used bool send_command1(unsigned int cmd)
{
	if(i2c_send2(ssd1306_address, ssd1306_control_command, cmd) != i2c_error_ok)
		return(false);

	return(true);
}

static attr_result_used bool send_command2(unsigned int cmd, unsigned int data)
{
	if(i2c_send3(ssd1306_address, ssd1306_control_command, cmd, data) != i2c_error_ok)
		return(false);

	return(true);
}

static attr_result_used bool send_command3(unsigned int cmd, unsigned int data1, unsigned int data2)
{
	if(i2c_send4(ssd1306_address, ssd1306_control_command, cmd, data1, data2) != i2c_error_ok)
		return(false);

	return(true);
}

static attr_result_used bool data_flush(void)
{
	if((display.buffer_current > 1) && i2c_send(ssd1306_address, display.buffer_current, display_buffer) != i2c_error_ok)
		return(false);

	display.buffer_current = 0;
	display_buffer[display.buffer_current++] = ssd1306_control_data;

	return(true);
}

attr_inline attr_result_used bool data_output(unsigned int byte)
{
	if(((display.buffer_current + 1) >= display.buffer_size) && !data_flush())
		return(false);

	display_buffer[display.buffer_current++] = (uint8_t)byte;

	return(true);
}

static attr_result_used bool box(bool onoff, unsigned int from_x, unsigned int from_page, unsigned int to_x, unsigned int to_page)
{
	unsigned int bytes;

	if(!data_flush())
		return(false);

	bytes = (to_x - from_x + 1) * (to_page - from_page + 1);

	if(!send_command3(reg_set_col_conf, from_x, to_x))
		return(false);

	if(!send_command3(reg_set_page_conf, from_page, to_page))
		return(false);

	while(bytes-- > 0)
		if(!data_output(onoff ? 0xff : 0x00))
			return(false);

	if(!data_flush())
		return(false);

	return(true);
}

static attr_result_used bool clear_screen(void)
{
	return(box(false, 0, 0, display.x_size - 1, (display.y_size / ssd1306_page_height) - 1));
}

static attr_result_used bool text_send(unsigned int code)
{
	font_info_t font_info;
	font_cell_t font_cell;
	unsigned int column, page;
	unsigned int x, y;
	unsigned int byte, bit;

	if(!data_flush())
		return(false);

	if(!font_get_info(&font_info))
		return(false);

	column = text.column * font_info.width;
	page = (text.row * text.row_height) / ssd1306_page_height;

	if(((column + font_info.width) >= display.x_size) || (page >= (display.y_size / ssd1306_page_height)))
		goto skip;

	if(!font_render(code, font_cell))
		return(false);

	if(!send_command3(reg_set_col_conf, column, column + font_info.width - 1))
		return(false);

	if(!send_command3(reg_set_page_conf, page, page + (text.row_height / ssd1306_page_height) - 1))
		return(false);

	for(x = 0; x < font_info.width; x++)
	{
		byte = 0;
		bit = 0;

		for(y = 0; y < text.row_height; y++)
		{
			if((y < font_info.height) && (font_cell[y][x]))
				byte |= (1 << bit);

			if(bit == 7)
			{
				if(!display.logmode && (text.row == 0))
					byte ^= 0xff;

				if(!data_output(byte))
					return(false);

				bit = 0;
				byte = 0;
			}
			else
				bit++;
		}
	}

	if(!data_flush())
		return(false);

skip:
	text.column++;

	return(true);
}

static attr_result_used bool text_newline(void)
{
	unsigned int x1, x2;
	unsigned int y1, y2;
	font_info_t font_info;

	if(!font_get_info(&font_info))
		return(false);

	if(display.logmode)
	{
		text.row = (text.row + 1) % (display.y_size / text.row_height);

		x1 = 0;
		y1 = (text.row * text.row_height) / ssd1306_page_height;

		x2 = display.x_size - 1;
		y2 = (((text.row + 1) * text.row_height) / ssd1306_page_height) - 1;

		if(!box(false, x1, y1, x2, y2))
			return(false);
	}
	else
		text.row++;

	text.column = 0;

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
	unsigned height;

	if(!config_get_uint("display_ssd1306.height", &height, -1, -1))
		height = 0;

	display.enabled = 0;

	if(height == 0)
		return(false);

	if(!send_command1(reg_display_off))
		return(false);

	if(!send_command2(reg_mux_ratio, height - 1))
		return(false);

	if(!send_command2(reg_start_line, 0))
		return(false);

	if(!send_command1(reg_display_start_line))
		return(false);

	if(!send_command1(reg_remap_segment_on))
		return(false);

	if(!send_command1(reg_remap_common_on))
		return(false);

	if(!send_command2(reg_pin_layout, height == 32 ? reg_pin_layout_normal : reg_pin_layout_alt))
		return(false);

	if(!send_command2(reg_address_mode, reg_address_mode_vertical))
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

	text.column = text.row = text.row_height = 0;

	display.enabled = 1;
	display.logmode = 1;
	display.graphic_mode = 0;
	display.x_size = 128;
	display.y_size = height;
	display.buffer_current = 0;
	display.buffer_size = display_buffer_size <= 32 ? display_buffer_size : 32;

	if(!clear_screen())
		return(false);

	return(bright(4));
}

static bool begin(unsigned int slot, bool logmode)
{
	unsigned int x1, x2, y1, y2;
	font_info_t font_info;

	if(!display.enabled)
		return(false);

	text.column = text.row = 0;
	display.logmode = logmode;

	if(!font_select(logmode))
		return(false);

	if(!font_get_info(&font_info))
		return(false);

	if(font_info.height <= 8)
		text.row_height = 8;
	else
	{
		if(font_info.height <= 16)
			text.row_height = 16;
		else
		{
			log("display_ssd1306: font too high\n");
			return(false);
		}
	}

	if(!display.logmode)
	{
		x1 = 0;
		x2 = display.x_size - 1;

		y1 = 0;
		y2 = (text.row_height / ssd1306_page_height) - 1;

		if(!box(true, x1, y1, x2, y2))
			return(false);

		y1 = y2 + 1;
		y2 = (display.y_size / ssd1306_page_height) - 1;

		if(!box(false, x1, y1, x2, y2))
			return(false);
	}

	return(true);
}

static bool output(unsigned int length, const unsigned int unicode[])
{
	unsigned int current_index, current;

	/* this is required for log mode */

	if(display.graphic_mode && display.logmode)
	{
		if(!clear_screen())
			return(false);

		display.graphic_mode = 0;
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

		if(!text_send(current))
			return(false);
	}

	return(true);
}

static bool end(void)
{
	return(true);
}

static bool plot(unsigned int pixel_amount, int x, int y, string_t *pixels)
{
	unsigned int segment, page, byte, byte_offset, bit_offset;

	if((pixel_amount == 0) || (string_length(pixels) == 0))
		return(true);

	if(x != 0)
		return(false);

	if(y != 0)
		return(false);

	if((unsigned int)string_length(pixels) < (pixel_amount / 8))
		return(false);

	if(pixel_amount != (display.x_size * display.y_size))
		return(false);

	string_setlength(pixels, pixel_amount / 8);

	if(!data_flush())
		return(false);

	if(!send_command3(reg_set_col_conf, 0, display.x_size - 1))
		return(false);

	if(!send_command3(reg_set_page_conf, 0, (display.y_size / ssd1306_page_height) - 1))
		return(false);

	display.graphic_mode = 1;

	for(segment = 0; segment < display.x_size; segment++)
	{
		for(page = 0; page < (display.y_size / ssd1306_page_height); page++)
		{
			byte = 0;

			for(y = 0; y < ssd1306_page_height; y++)
			{
				byte_offset = (((page * ssd1306_page_height) + y) * display.x_size) + segment;

				bit_offset = 7 - (byte_offset % 8);
				byte_offset = byte_offset / 8;

				if(string_at(pixels, byte_offset) & (1 << bit_offset))
					byte |= 1 <<  y;
			}

			if(!data_output(byte))
				return(false);
		}
	}

	if(!data_flush())
		return(false);

	return(true);
}

app_action_t application_function_display_ssd1306(string_t *src, string_t *dst)
{
	unsigned int height;

	if(parse_uint(1, src, &height, 0, ' ') == parse_ok)
	{
		if((height != 0) && (height != 32) && (height != 64))
		{
			string_append_cstr_flash(dst, help_description_display_ssd1306);
			return(app_action_error);
		}

		if(!config_open_write())
			goto config_error;

		if(height == 0)
			config_delete("display_ssd1306.", true, -1, -1);
		else
			if(!config_set_uint("display_ssd1306.height", height, -1, -1))
				goto config_error;

		if(!config_close_write())
			goto config_error;
	}

	if(!config_get_uint("display_ssd1306.height", &height, -1, -1))
		height = 0;

	if(height > 0)
		string_format(dst, "> display ssd1306 height: %u\n", height);
	else
		string_append(dst, "> display ssd1306 disabled\n");

	return(app_action_normal);

config_error:
	config_abort_write();
	string_clear(dst);
	string_append(dst, "> cannot set config\n");
	return(app_action_error);
}

static bool info(display_info_t *infostruct)
{
	font_info_t font_info;
	unsigned int columns, rows;
	unsigned int cell_width, cell_height;

	if(font_get_info(&font_info))
	{
		cell_width = font_info.width;
		cell_height = font_info.height;
	}
	else
	{
		cell_width = 0;
		cell_height = 0;
	}

	if((cell_width != 0) && (cell_height != 0))
	{
		columns = display.x_size / cell_width;
		rows = display.y_size / cell_height;
	}
	else
	{
		columns = 0;
		rows = 0;
	}

	strncpy(infostruct->name, "SSD1306 OLED", sizeof(infostruct->name));

	infostruct->columns = columns;
	infostruct->rows = rows;
	infostruct->cell_width = cell_width;
	infostruct->cell_height = cell_height;
	infostruct->width = display.x_size;
	infostruct->height = display.y_size;
	infostruct->pixel_mode = display_pixel_mode_1;

	return(true);
}

roflash const char help_description_display_ssd1306[] = "display ssd1306 <height=32|64 or 0=disable>";

roflash const display_hooks_t display_hooks_ssd1306 =
{
	init,
	info,
	begin,
	output,
	end,
	bright,
	(void *)0,
	(void *)0,
	plot,
	(void *)0,
};
