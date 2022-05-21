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

	madctl_my =		(1 << 7),
	madctl_mx = 	(1 << 6),
	madctl_mv =		(1 << 5),
	madctl_ml =		(1 << 4),
	madctl_bgr =	(1 << 3),
	madctl_mh =		(1 << 2),

	colmod_16bpp = 	0x05,
};

typedef struct
{
	unsigned int column;
	unsigned int row;
	unsigned int slot;
	unsigned int display_slot;
} text_t;

assert_size(text_t, 16);

typedef struct
{
	unsigned int brightness;
	unsigned int x_size;
	unsigned int y_size;
	unsigned int x_offset;
	unsigned int y_offset;
	unsigned int enabled:1;
	unsigned int logmode:1;
	unsigned int graphic_mode:1;
	unsigned int x_mirror:1;
	unsigned int y_mirror:1;
	unsigned int rotate:1;
	unsigned int buffer_dirty:1;
} display_t;

assert_size(display_t, 24);

typedef struct
{
	struct
	{
		unsigned int enabled;
		int io;
		int pin;
	} dcx;

	struct
	{
		unsigned int enabled;
		int io;
		int pin;
	} bright;

	struct
	{
		unsigned int enabled;
		int io;
		int pin;
	} user_cs;
} pin_t;

assert_size(pin_t, 36);

static text_t text;
static display_t display;
static pin_t pin;

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
	roflash static const unsigned int rgb[4][3] =
	{
		{	0x40, 	0x40,	0x40	},
		{	0xff,	0x00,	0x00	},
		{	0x00,	0x88,	0x00	},
		{	0x00,	0x00,	0xff	},
	};

	slot = slot % 4;

	*r = rgb[slot][0];
	*g = rgb[slot][1];
	*b = rgb[slot][2];

	if(!highlight)
	{
		*r >>= 1;
		*g >>= 1;
		*b >>= 1;
	}

	if(!pin.bright.enabled)
	{
		*r >>= (4 - display.brightness);
		*g >>= (4 - display.brightness);
		*b >>= (4 - display.brightness);
	}
}

static attr_result_used bool send_command_data(string_t *error, unsigned int send_cmd, unsigned int cmd, unsigned int length, const uint8_t *data)
{
	unsigned int current;
	unsigned int value;

	if(pin.dcx.enabled)
	{
		if(send_cmd)
		{
			if(!spi_start(error))
			{
				if(error)
					string_append(error, " - during cmd start 8");
				return(false);
			}

			if(io_write_pin(error, pin.dcx.io, pin.dcx.pin, 0) != io_ok)
			{
				if(error)
					string_append(error, " - during cmd io 8");
				return(false);
			}

			if(!spi_transmit(error, spi_clock_20M, 8, cmd, 0, 0, 0, 0))
			{
				if(error)
					string_append(error, "- during cmd transmit 8");
				return(false);
			}

			if(!spi_finish(error))
			{
				if(error)
					string_append(error, "- during cmd finish 8");
				return(false);
			}
		}

		if(length > 0)
		{
			if(!spi_start(error))
			{
				if(error)
					string_append(error, " - during data start 8");
				return(false);
			}

			for(current = 0; current < length; current++)
			{
				value = data[current] & 0xff;

				if(!spi_write(8, value))
				{
					if(error)
						string_append(error, " - during data write 8");
					return(false);
				}
			}

			if(io_write_pin(error, pin.dcx.io, pin.dcx.pin, 1) != io_ok)
			{
				if(error)
					string_append(error, " - during data io 8");
				return(false);
			}

			if(!spi_transmit(error, spi_clock_20M, 0, 0, 0, 0, 0, 0))
			{
				if(error)
					string_append(error, " - during data transmit 8");
				return(false);
			}

			if(!spi_finish(error))
			{
				if(error)
					string_append(error, " - during data finish 8");
				return(false);
			}
		}
	}
	else
	{
		if(!spi_start(error))
		{
			if(error)
				string_append(error, " - during start 9");
			return(false);
		}

		for(current = 0; current < length; current++)
		{
			value = data[current] | 0x100;

			if(!spi_write(9, value))
			{
				if(error)
					string_append(error, " - during write 9");
				return(false);
			}
		}

		if(!spi_transmit(error, spi_clock_20M, send_cmd ? 9 : 0, cmd, 0, 0, 0, 0))
		{
			if(error)
				string_append(error, " - during transmit 9");
			return(false);
		}

		if(!spi_finish(error))
		{
			if(error)
				string_append(error, " - during finish 9");
			return(false);
		}
	}

	return(true);
}

