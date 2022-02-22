#include "display.h"
#include "display_spitft.h"
#include "spi.h"
#include "config.h"
#include "sys_time.h"
#include "util.h"
#include "display_font_6x8.h"
#include "io.h"

#include <stdint.h>
#include <stdbool.h>

typedef enum
{
	display_type_disabled = 0,
	display_type_st7735 = 1,
	display_type_illegal,
} display_type_t;

typedef struct
{
	unsigned int	unicode;
	unsigned int	internal;
} unicode_map_t;

enum
{
	cmd_nop =		0x00,
	cmd_swreset =	0x01,
	cmd_sleepin =	0x10,
	cmd_sleepout = 	0x11,
	cmd_ptlon =		0x12,
	cmd_noron =		0x13,
	cmd_invoff =	0x20,
	cmd_invon =		0x21,
	cmd_dispoff =	0x28,
	cmd_dispon =	0x29,
	cmd_caset =		0x2a,
	cmd_raset =		0x2b,
	cmd_ramwr =		0x2c,
	cmd_plar =		0x30,
	cmd_madctl =	0x36,
	cmd_idmoff =	0x38,
	cmd_idmon =		0x39,
	cmd_colmod =	0x3a,
};

static struct attr_packed
{
	unsigned int column:8;
	unsigned int row:8;
	unsigned int slot:4;
	unsigned int display_slot:1;
} text;

assert_size(text, 3);

static struct attr_packed
{
	display_type_t	type:3;
	unsigned int	logmode:1;
	unsigned int	graphic_mode:1;
	unsigned int	brightness:3;
	unsigned int	x_size:8;
	unsigned int	y_size:8;
	unsigned int	x_offset:4;
	unsigned int	y_offset:4;
	unsigned int	buffer_size:8;
	unsigned int	buffer_current:8;
} display;

assert_size(display, 6);

static struct attr_packed
{
	struct attr_packed
	{
		int8_t io;
		int8_t pin;
	} user_cs;

	struct attr_packed
	{
		int8_t io;
		int8_t pin;
	} dcx;
} pin;

attr_result_used attr_inline unsigned int text_columns(void)
{
	return(display.x_size / display_font_width);
}

attr_result_used attr_inline unsigned int text_rows(void)
{
	if(display.logmode)
		return(16);
	else
		return(4);
}

static unsigned int rgb_to_16bit_colour(unsigned int r, unsigned int g, unsigned int b)
{
	unsigned int colour;

	r = (r & 0b11111000) >> 3;
	g = (g & 0b11111100) >> 2;
	b = (b & 0b11111000) >> 3;

	colour = (r << 11) | (g << 5) | (b << 0);

	return(colour);
}

static void background_colour(unsigned int slot, bool highlight, unsigned int *r, unsigned int *g, unsigned int *b)
{
	roflash static const unsigned int rgb[3][3] =
	{
		{	0xff, 	0x00,	0x00	},
		{	0x00,	0x88,	0x00	},
		{	0x00,	0x00,	0xff	},
	};

	slot = slot % 3;

	*r = rgb[slot][0];
	*g = rgb[slot][1];
	*b = rgb[slot][2];

	if(!highlight)
	{
		*r >>= 1;
		*g >>= 1;
		*b >>= 1;
	}

	*r >>= (4 - display.brightness);
	*g >>= (4 - display.brightness);
	*b >>= (4 - display.brightness);
}

static bool attr_result_used write_command(uint8_t cmd)
{
	string_new(, error, 64);

	if(io_write_pin(&error, pin.dcx.io, pin.dcx.pin, 0) != io_ok)
	{
		log("spitft write command: io: %s\n", string_to_cstr(&error));
		return(false);
	}

	if(!spi_send_receive(spi_clock_20M, spi_mode_0, false, pin.user_cs.io, pin.user_cs.pin,
				false, 0, 1, &cmd, 0, 0, (uint8_t *)0, &error))
	{
		log("spitft write command: spi: %s\n", string_to_cstr(&error));
		return(false);
	}

	return(true);
}

static bool attr_result_used write_data(int length, const uint8_t *data)
{
	string_new(, error, 64);

	if(io_write_pin(&error, pin.dcx.io, pin.dcx.pin, 1) != io_ok)
	{
		log("spitft write data: io: %s\n", string_to_cstr(&error));
		return(false);
	}

	if(!spi_send_receive(spi_clock_20M, spi_mode_0, false, pin.user_cs.io, pin.user_cs.pin,
				false, 0, length, data, 0, 0, (uint8_t *)0, &error))
	{
		log("spitft write data: spi: %s\n", string_to_cstr(&error));
		return(false);
	}

	return(true);
}

