#include "display.h"
#include "display_spitft.h"
#include "spi.h"
#include "config.h"
#include "sys_time.h"
#include "util.h"
#include "io.h"
#include "font.h"
#include "stats.h"

#include <stdint.h>
#include <stdbool.h>

typedef struct
{
	unsigned int	unicode;
	unsigned int	internal;
} unicode_map_t;

enum
{
	cmd_nop =		0x00,
	cmd_swreset =	0x01,
	cmd_sleepout = 	0x11,
	cmd_noron =		0x13,
	cmd_invoff =	0x20,
	cmd_invon =		0x21,
	cmd_dispon =	0x29,
	cmd_caset =		0x2a,
	cmd_raset =		0x2b,
	cmd_ramwr =		0x2c,
	cmd_madctl =	0x36,
	cmd_idmoff =	0x38,
	cmd_idmon =		0x39,
	cmd_colmod =	0x3a,
	cmd_ramwrc =	0x3c,

	madctl_my =		(1 << 7),
	madctl_mx = 	(1 << 6),
	madctl_mv =		(1 << 5),
	madctl_ml =		(1 << 4),
	madctl_bgr =	(1 << 3),
	madctl_mh =		(1 << 2),

	colmod_16bpp = 	0x05,
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
	unsigned int	enabled:1;
	unsigned int	logmode:1;
	unsigned int	graphic_mode:1;
	unsigned int	brightness:3;
	unsigned int	x_size:10;
	unsigned int	y_size:10;
	unsigned int	x_offset:4;
	unsigned int	y_offset:4;
	unsigned int	x_mirror:1;
	unsigned int	y_mirror:1;
	unsigned int	rotate:1;
	unsigned int	buffer_size:8;
	unsigned int	buffer_current:8;
} display;

assert_size(display, 7);

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
	unsigned int box_colour;
	uint8_t colour[2];
	unsigned int x, y;

	if(!write_command_data_2_16(cmd_caset, from_x + display.x_offset, to_x + display.x_offset))
		return(false);

	if(!write_command_data_2_16(cmd_raset, from_y + display.y_offset, to_y + display.y_offset))
		return(false);

	if(!write_command(cmd_ramwr))
		return(false);

	box_colour = rgb_to_16bit_colour(r, g, b);
	colour[0] = (box_colour & 0xff00) >> 8;
	colour[1] = (box_colour & 0x00ff) >> 0;

	for(y = from_y; y <= to_y; y++)
		for(x = from_x; x <= to_x; x++)
		{
			if(!output_data(colour[0]))
				return(false);
			if(!output_data(colour[1]))
				return(false);
		}

	if(!flush_data())
		return(false);

	return(true);
}

static bool clear_screen(void)
{
	return(box(0x00, 0x00, 0x00, 0, 0, display.x_size, display.y_size));
}