static attr_result_used bool send_command(string_t *error, unsigned int cmd)
{
	return(send_command_data(error, true, cmd, 0, 0));
}

static attr_result_used bool send_command_data_1_8(string_t *error, unsigned int cmd, unsigned int data)
{
	uint8_t bytes[1];

	bytes[0] = data & 0xff;

	return(send_command_data(error, true, cmd, 1, bytes));
}

static attr_result_used bool send_command_data_2_16(string_t *error, unsigned int cmd, unsigned int data_1, unsigned int data_2)
{
	uint8_t bytes[4];

	bytes[0] = (data_1 & 0xff00) >> 8;
	bytes[1] = (data_1 & 0x00ff) >> 0;
	bytes[2] = (data_2 & 0xff00) >> 8;
	bytes[3] = (data_2 & 0x00ff) >> 0;

	return(send_command_data(error, true, cmd, 4, bytes));
}

static attr_result_used bool flush_data(string_t *error)
{
	if(!display.buffer_dirty)
		return(true);

	if(pin.dcx.enabled && (io_write_pin(error, pin.dcx.io, pin.dcx.pin, 1) != io_ok))
		return(false);

	if(!spi_transmit(error, spi_clock_40M, 0, 0, 0, 0, 0, 0))
		return(false);

	if(!spi_finish(error))
		return(false);

	if(!spi_start(error))
		return(false);

	display.buffer_dirty = 0;

	return(true);
}

static attr_result_used bool output_data_8(string_t *error, unsigned int data)
{
	if(pin.dcx.enabled)
	{
		if(!spi_write(8, data))
		{
			if(!flush_data(error))
			{
				if(error)
					string_append(error, "flush data");
				return(false);
			}

			if(!spi_write(8, data))
			{
				if(error)
					string_append(error, "spi write");
				return(false);
			}
		}
	}
	else
	{
		data |= (1UL << 8);

		if(!spi_write(9, data))
		{
			if(!flush_data(error))
			{
				if(error)
					string_append(error, "flush data");
				return(false);
			}

			if(!spi_write(9, data))
			{
				if(error)
					string_append(error, "spi write");
				return(false);
			}
		}
	}

	display.buffer_dirty = 1;

	return(true);
}

static attr_result_used bool output_data_16(string_t *error, unsigned int data)
{
	if(pin.dcx.enabled)
	{
		if(!spi_write(16, data))
		{
			if(!flush_data(error))
			{
				if(error)
					string_append(error, "flush data");
				return(false);
			}

			if(!spi_write(16, data))
			{
				if(error)
					string_append(error, "spi write");
				return(false);
			}
		}
	}
	else
	{
		data = (((data & 0x0000ff00) << 1) | ((data & 0x000000ff) << 0)) | 0x20100;

		if(!spi_write(18, data))
		{
			if(!flush_data(error))
			{
				if(error)
					string_append(error, "flush data");
				return(false);
			}

			if(!spi_write(18, data))
			{
				if(error)
					string_append(error, "spi write");
				return(false);
			}
		}
	}

	display.buffer_dirty = 1;

	return(true);
}