static bool attr_result_used write_command_data_1(unsigned int cmd, unsigned int data)
{
	uint8_t data_array[1];

	if(!write_command(cmd))
		return(false);

	data_array[0] = data;

	if(!write_data(1, data_array))
		return(false);

	return(true);
}

static bool attr_result_used write_command_data_2_16(unsigned int cmd, unsigned int data_1, unsigned int data_2)
{
	uint8_t data_array[4];

	data_array[0] = (data_1 & 0xff00) >> 8;
	data_array[1] = (data_1 & 0x00ff) >> 0;
	data_array[2] = (data_2 & 0xff00) >> 8;
	data_array[3] = (data_2 & 0x00ff) >> 0;

	if(!write_command(cmd))
		return(false);

	if(!write_data(sizeof(data_array), data_array))
		return(false);

	return(true);
}

static attr_result_used bool flush_data(void)
{
	if((display.buffer_current > 1) && !write_data(display.buffer_current, display_buffer))
		return(false);

	display.buffer_current = 0;

	return(true);
}

attr_inline attr_result_used bool output_data(unsigned int byte)
{
	if(((display.buffer_current + 1) >= display.buffer_size) && !flush_data())
		return(false);

	display_buffer[display.buffer_current++] = (uint8_t)byte;

	return(true);
}

attr_inline attr_result_used bool output_data_16(unsigned int word)
{
	if(!output_data((word & 0xff00) >> 8))
		return(false);

	if(!output_data((word & 0x00ff) >> 0))
		return(false);

	return(true);
}

static bool box(unsigned int r, unsigned int g, unsigned int b, unsigned int from_x, unsigned int from_y, unsigned int to_x, unsigned int to_y)
{
	enum { entries = 16 };
	unsigned int run;
	int bytes;
	uint16_t spibuffer[entries];
	unsigned int box_colour;

	assert_size(spibuffer, 32);

	if(!flush_data())
		return(false);

	if(!write_command_data_2_16(cmd_caset, from_x + display.x_offset, to_x + display.x_offset))
		return(false);

	if(!write_command_data_2_16(cmd_raset, from_y + display.y_offset, to_y + display.y_offset))
		return(false);

	if(!write_command(cmd_ramwr))
		return(false);

	box_colour = rgb_to_16bit_colour(r, g, b);
	// swap bytes for host order not correct when using SPI engine
	box_colour = ((box_colour & 0xff00) >> 8) | ((box_colour & 0x00ff) << 8);

	for(run = 0; run < entries; run++)
		spibuffer[run] = box_colour;

	bytes = (to_x - from_x + 1) * (to_y - from_y + 1) * sizeof(uint16_t) /*16 bpp*/;

	for(; bytes >= 0; bytes -= sizeof(spibuffer))
		if(!write_data(sizeof(spibuffer), (uint8_t *)spibuffer))
			return(false);

	return(true);
}

static bool clear_screen(void)
{
	return(box(0x00, 0x00, 0x00, 0, 0, display.x_size, display.y_size));
}