static attr_result_used bool text_send(unsigned int code)
{
	font_info_t font_info;
	font_cell_t cell;
	unsigned int x, y, r, g, b;
	unsigned int y_offset;
	unsigned int fg_colour, bg_colour;

	if(!flush_data())
		return(false);

	if(!font_get_info(&font_info))
		return(false);

	x = text.column * font_info.width;
	y = text.row * font_info.height;

	if(display.logmode)
		y_offset = 0;
	else
	{
		y_offset = text.display_slot ? display.y_size / 2 : 0;

		if(text.row > 0)
			y_offset += 2;
	}

	y += y_offset;

	if(((x + font_info.width) > display.x_size) || ((y + font_info.height) > display.y_size))
		goto skip;

	if(!font_render(code, cell))
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

	if(!write_command_data_2_16(cmd_caset, x + 0 + display.x_offset, x + font_info.width + display.x_offset - 1))
		return(false);

	if(!write_command_data_2_16(cmd_raset, y + 0 + display.y_offset, y + font_info.height + display.y_offset - 1))
		return(false);

	if(!write_command(cmd_ramwr))
		return(false);

	for(y = 0; y < font_info.height; y++)
		for(x = 0; x < font_info.width; x++)
			if(!output_data_16(cell[y][x] ? fg_colour : bg_colour))
				return(false);

	if(!flush_data())
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
		text.row = (text.row + 1) % (display.y_size / font_info.height);

		x1 = 0;
		x2 = x1 + display.x_size;
		y1 = text.row * font_info.height;
		y2 = y1 + font_info.height;

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
	int dcx_io, dcx_pin;
	int user_cs_io, user_cs_pin;
	unsigned int x_size, x_offset;
	unsigned int y_size, y_offset;
	unsigned int x_mirror, y_mirror;
	unsigned int rotate;
	unsigned int madctl;

	display.enabled = 0;

	if(!config_get_uint("spitft.x.size", &x_size, -1, -1))
		goto error;

	if(!config_get_uint("spitft.x.offset", &x_offset, -1, -1))
		goto error;

	if(!config_get_uint("spitft.y.size", &y_size, -1, -1))
		goto error;

	if(!config_get_uint("spitft.y.offset", &y_offset, -1, -1))
		goto error;

	if(!config_get_uint("spitft.x.mirror", &x_mirror, -1, -1))
		goto error;

	if(!config_get_uint("spitft.y.mirror", &y_mirror, -1, -1))
		goto error;

	if(!config_get_uint("spitft.rotate", &rotate, -1, -1))
		goto error;

	display.x_size = x_size;
	display.x_offset = x_offset;
	display.y_size = y_size;
	display.y_offset = y_offset;
	display.x_mirror = x_mirror;
	display.y_mirror = y_mirror;
	display.rotate = rotate;

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

	if(!write_command_data_1(cmd_colmod, colmod_16bpp))
		goto error;

	if(!write_command(cmd_noron))
		goto error;

	if(!write_command(cmd_dispon))
		goto error;

	if(!write_command(cmd_invoff))
		goto error;

	madctl = madctl_bgr;

	if(display.rotate)
		madctl |= madctl_mv;

	if(display.x_mirror)
		madctl |= madctl_mx;

	if(display.y_mirror)
		madctl |= madctl_my;

	if(!write_command_data_1(cmd_madctl, madctl))
		goto error;

	text.column = text.row = 0;

	display.brightness = 4;
	display.graphic_mode = 0;
	display.logmode = 0;
	display.buffer_current = 0;
	display.enabled = 1;

	if(display_buffer_size > 32) // spi engine can't handle writes > 32 bytes
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
	font_info_t font_info;

	if(!display.enabled)
		return(false);

	if(!font_select(logmode))
		return(false);

	if(!font_get_info(&font_info))
		return(false);

	text.column = text.row = 0;
	text.slot = slot;
	text.display_slot = slot % 2;
	display.logmode = logmode;

	if(!display.logmode)
	{
		background_colour(text.slot, true, &r, &g, &b);

		x1 = 0;
		x2 = display.x_size;

		if(text.display_slot == 0)
		{
			y1 = 0;
			y2 = font_info.height + 1;
		}
		else
		{
			y1 = display.y_size / 2;
			y2 = y1 + font_info.height + 1;
		}

		if(!box(r, g, b, x1, y1, x2, y2))
			return(false);

		background_colour(text.slot, false, &r, &g, &b);

		x1 = 0;
		x2 = display.x_size;

		if(text.display_slot == 0)
		{
			y1 = font_info.height + 2;
			y2 = display.y_size / 2 - 1;
		}
		else
		{
			y1 = (display.y_size / 2) + font_info.height + 2;
			y2 = display.y_size;
		}

		if(!box(r, g, b, x1, y1, x2, y2))
			return(false);
	}

	return(true);
}

static bool output(unsigned int length, const unsigned int unicode[])
{
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
	int ix;

	if(string_length(pixels) == 0)
		return(true);

	if(x > display.x_size)
		return(false);

	if(y > display.y_size)
		return(false);

	if((unsigned int)string_length(pixels) < (pixel_amount * 2))
		return(false);

	if(!flush_data())
		return(false);

	string_setlength(pixels, pixel_amount * 2);

	if((x == 0) && (y == 0))
	{
		if(!write_command_data_2_16(cmd_caset, display.x_offset, display.x_offset + display.x_size))
			return(false);

		if(!write_command_data_2_16(cmd_raset, display.y_offset, display.y_offset + display.y_size))
			return(false);

		if(!write_command(cmd_ramwr))
			return(false);
	}

	if(!write_command(cmd_ramwrc))
		return(false);

	for(ix = 0; ix < string_length(pixels); ix++)
		if(!output_data(string_at(pixels, ix)))
			return(false);

	if(!flush_data())
		return(false);

	return(true);
}