static attr_result_used bool box(unsigned int r, unsigned int g, unsigned int b, unsigned int from_x, unsigned int from_y, unsigned int to_x, unsigned int to_y)
{
	string_new(, error, 64);
	unsigned int box_colour;
	unsigned int colour[2];
	unsigned int pixels, bulk_pixel, bulk_stride, write_bits;

	if(!flush_data(&error))
	{
		log("spitft box: spi error: %s\n", string_to_cstr(&error));
		return(false);
	}

	if(!send_command_data_2_16(&error, cmd_caset, from_x + display.x_offset, to_x + display.x_offset))
	{
		log("spitft box: spi error: %s\n", string_to_cstr(&error));
		return(false);
	}

	if(!send_command_data_2_16(&error, cmd_raset, from_y + display.y_offset, to_y + display.y_offset))
	{
		log("spitft box: spi error: %s\n", string_to_cstr(&error));
		return(false);
	}

	if(!send_command(&error, cmd_ramwr))
	{
		log("spitft box: spi error: %s\n", string_to_cstr(&error));
		return(false);
	}

	box_colour = rgb_to_16bit_colour(r, g, b) & 0x0000ffff;
	pixels = (to_x - from_x + 1) * (to_y - from_y + 1);

	colour[0] = (box_colour & 0xff00) >> 8;
	colour[1] = (box_colour & 0x00ff) >> 0;
	write_bits = 8;

	if(!pin.dcx.enabled)
	{
		write_bits = 9;
		colour[0] |= (1UL << 8);
		colour[1] |= (1UL << 8);
	}

	if(!spi_start(&error))
	{
		log("spitft box: spi error: %s\n", string_to_cstr(&error));
		return(false);
	}

	for(bulk_stride = 0; bulk_stride < 256; bulk_stride++)
	{
		if(!spi_write(write_bits, colour[0]))
			break;

		if(!spi_write(write_bits, colour[1]))
		{
			log("spitft box: spi error: %s\n", string_to_cstr(&error));
			return(false);
		}
	}

	if(bulk_stride >= 256)
	{
		log("spitft box: out of limits\n");
		return(false);
	}

	if(pin.dcx.enabled && (io_write_pin(&error, pin.dcx.io, pin.dcx.pin, 1) != io_ok))
		return(false);

	for(bulk_pixel = 0; (bulk_pixel + bulk_stride) < pixels; bulk_pixel += bulk_stride)
	{
		if(!spi_transmit(&error, spi_clock_40M, 0, 0, 0, 0, 0, 0))
		{
			log("spitft box: spi error: %s\n", string_to_cstr(&error));
			return(false);
		}

		if(!spi_finish(&error))
		{
			log("spitft box: spi error: %s\n", string_to_cstr(&error));
			return(false);
		}
	}

	if(!spi_start(&error))
	{
		log("spitft box: spi error: %s\n", string_to_cstr(&error));
		return(false);
	}

	for(; bulk_pixel < pixels; bulk_pixel++)
	{
		if(!spi_write(write_bits, colour[0]))
		{
			log("spitft box: spi error: %s\n", string_to_cstr(&error));
			return(false);
		}

		if(!spi_write(write_bits, colour[1]))
		{
			log("spitft box: spi error: %s\n", string_to_cstr(&error));
			return(false);
		}
	}

	if(!spi_transmit(&error, spi_clock_40M, 0, 0, 0, 0, 0, 0))
	{
		log("spitft box: spi error: %s\n", string_to_cstr(&error));
		return(false);
	}

	if(!spi_finish(&error))
	{
		log("spitft box: spi error: %s\n", string_to_cstr(&error));
		return(false);
	}

	return(true);
}

static attr_result_used bool clear_screen(void)
{
	return(box(0x00, 0x00, 0x00, 0, 0, display.x_size - 1, display.y_size - 1));
}