static attr_result_used bool text_send(unsigned int byte)
{
	const unsigned int *font_6x8 = (const unsigned int *)(const void *)display_font_6x8;
	const unsigned int *font_entry;
	uint8_t font_line[display_font_height];
	unsigned int x, y, r, g, b;
	bool set;
	unsigned int fg_colour, bg_colour;
	unsigned int cell_width, cell_height, line_repeat;

	if((text.column >= text_columns()) || (text.row >= text_rows()))
		goto done;

	if(!flush_data())
		return(false);

	r = 0xff;
	g = 0xff;
	b = 0xff;

	r >>= (4 - display.brightness);
	g >>= (4 - display.brightness);
	b >>= (4 - display.brightness);

	fg_colour = rgb_to_16bit_colour(r, g, b);

	if(display.logmode)
	{
		r = 0;
		g = 0;
		b = 0;
	}
	else
		background_colour(text.slot, text.row == 0, &r, &g, &b);

	bg_colour = rgb_to_16bit_colour(r, g, b);

	x = text.column * display_font_width;
	y = text.row * display_font_height;
	cell_width = display_font_width;
	cell_height = display_font_height;
	line_repeat = 1;

	if(!display.logmode)
	{
		y *= 2;
		cell_height *= 2;
		line_repeat *= 2;
	}

	if(!display.logmode && (text.display_slot != 0))
		y += display.y_size / 2;

	if(!write_command_data_2_16(cmd_caset, x + 0 + display.x_offset, x + cell_width + display.x_offset - 1))
		return(false);

	if(!write_command_data_2_16(cmd_raset, y + 0 + display.y_offset, y + cell_height + display.y_offset - 1))
		return(false);

	if(!write_command(cmd_ramwr))
		return(false);

	font_entry = &font_6x8[byte * 8 /*sizeof(entry)*/ / 4 /*sizeof(int)*/];

	font_line[0] = (font_entry[0] & 0x000000ff) >> 0;
	font_line[1] = (font_entry[0] & 0x0000ff00) >> 8;
	font_line[2] = (font_entry[0] & 0x00ff0000) >> 16;
	font_line[3] = (font_entry[0] & 0xff000000) >> 24;
	font_line[4] = (font_entry[1] & 0x000000ff) >> 0;
	font_line[5] = (font_entry[1] & 0x0000ff00) >> 8;
	font_line[6] = (font_entry[1] & 0x00ff0000) >> 16;
	font_line[7] = (font_entry[1] & 0xff000000) >> 24;

	for(y = 0; y < display_font_height; y++)
	{
		for(r = 0; r < line_repeat; r++)
		{
			for(x = 0; x < display_font_width; x++)
			{
				set = !!(font_line[y] & (1 << (display_font_width - x)));

				if(!output_data_16(set ? fg_colour : bg_colour))
					return(false);
			}
		}
	}

done:
	if(!flush_data())
		return(false);

	text.column++;

	return(true);
}

static attr_result_used bool text_newline(void)
{
	unsigned int x1, x2;
	unsigned int y1, y2;

	if(display.logmode)
	{
		text.row = (text.row + 1) % text_rows();

		x1 = 0;
		x2 = x1 + display.x_size;
		y1 = text.row * display_font_height;
		y2 = y1 + display_font_height;

		if(!box(0x00, 0x00, 0x00, x1, y1, x2, y2))
			return(false);
	}
	else
		text.row++;

	text.column = 0;

	return(true);
}

static bool init(void)
{
	unsigned int display_type;
	int dcx_io, dcx_pin;
	int user_cs_io, user_cs_pin;
	unsigned int x_size, x_offset;
	unsigned int y_size, y_offset;

	if(!config_get_uint("spitft.type", &display_type, -1, -1))
	{
		display.type = display_type_disabled;
		goto error;
	}

	display.type = display_type;
	display.brightness = 4;

	if(!config_get_uint("spitft.x.size", &x_size, -1, -1))
		goto error;

	if(!config_get_uint("spitft.x.offset", &x_offset, -1, -1))
		goto error;

	if(!config_get_uint("spitft.y.size", &y_size, -1, -1))
		goto error;

	if(!config_get_uint("spitft.y.offset", &y_offset, -1, -1))
		goto error;

	display.x_size = x_size;
	display.x_offset = x_offset;
	display.y_size = y_size;
	display.y_offset = y_offset;

	if(!config_get_int("spitft.dcx.io", &dcx_io, -1, -1) ||
			!config_get_int("spitft.dcx.pin", &dcx_pin, -1, -1))
		goto error;

	pin.dcx.io = dcx_io;
	pin.dcx.pin = dcx_pin;

	if(!config_get_int("spitft.cs.io", &user_cs_io, -1, -1) ||
			!config_get_int("spitft.cs.pin", &user_cs_pin, -1, -1))
	{
		user_cs_io = -1;
		user_cs_pin = -1;
	}

	pin.user_cs.io = user_cs_io;
	pin.user_cs.pin = user_cs_pin;

	if(!write_command(cmd_swreset))
		goto error;

	msleep(10);

	if(!write_command(cmd_sleepout))
		goto error;

	msleep(10);

	if(!write_command_data_1(cmd_colmod, 0x05)) // 16 bpp
		goto error;

	if(!write_command(cmd_noron))
		goto error;

	if(!write_command(cmd_dispon))
		goto error;

	if(!write_command(cmd_invoff))
		goto error;

	switch(display.type)
	{
		case(display_type_st7735):
		{
			if(!write_command_data_1(cmd_madctl, 0xa8)) // mirror everything, use BGR
				goto error;

			break;
		}

		default:
			goto error;
	}

	text.column = text.row = 0;

	display.graphic_mode = 0;
	display.logmode = 0;
	display.buffer_current = 0;

	if(display_buffer_size > 32)
		display.buffer_size = 32;
	else
		display.buffer_size = display_buffer_size;

	clear_screen();

	return(true);

error:
	return(false);
}