app_action_t application_function_display_spitft(string_t *src, string_t *dst)
{
	unsigned int x_size, x_offset, x_mirror;
	unsigned int y_size, y_offset, y_mirror;
	unsigned int rotate;
	int dcx_io, dcx_pin;
	int cs_io, cs_pin;

	if(parse_uint(1, src, &x_size, 0, ' ') == parse_ok)
	{
		if((parse_uint(2, src, &x_offset, 0, ' ') != parse_ok) ||
			(parse_uint(3, src, &x_mirror, 0, ' ') != parse_ok))
		{
			string_append_cstr_flash(dst, help_description_display_spitft);
			return(app_action_error);
		}

		if((parse_uint(4, src, &y_size, 0, ' ') != parse_ok) ||
				(parse_uint(5, src, &y_offset, 0, ' ') != parse_ok) ||
				(parse_uint(6, src, &y_mirror, 0, ' ') != parse_ok))
		{
			string_append_cstr_flash(dst, help_description_display_spitft);
			return(app_action_error);
		}

		if(parse_uint(7, src, &rotate, 0, ' ') != parse_ok)
		{
			string_append_cstr_flash(dst, help_description_display_spitft);
			return(app_action_error);
		}

		if((parse_int(8, src, &dcx_io, 0, ' ') != parse_ok) ||
				(parse_int(9, src, &dcx_pin, 0, ' ') != parse_ok))
		{
			string_append_cstr_flash(dst, help_description_display_spitft);
			return(app_action_error);
		}

		if((dcx_io < 0) || (dcx_io >= io_id_size) || (dcx_pin < 0) || (dcx_pin >= max_pins_per_io))
		{
			string_append_cstr_flash(dst, help_description_display_spitft);
			return(app_action_error);
		}

		if((parse_int(10, src, &cs_io, 0, ' ') == parse_ok) &&
				(parse_int(11, src, &cs_pin, 0, ' ') == parse_ok))
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

		if(!config_open_write())
			goto config_error;

		if(x_size == 0)
			config_delete("spitft.", true, -1, -1);
		else
		{
			if(!config_set_uint("spitft.x.size", x_size, -1, -1))
				goto config_error;

			if(!config_set_uint("spitft.x.offset", x_offset, -1, -1))
				goto config_error;

			if(!config_set_uint("spitft.x.mirror", x_mirror, -1, -1))
				goto config_error;

			if(!config_set_uint("spitft.y.size", y_size, -1, -1))
				goto config_error;

			if(!config_set_uint("spitft.y.offset", y_offset, -1, -1))
				goto config_error;

			if(!config_set_uint("spitft.y.mirror", y_mirror, -1, -1))
				goto config_error;

			if(!config_set_uint("spitft.rotate", rotate, -1, -1))
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

	if(!config_get_uint("spitft.x.size", &x_size, -1, -1))
	{
		string_format(dst, "no spi tft display configured\n");
		return(app_action_error);
	}

	if(!config_get_uint("spitft.x.offset", &x_offset, -1, -1))
		x_offset = 0;

	if(!config_get_uint("spitft.x.mirror", &x_mirror, -1, -1))
		x_mirror = 0;

	string_format(dst, "> x size: %u, offset: %u, mirror: %u\n", x_size, x_offset, x_mirror);

	if(!config_get_uint("spitft.y.size", &y_size, -1, -1))
		y_size = 0;

	if(!config_get_uint("spitft.y.offset", &y_offset, -1, -1))
		y_offset = 0;

	if(!config_get_uint("spitft.y.mirror", &y_mirror, -1, -1))
		x_mirror = 0;

	string_format(dst, "> y size: %u, offset: %u, mirror: %u\n", y_size, y_offset, y_mirror);

	if(!config_get_uint("spitft.rotate", &rotate, -1, -1))
		rotate = 0;

	string_format(dst, "> rotate: %u\n", rotate);

	if(!config_get_int("spitft.dcx.io", &dcx_io, -1, -1) ||
			!config_get_int("spitft.dcx.pin", &dcx_pin, -1, -1))
		dcx_io = dcx_pin = -1;

	string_format(dst, "> dcx io: %d, pin: %d\n", dcx_io, dcx_pin);

	if(!config_get_int("spitft.cs.io", &cs_io, -1, -1) ||
			!config_get_int("spitft.cs.pin", &cs_pin, -1, -1))
		cs_io = cs_pin = -1;

	if((cs_io >= 0) && (cs_pin >= 0))
		string_format(dst, "> cs io: %d, pin: %d\n", cs_io, cs_pin);

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


roflash const char help_description_display_spitft[] =	"> usage: display spitft <mode=0=disabled|1=st7735|2=ili9341>\n"
														"> <x size> <x offset> <x mirror 0|1>\n"
														"> <y size> <x offset> <y mirror 0|1>\n"
														"> <rotate 0|1>\n"
														"> <dcx io> <dcx pin>\n"
														"> [<user cs io> <user cs pin>]\n";

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

	strncpy(infostruct->name, "SPI TFT", sizeof(infostruct->name));

	infostruct->columns = columns;
	infostruct->rows = rows;
	infostruct->cell_width = cell_width;
	infostruct->cell_height = cell_height;
	infostruct->width = display.x_size;
	infostruct->height = display.y_size;
	infostruct->pixel_mode = display_pixel_mode_16_rgb;

	return(true);
}

roflash const display_hooks_t display_hooks_spitft =
{
	init,
	info,
	begin,
	output,
	end,
	bright,
	(void *)0,
	(void *)0,
	(void *)0,
	plot,
	(void *)0,
};