static attr_result_used bool text_send(unsigned int code)
{
	string_new(, error, 64);
	font_info_t font_info;
	font_cell_t font_cell;
	unsigned int x, y, r, g, b;
	unsigned int y_offset;
	unsigned int fg_colour, bg_colour, colour;

	if(!flush_data(&error))
	{
		log("spi: text_send 1: %s\n", string_to_cstr(&error));
		return(false);
	}

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

	if(!font_render(code, font_cell))
		return(false);

	r = 0xff;
	g = 0xff;
	b = 0xff;

	if(!pin.bright.enabled)
	{
		r >>= (4 - display.brightness);
		g >>= (4 - display.brightness);
		b >>= (4 - display.brightness);
	}

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

	if(!send_command_data_2_16(&error, cmd_caset, x + 0 + display.x_offset, x + font_info.width + display.x_offset - 1))
	{
		log("spi: text_send 2: %s\n", string_to_cstr(&error));
		return(false);
	}

	if(!send_command_data_2_16(&error, cmd_raset, y + 0 + display.y_offset, y + font_info.height + display.y_offset - 1))
	{
		log("spi: text_send 3: %s\n", string_to_cstr(&error));
		return(false);
	}

	if(!send_command(&error, cmd_ramwr))
	{
		log("spi: text_send 4: %s\n", string_to_cstr(&error));
		return(false);
	}

	for(y = 0; y < font_info.height; y++)
	{
		for(x = 0; x < font_info.width; x++)
		{
			colour = font_cell[y][x] ? fg_colour : bg_colour;

			if(!output_data_16(&error, colour))
			{
				log("spi: text_send 5: %s\n", string_to_cstr(&error));
				return(false);
			}
		}
	}

	if(!flush_data(&error))
	{
		log("spi: text_send 7: %s\n", string_to_cstr(&error));
		return(false);
	}

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
		x2 = x1 + (display.x_size - 1);
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
	string_new(, error, 64);

	int dcx_io, dcx_pin;
	int bright_io, bright_pin;
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

	if(config_get_int("spitft.dcx.io", &dcx_io, -1, -1) &&
			config_get_int("spitft.dcx.pin", &dcx_pin, -1, -1))
	{
		pin.dcx.enabled = 1;
		pin.dcx.io = dcx_io;
		pin.dcx.pin = dcx_pin;
	}
	else
	{
		pin.dcx.enabled = 0;
		pin.dcx.io = -1;
		pin.dcx.pin = -1;
	}

	if(config_get_int("spitft.bright.io", &bright_io, -1, -1) &&
			config_get_int("spitft.bright.pin", &bright_pin, -1, -1))
	{
		pin.bright.enabled = 1;
		pin.bright.io = bright_io;
		pin.bright.pin = bright_pin;
	}
	else
	{
		pin.bright.enabled = 0;
		pin.bright.io = -1;
		pin.bright.pin = -1;
	}

	if(config_get_int("spitft.cs.io", &user_cs_io, -1, -1) &&
			config_get_int("spitft.cs.pin", &user_cs_pin, -1, -1))
	{
		pin.user_cs.enabled = 1;
		pin.user_cs.io = user_cs_io;
		pin.user_cs.pin = user_cs_pin;
	}
	else
	{
		pin.user_cs.enabled = 0;
		pin.user_cs.io = -1;
		pin.user_cs.pin = -1;
	}

	text.column = text.row = 0;

	display.brightness = 4;
	display.graphic_mode = 0;
	display.logmode = 0;
	display.buffer_dirty = 0;
	display.enabled = 1;

	if(!spi_configure(&error, spi_mode_0, true, pin.user_cs.io, pin.user_cs.pin))
	{
		log("spitft init configure: %s\n", string_to_cstr(&error));
		return(false);
	}

	if(!send_command(&error, cmd_swreset))
	{
		log("spitft init: %s\n", string_to_cstr(&error));
		goto error;
	}

	msleep(10);

	if(!send_command(&error, cmd_sleepout))
	{
		log("spitft init: %s\n", string_to_cstr(&error));
		goto error;
	}

	msleep(10);

	if(!send_command_data_1_8(&error, cmd_colmod, colmod_16bpp))
	{
		log("spitft init: %s\n", string_to_cstr(&error));
		goto error;
	}

	if(!send_command(&error, cmd_noron))
	{
		log("spitft init: %s\n", string_to_cstr(&error));
		goto error;
	}

	if(!send_command(&error, cmd_dispon))
	{
		log("spitft init: %s\n", string_to_cstr(&error));
		goto error;
	}

	if(!send_command(&error, cmd_invoff))
		goto error;

	madctl = madctl_bgr;

	if(display.rotate)
		madctl |= madctl_mv;

	if(display.x_mirror)
		madctl |= madctl_mx;

	if(display.y_mirror)
		madctl |= madctl_my;

	if(!send_command_data_1_8(&error, cmd_madctl, madctl))
	{
		log("spitft init: %s\n", string_to_cstr(&error));
		goto error;
	}

	if(!clear_screen())
		return(false);

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

	text.column = text.row = 0;
	text.slot = slot;
	text.display_slot = slot % 2;
	display.logmode = logmode;

	if(!font_select(logmode))
		return(false);

	if(!font_get_info(&font_info))
		return(false);

	if(!display.logmode)
	{
		background_colour(text.slot, true, &r, &g, &b);

		x1 = 0;
		x2 = display.x_size - 1;

		if(text.display_slot == 0)
		{
			y1 = 0;
			y2 = font_info.height + 1;
		}
		else
		{
			y1 = (display.y_size - 1) / 2;
			y2 = y1 + font_info.height + 1;
		}

		if(!box(r, g, b, x1, y1, x2, y2))
			return(false);

		background_colour(text.slot, false, &r, &g, &b);

		x1 = 0;
		x2 = display.x_size - 1;

		if(text.display_slot == 0)
		{
			y1 = font_info.height + 2;
			y2 = (display.y_size - 1) / 2 - 1;
		}
		else
		{
			y1 = ((display.y_size - 1) / 2) + font_info.height + 2;
			y2 = display.y_size - 1;
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
	string_new(, error, 64);
	int ix;

	if(string_length(pixels) == 0)
		return(true);

	if((unsigned int)x >= display.x_size)
		return(false);

	if((unsigned int)y >= display.y_size)
		return(false);

	if((unsigned int)string_length(pixels) < (pixel_amount * 2))
		return(false);

	if(!flush_data(&error))
	{
		log("spitft plot: %s\n", string_to_cstr(&error));
		return(false);
	}

	string_setlength(pixels, pixel_amount * 2);

	if((x == 0) && (y == 0))
	{
		if(!send_command_data_2_16((string_t *)0, cmd_caset, display.x_offset, display.x_offset + display.x_size - 1))
			return(false);

		if(!send_command_data_2_16((string_t *)0, cmd_raset, display.y_offset, display.y_offset + display.y_size - 1))
			return(false);

		if(!send_command((string_t *)0, cmd_ramwr))
			return(false);
	}

	display.graphic_mode = 1;

	for(ix = 0; ix < string_length(pixels); ix++)
		if(!output_data_8((string_t *)0, string_at(pixels, ix)))
			return(false);

	if(!flush_data(&error))
	{
		log("spitft plot: %s\n", string_to_cstr(&error));
		return(false);
	}

	return(true);
}

app_action_t application_function_display_spitft(string_t *src, string_t *dst)
{
	unsigned int x_size, x_offset, x_mirror;
	unsigned int y_size, y_offset, y_mirror;
	unsigned int rotate;
	int dcx_io, dcx_pin;
	int bright_io, bright_pin;
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

		if((parse_int(8, src, &dcx_io, 0, ' ') == parse_ok) &&
				(parse_int(9, src, &dcx_pin, 0, ' ') == parse_ok))
		{
			if((dcx_io < -1) || (dcx_io >= io_id_size) || (dcx_pin < -1) || (dcx_pin >= max_pins_per_io))
			{
				string_append_cstr_flash(dst, help_description_display_spitft);
				return(app_action_error);
			}
		}
		else
		{
			dcx_io = -1;
			dcx_pin = -1;
		}

		if((parse_int(10, src, &bright_io, 0, ' ') == parse_ok) &&
				(parse_int(11, src, &bright_pin, 0, ' ') == parse_ok))
		{
			if((bright_io < -1) || (bright_io >= io_id_size) || (bright_pin < -1) || (bright_pin >= max_pins_per_io))
			{
				string_append_cstr_flash(dst, help_description_display_spitft);
				return(app_action_error);
			}
		}
		else
		{
			bright_io = -1;
			bright_pin = -1;
		}

		if((parse_int(11, src, &cs_io, 0, ' ') == parse_ok) &&
				(parse_int(12, src, &cs_pin, 0, ' ') == parse_ok))
		{
			if((cs_io < -1) || (cs_io >= io_id_size) || (cs_pin < -1) || (cs_pin >= max_pins_per_io))
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

			if((dcx_io < 0) || (dcx_pin < 0))
			{
				config_delete("spitft.dcx.io", false, -1, -1);
				config_delete("spitft.dcx.pin", false, -1, -1);
			}
			else
			{
				if(!config_set_uint("spitft.dcx.io", dcx_io, -1, -1))
					goto config_error;

				if(!config_set_uint("spitft.dcx.pin", dcx_pin, -1, -1))
					goto config_error;
			}

			if((bright_io < 0) || (bright_pin < 0))
			{
				config_delete("spitft.bright.io", false, -1, -1);
				config_delete("spitft.bright.pin", false, -1, -1);
			}
			else
			{
				if(!config_set_uint("spitft.bright.io", bright_io, -1, -1))
					goto config_error;

				if(!config_set_uint("spitft.bright.pin", bright_pin, -1, -1))
					goto config_error;
			}

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

	if((dcx_io >= 0) && (dcx_pin >= 0))
		string_format(dst, "> dcx io: %d, pin: %d\n", dcx_io, dcx_pin);

	if(!config_get_int("spitft.bright.io", &bright_io, -1, -1) ||
			!config_get_int("spitft.bright.pin", &bright_pin, -1, -1))
		bright_io = bright_pin = -1;

	if((bright_io >= 0) && (bright_pin >= 0))
		string_format(dst, "> brightness pwm io: %d, pin: %d\n", bright_io, bright_pin);

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
	static const roflash unsigned int brightness_factor[2][5] =
	{
		{
			1000,
			980,
			900,
			700,
			0,
		},
		{
			1000,
			920,
			800,
			650,
			0,
		},
	};

	if((brightness < 0) || (brightness > 4))
		return(false);

	display.brightness = brightness;

	if(pin.bright.enabled)
	{
		io_pin_mode_t mode;
		unsigned int lower_bound, upper_bound, max_value, value;
		int step;

		if(io_traits((string_t *)0, pin.bright.io, pin.bright.pin, &mode, &lower_bound, &upper_bound, &step, &value) == io_ok)
		{
			max_value = io_pin_max_value(pin.bright.io, pin.bright.pin);

			if(mode == io_pin_output_pwm1)
				value = max_value * brightness_factor[0][brightness] / 1000;
			else
				if(mode == io_pin_output_pwm2)
					value = max_value * brightness_factor[1][brightness] / 1000;
				else
					value = ~0;

			if(value != ~0UL)
				io_write_pin((string_t *)0, pin.bright.io, pin.bright.pin, value + lower_bound);
		}
	}

	return(true);
}

roflash const char help_description_display_spitft[] =	"> usage: display spitft\n"
														"> <x size> <x offset> <x mirror 0|1>\n"
														"> <y size> <y offset> <y mirror 0|1>\n"
														"> <rotate 0|1>\n"
														"> [<dcx io> <dcx pin>]\n"
														"> [<brightness pwm io> <brightness pwm pin>]\n"
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