static bool begin(unsigned int slot, bool logmode)
{
	unsigned int x1, x2, y1, y2;
	unsigned int r, g, b;

	if(display.type == display_type_disabled)
		return(false);

	text.column = text.row = 0;
	text.slot = slot;
	text.display_slot = slot % 2;
	display.logmode = logmode;

	if(!display.logmode)
	{
		background_colour(text.slot, true, &r, &g, &b);

		x1 = 0;
		y1 = 0;
		x2 = display.x_size;
		y2 = display_font_height * 2;

		if(text.display_slot != 0)
		{
			y1 += display.y_size / 2;
			y2 += display.y_size / 2;
		}

		y2--;

		if(!box(r, g, b, x1, y1, x2, y2))
			return(false);

		background_colour(text.slot, false, &r, &g, &b);

		x1 = 0;
		y1 = display_font_height * 1 * 2;
		x2 = display.x_size;
		y2 = display_font_height * text_rows() * 2 - 1;

		if(text.display_slot != 0)
		{
			y1 = y1 + (display.y_size / 2) - 2;
			y2 += display.y_size / 2;
		}

		if(!box(r, g, b, x1, y1, x2, y2))
			return(false);
	}

	return(true);
}

static bool output(unsigned int length, const unsigned int unicode[])
{
	const unicode_map_6x8_t *unicode_map_ptr;
	unsigned int current_index, current;

	/* this is required for log mode */

	if(display.graphic_mode)
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

		for(unicode_map_ptr = unicode_map_6x8; unicode_map_ptr->unicode != unicode_map_6x8_eof; unicode_map_ptr++)
		{
			if(unicode_map_ptr->unicode == current)
			{
				if(!text_send(unicode_map_ptr->internal))
					return(false);

				continue;
			}
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
	return(true);
}

static bool plot(unsigned int pixel_amount, int x, int y, string_t *pixels)
{
	return(true); // FIXME
}

extern roflash const char help_description_display_spitft[];

app_action_t application_function_display_spitft(string_t *src, string_t *dst)
{
	unsigned int param_type;
	int dcx_io, dcx_pin;
	int cs_io, cs_pin;
	unsigned int x_size, x_offset;
	unsigned int y_size, y_offset;

	if(parse_uint(1, src, &param_type, 0, ' ') == parse_ok)
	{
		if(param_type >= display_type_illegal)
		{
			string_append_cstr_flash(dst, help_description_display_spitft);
			return(app_action_error);
		}

		if(param_type != display_type_disabled)
		{
			if((parse_uint(2, src, &x_size, 0, ' ') != parse_ok) || (parse_uint(3, src, &x_offset, 0, ' ') != parse_ok))
			{
				string_append_cstr_flash(dst, help_description_display_spitft);
				return(app_action_error);
			}

			if((parse_uint(4, src, &y_size, 0, ' ') != parse_ok) || (parse_uint(5, src, &y_offset, 0, ' ') != parse_ok))
			{
				string_append_cstr_flash(dst, help_description_display_spitft);
				return(app_action_error);
			}

			if((parse_int(6, src, &dcx_io, 0, ' ') != parse_ok) || (parse_int(7, src, &dcx_pin, 0, ' ') != parse_ok))
			{
				string_append_cstr_flash(dst, help_description_display_spitft);
				return(app_action_error);
			}

			if((dcx_io < 0) || (dcx_io >= io_id_size) || (dcx_pin < 0) || (dcx_pin >= max_pins_per_io))
			{
				string_append_cstr_flash(dst, help_description_display_spitft);
				return(app_action_error);
			}

			if((parse_int(8, src, &cs_io, 0, ' ') == parse_ok) && (parse_int(9, src, &cs_pin, 0, ' ') == parse_ok))
			{
				if((cs_io < 0) || (cs_io >= io_id_size) || (cs_pin < 0) || (cs_pin >= max_pins_per_io))
				{
					string_append_cstr_flash(dst, help_description_display_spitft);
					return(app_action_error);
				}
			}
			else
			{
				cs_io = -1;
				cs_pin = -1;
			}
		}

		if(!config_open_write())
			goto config_error;

		if(param_type == display_type_disabled)
		{
			config_delete("spitft.type", false, -1, -1);
			config_delete("spitft.dcx.io", false, -1, -1);
			config_delete("spitft.dcx.pin", false, -1, -1);
			config_delete("spitft.cs.io", false, -1, -1);
			config_delete("spitft.cs.pin", false, -1, -1);
			config_delete("spitft.x.size", false, -1, -1);
			config_delete("spitft.x.offset", false, -1, -1);
			config_delete("spitft.y.size", false, -1, -1);
			config_delete("spitft.y.offset", false, -1, -1);
		}
		else
		{
			if(!config_set_uint("spitft.type", param_type, -1, -1))
				goto config_error;

			if(!config_set_uint("spitft.x.size", x_size, -1, -1))
				goto config_error;

			if(!config_set_uint("spitft.x.offset", x_offset, -1, -1))
				goto config_error;

			if(!config_set_uint("spitft.y.size", y_size, -1, -1))
				goto config_error;

			if(!config_set_uint("spitft.y.offset", y_offset, -1, -1))
				goto config_error;

			if(!config_set_uint("spitft.dcx.io", dcx_io, -1, -1))
				goto config_error;

			if(!config_set_uint("spitft.dcx.pin", dcx_pin, -1, -1))
				goto config_error;

			if((cs_io < 0) || (cs_pin < 0))
			{
				config_delete("spitft.cs.io", false, -1, -1);
				config_delete("spitft.cs.pin", false, -1, -1);
			}
			else
			{
				if(!config_set_uint("spitft.cs.io", cs_io, -1, -1))
					goto config_error;

				if(!config_set_uint("spitft.cs.pin", cs_pin, -1, -1))
					goto config_error;
			}

		}

		if(!config_close_write())
			goto config_error;

		pin.dcx.io = dcx_io;
		pin.dcx.pin = dcx_pin;

		pin.user_cs.io = cs_io;
		pin.user_cs.pin = cs_pin;
	}

	if(!config_get_uint("spitft.type", &param_type, -1, -1))
		param_type = 0;

	switch(param_type)
	{
		case(display_type_disabled): string_append(dst, "> type 0 (disabled)\n"); break;
		case(display_type_st7735): string_append(dst, "> type 1 (st7735)\n"); break;
		default: string_append(dst, "> unknown type\n"); break;
	}

	if(param_type != display_type_disabled)
	{
		if(!config_get_uint("spitft.x.size", &x_size, -1, -1))
			x_size = 0;

		if(!config_get_uint("spitft.x.offset", &x_offset, -1, -1))
			x_offset = 0;

		string_format(dst, "> x size: %u, offset: %u\n", x_size, x_offset);

		if(!config_get_uint("spitft.y.size", &y_size, -1, -1))
			y_size = 0;

		if(!config_get_uint("spitft.y.offset", &y_offset, -1, -1))
			y_offset = 0;

		string_format(dst, "> y size: %u, offset: %u\n", y_size, y_offset);

		if(!config_get_int("spitft.dcx.io", &dcx_io, -1, -1) ||
				!config_get_int("spitft.dcx.pin", &dcx_pin, -1, -1))
			dcx_io = dcx_pin = -1;

		string_format(dst, "> dcx io: %d, pin: %d\n", dcx_io, dcx_pin);

		if(!config_get_int("spitft.cs.io", &cs_io, -1, -1) ||
				!config_get_int("spitft.cs.pin", &cs_pin, -1, -1))
			cs_io = cs_pin = -1;

		if((cs_io >= 0) && (cs_pin >= 0))
			string_format(dst, "> cs io: %d, pin: %d\n", cs_io, cs_pin);
	}

	return(app_action_normal);

config_error:
	config_abort_write();
	string_clear(dst);
	string_append(dst, "> cannot set config\n");
	return(app_action_error);
}

static bool bright(int brightness)
{
	if((brightness < 0) || (brightness > 4))
		return(false);

	display.brightness = brightness;

	return(true);
}

roflash const display_info_t display_info_spitft =
{
	{
		"generic SPI TFT LCD",
		{ 21, 16 },
		{ 128, 128 },
		16,
	},
	{
		init,
		begin,
		output,
		end,
		bright,
		(void *)0,
		(void *)0,
		(void *)0,
		plot,
		(void *)0,
	}
};
