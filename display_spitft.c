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
	pad_1 = 0,
	pad_2 = 3,
	border_1 = 4,
	border_2 = 3,
};

enum
{
	cmd_nop =				0x00,
	cmd_swreset =			0x01,
	cmd_sleepout = 			0x11,
	cmd_noron =				0x13,
	cmd_invoff =			0x20,
	cmd_invon =				0x21,
	cmd_dispon =			0x29,
	cmd_caset =				0x2a,
	cmd_raset =				0x2b,
	cmd_ramwr =				0x2c,
	cmd_madctl =			0x36,
	cmd_idmoff =			0x38,
	cmd_idmon =				0x39,
	cmd_colmod =			0x3a,
	cmd_write_ctrl_disp =	0x53,
	cmd_if_mode_ctrl =		0xb0,
	cmd_frame_rate_ctrl =	0xb1,
	cmd_disp_inv_ctrl =		0xb4,
	cmd_disp_func_ctrl =	0xb6,
	cmd_entry_mode_set =	0xb7,
	cmd_power_ctrl_1 =		0xc0,
	cmd_power_ctrl_2 =		0xc1,
	cmd_power_ctrl_3 =		0xc2,
	cmd_vcom_ctrl =			0xc5,
	cmd_set_image_func =	0xe9,

	madctl_my =		(1 << 7),
	madctl_mx = 	(1 << 6),
	madctl_mv =		(1 << 5),
	madctl_ml =		(1 << 4),
	madctl_bgr =	(1 << 3),
	madctl_mh =		(1 << 2),

	colmod_18bpp = 	0b01100110,

	ifmodectrl_modir =	(0 << 7),
	ifmodectrl_bidir =	(1 << 7),

	dispinvctrl_column	=	0b000,
	dispinvctrl_1dot	=	0b001,
	dispinvctrl_2dot	=	0b010,
};

typedef struct
{
	unsigned int column;
	unsigned int row;
	unsigned int slot;
} text_t;

typedef struct
{
	unsigned int brightness;
	unsigned int x_size;
	unsigned int y_size;
	unsigned int x_offset;
	unsigned int y_offset;
	unsigned int spispeed;
	unsigned int enabled:1;
	unsigned int logmode:1;
	unsigned int graphic_mode:1;
	unsigned int x_mirror:1;
	unsigned int y_mirror:1;
	unsigned int rotate:1;
	unsigned int invert:1;
} display_t;

assert_size(display_t, 28);

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

static unsigned int rgb_to_18bit_colour(unsigned int r, unsigned int g, unsigned int b)
{
	unsigned int colour;

	r = (r & 0b11111100) >> 0;
	g = (g & 0b11111100) >> 0;
	b = (b & 0b11111100) >> 0;

	colour = (r << 16) | (g << 8) | (b << 0);

	return(colour);
}

static void background_colour(unsigned int slot, unsigned int *r, unsigned int *g, unsigned int *b)
{
	roflash static const unsigned int rgb[8][3] =
	{
		{	0x40,	0x40,	0x40	},	// dark grey	0
		{	0xff, 	0x00,	0x00	},	// red			1
		{	0x00,	0x88,	0x00	},	// green		2
		{	0x00,	0x00,	0xff	},	// blue			3
		{	0xff,	0x88,	0x00	},	// orange		4
		{	0xaa,	0x77,	0x00	},	// brow			5
		{	0xaa,	0xaa,	0xaa	},	// light grey	6
		{	0x00,	0x88,	0xff	},	// cyan		7
	};

	slot = slot % 8;

	*r = rgb[slot][0];
	*g = rgb[slot][1];
	*b = rgb[slot][2];

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

			if(!spi_transmit(error, display.spispeed, 8, cmd, 0, 0, 0, 0))
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

			if(!spi_transmit(error, display.spispeed, 0, 0, 0, 0, 0, 0))
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

		if(!spi_transmit(error, display.spispeed, send_cmd ? 9 : 0, cmd, 0, 0, 0, 0))
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

static attr_result_used bool write_spi_write_buffer(string_t *error)
{
	if(spi_write_bits_used() == 0)
		return(true);

	if(pin.dcx.enabled && (io_write_pin(error, pin.dcx.io, pin.dcx.pin, 1) != io_ok))
		return(false);

	if(!spi_transmit(error, display.spispeed, 0, 0, 0, 0, 0, 0))
		return(false);

	if(!spi_finish(error))
		return(false);

	if(!spi_start(error))
		return(false);

	return(true);
}

static attr_result_used bool output_data_8(string_t *error, unsigned int data)
{
	if(pin.dcx.enabled)
	{
		if(spi_write_bits_available() < 8)
		{
			if(!write_spi_write_buffer(error))
			{
				if(error)
					string_append(error, "flush data");
				return(false);
			}
		}

		if(!spi_write(8, data))
		{
			if(error)
				string_append(error, "spi write");
			return(false);
		}
	}
	else
	{
		data |= (1UL << 8);

		if(spi_write_bits_available() < 9)
		{
			if(!write_spi_write_buffer(error))
			{
				if(error)
					string_append(error, "flush data");
				return(false);
			}
		}

		if(!spi_write(9, data))
		{
			if(error)
				string_append(error, "spi write");
			return(false);
		}
	}

	return(true);
}

static attr_result_used bool output_data_24(string_t *error, unsigned int data)
{
	if(pin.dcx.enabled)
	{
		if(spi_write_bits_available() < 24)
		{
			if(!write_spi_write_buffer(error))
			{
				if(error)
					string_append(error, "flush data");
				return(false);
			}
		}

		if(!spi_write(24, data))
		{
			if(error)
				string_append(error, "spi write");
			return(false);
		}
	}
	else
	{
		data = (((data & 0x00ff0000) << 2 ) | ((data & 0x0000ff00) << 1) | ((data & 0x000000ff) << 0)) | 0x4020100;

		if(spi_write_bits_available() < 27)
		{
			if(!write_spi_write_buffer(error))
			{
				if(error)
					string_append(error, "flush data");
				return(false);
			}
		}

		if(!spi_write(27, data))
		{
			if(error)
				string_append(error, "spi write");
			return(false);
		}
	}

	return(true);
}

static attr_result_used bool box(unsigned int r, unsigned int g, unsigned int b, unsigned int from_x, unsigned int from_y, unsigned int to_x, unsigned int to_y)
{
	string_new(, error, 64);
	unsigned int box_colour;
	unsigned int colour[3];
	unsigned int pixels, bulk_pixel, bulk_stride, write_bits;

	if(!write_spi_write_buffer(&error))
	{
		log("spitft box: spi error 1: %s\n", string_to_cstr(&error));
		return(false);
	}

	if(!send_command_data_2_16(&error, cmd_caset, from_x + display.x_offset, to_x + display.x_offset))
	{
		log("spitft box: spi error 2: %s\n", string_to_cstr(&error));
		return(false);
	}

	if(!send_command_data_2_16(&error, cmd_raset, from_y + display.y_offset, to_y + display.y_offset))
	{
		log("spitft box: spi error 3: %s\n", string_to_cstr(&error));
		return(false);
	}

	if(!send_command(&error, cmd_ramwr))
	{
		log("spitft box: spi error 4: %s\n", string_to_cstr(&error));
		return(false);
	}

	box_colour = rgb_to_18bit_colour(r, g, b) & 0x00ffffff;
	pixels = (to_x - from_x + 1) * (to_y - from_y + 1);

	colour[0] = (box_colour & 0x00ff0000) >> 16;
	colour[1] = (box_colour & 0x0000ff00) >> 8;
	colour[2] = (box_colour & 0x000000ff) >> 0;
	write_bits = 8;

	if(!pin.dcx.enabled)
	{
		write_bits = 9;
		colour[0] |= (1UL << 8);
		colour[1] |= (1UL << 8);
		colour[2] |= (1UL << 8);
	}

	if(!spi_start(&error))
	{
		log("spitft box: spi error 5: %s\n", string_to_cstr(&error));
		return(false);
	}

	for(bulk_stride = 0; bulk_stride < 256; bulk_stride++)
	{
		if(spi_write_bits_available() < (write_bits * 3))
			break;

		if(!spi_write(write_bits, colour[0]))
		{
			log("spitft box: spi error 6: spi write\n");
			return(false);
		}

		if(!spi_write(write_bits, colour[1]))
		{
			log("spitft box: spi error 7: spi write\n");
			return(false);
		}

		if(!spi_write(write_bits, colour[2]))
		{
			log("spitft box: spi error 8: spi write\n");
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
		if(!spi_transmit(&error, display.spispeed, 0, 0, 0, 0, 0, 0))
		{
			log("spitft box: spi error 9: %s\n", string_to_cstr(&error));
			return(false);
		}

		if(!spi_finish(&error))
		{
			log("spitft box: spi error 10: %s\n", string_to_cstr(&error));
			return(false);
		}
	}

	if(!spi_start(&error))
	{
		log("spitft box: spi error 10: %s\n", string_to_cstr(&error));
		return(false);
	}

	for(; bulk_pixel < pixels; bulk_pixel++)
	{
		if(!spi_write(write_bits, colour[0]))
		{
			log("spitft box: spi error 12: %s\n", string_to_cstr(&error));
			return(false);
		}

		if(!spi_write(write_bits, colour[1]))
		{
			log("spitft box: spi error 13: spi_write\n");
			return(false);
		}

		if(!spi_write(write_bits, colour[2]))
		{
			log("spitft box: spi error 14: spi_write\n");
			return(false);
		}
	}

	if(!spi_transmit(&error, display.spispeed, 0, 0, 0, 0, 0, 0))
	{
		log("spitft box: spi error 15: %s\n", string_to_cstr(&error));
		return(false);
	}

	if(!spi_finish(&error))
	{
		log("spitft box: spi error 16: %s\n", string_to_cstr(&error));
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
	font_info_t font_info;
	font_cell_t font_cell;
	unsigned int x, y, max_x, max_y;
	unsigned int colour, fg_colour, bg_colour;
	string_new(, error, 64);
	unsigned int black, white;

	if(code == ' ')
		goto skip;

	if(!font_get_info(&font_info))
		return(false);

	if(!write_spi_write_buffer(&error))
	{
		log("spi: text_send 1: %s\n", string_to_cstr(&error));
		return(false);
	}

	x = text.column * font_info.width;
	y = text.row * font_info.height;

	black = 0x00;
	white = 0xff;

	if(!pin.bright.enabled)
		white >>= (4 - display.brightness);

	if(display.logmode)
	{
		max_x = x + font_info.width;
		max_y = y + font_info.height;

		fg_colour = rgb_to_18bit_colour(white, white, white);
		bg_colour = rgb_to_18bit_colour(black, black, black);
	}
	else
	{
		if(text.row == 0)
		{
			unsigned int r, g, b;

			fg_colour = rgb_to_18bit_colour(white, white, white);

			background_colour(text.slot, &r, &g, &b);

			if(!pin.bright.enabled)
			{
				r >>= (4 - display.brightness);
				g >>= (4 - display.brightness);
				b >>= (4 - display.brightness);
			}

			bg_colour = rgb_to_18bit_colour(r, g, b);

			x += border_1 + pad_1;
			y += border_1 + pad_1;

			max_x = x + font_info.width + border_1 + pad_1;
			max_y = y + font_info.height + border_1 + pad_1;
		}
		else
		{
			fg_colour = rgb_to_18bit_colour(black, black, black);
			bg_colour = rgb_to_18bit_colour(white, white, white);

			x += border_2 + pad_2;
			y += (2 * border_1) + pad_2;

			max_x = x + font_info.width + border_2 + pad_2;
			max_y = y + font_info.height + border_2 + pad_2;
		}
	}

	if((max_x >= display.x_size) || (max_y >= display.y_size))
		goto skip;

	if(!font_render(code, font_cell))
		return(false);

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

			if(!output_data_24(&error, colour))
			{
				log("spi: text_send 5: %s\n", string_to_cstr(&error));
				return(false);
			}
		}
	}

	if(!write_spi_write_buffer(&error))
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

static attr_result_used bool text_send_ascii_string(const char *string)
{
	unsigned int current;

	for(current = 0; string[current]; current++)
		if(!text_send(string[current]))
			return(false);

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
	unsigned int invert;
	unsigned int madctl;
	unsigned int speed;

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

	if(!config_get_uint("spitft.invert", &invert, -1, -1))
		invert = 0;

	if(!config_get_uint("spitft.speed", &speed, -1, -1))
		speed = spi_clock_10M;

	display.x_size = x_size;
	display.x_offset = x_offset;
	display.y_size = y_size;
	display.y_offset = y_offset;
	display.x_mirror = x_mirror;
	display.y_mirror = y_mirror;
	display.rotate = rotate;
	display.invert = invert;
	display.spispeed = speed;

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

	msleep(5);

	if(!send_command_data_1_8(&error, cmd_colmod, colmod_18bpp))
	{
		log("spitft init: %s\n", string_to_cstr(&error));
		goto error;
	}

	if(!send_command_data_1_8(&error, cmd_if_mode_ctrl, ifmodectrl_modir))
	{
		log("spitft init: %s\n", string_to_cstr(&error));
		goto error;
	}

	if(!send_command(&error, cmd_sleepout))
	{
		log("spitft init: %s\n", string_to_cstr(&error));
		goto error;
	}

	msleep(5);

	if(!send_command(&error, cmd_noron))
	{
		log("spitft init: %s\n", string_to_cstr(&error));
		goto error;
	}

	if(!send_command(&error, display.invert ? cmd_invon : cmd_invoff))
	{
		log("spitft init: %s\n", string_to_cstr(&error));
		goto error;
	}

	if(!send_command(&error, cmd_dispon))
	{
		log("spitft init: %s\n", string_to_cstr(&error));
		goto error;
	}

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
	display.logmode = logmode;

	if(!font_select(logmode))
		return(false);

	if(!font_get_info(&font_info))
		return(false);

	if(!display.logmode)
	{
		background_colour(slot, &r, &g, &b);

		x1 = 0;
		y1 = 0;
		x2 = display.x_size - 1;
		y2 = font_info.height + (2 * border_1) + border_2 - 1;

		if(!box(r, g, b, x1, y1, x2, y2))
			return(false);

		x1 = 0;
		y1 = font_info.height + (2 * border_1);
		x2 = border_2 - 1;
		y2 = display.y_size - 1;

		if(!box(r, g, b, x1, y1, x2, y2))
			return(false);

		x1 = display.x_size - border_2;
		y1 = font_info.height + (2 * border_1);
		x2 = display.x_size - 1;
		y2 = display.y_size - 1;

		if(!box(r, g, b, x1, y1, x2, y2))
			return(false);

		x1 = 0;
		y1 = display.y_size - border_2;
		x2 = display.x_size - 1;
		y2 = display.y_size - 1;

		if(!box(r, g, b, x1, y1, x2, y2))
			return(false);

		x1 = border_2;
		y1 = font_info.height + (2 * border_1);
		x2 = display.x_size - 1 - border_2;
		y2 = display.y_size - 1 - border_2;

		if(!box(0xff, 0xff, 0xff, x1, y1, x2, y2))
			return(false);

		int column;
		unsigned int columns;
		unsigned int hour, minute, month, day;
		string_new(, time_date, 32);

		string_clear(&time_date);
		time_get(&hour, &minute, 0, 0, &month, &day);
		string_format(&time_date, "%02u/%02u %02u:%02u ", day, month, hour, minute);

		columns = display.x_size / font_info.width;
		column = columns - string_length(&time_date);

		if(column >= 0)
		{
			text.column = column;
			if(!text_send_ascii_string(string_to_cstr(&time_date)))
				return(false);
			text.column = 0;
		}
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
	enum { display_depth_bytes = 3 };
	string_new(, error, 64);
	int ix;

	if(string_length(pixels) == 0)
		return(true);

	if((unsigned int)x >= display.x_size)
		return(false);

	if((unsigned int)y >= display.y_size)
		return(false);

	if((unsigned int)string_length(pixels) < (pixel_amount * display_depth_bytes))
		return(false);

	if(!write_spi_write_buffer(&error))
	{
		log("spitft plot: %s\n", string_to_cstr(&error));
		return(false);
	}

	string_setlength(pixels, pixel_amount * display_depth_bytes);

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

	if(!write_spi_write_buffer(&error))
	{
		log("spitft plot: %s\n", string_to_cstr(&error));
		return(false);
	}

	return(true);
}

roflash const char help_description_display_spitft[] =	"> usage: display spitft\n"
														"> <x size> <x offset> <x mirror 0|1>\n"
														"> <y size> <y offset> <y mirror 0|1>\n"
														"> <rotate 0|1>\n"
														"> <invert 0|1>\n"
														"> [<spi interface speed index, -1 = default>]\n"
														"> [<dcx io> <dcx pin>]\n"
														"> [<brightness pwm io> <brightness pwm pin>]\n"
														"> [<user cs io> <user cs pin>]\n";

app_action_t application_function_display_spitft(app_params_t *parameters)
{
	unsigned int x_size, x_offset, x_mirror;
	unsigned int y_size, y_offset, y_mirror;
	unsigned int rotate;
	unsigned int invert;
	int speed;
	int dcx_io, dcx_pin;
	int bright_io, bright_pin;
	int cs_io, cs_pin;

	if(parse_uint(1, parameters->src, &x_size, 0, ' ') == parse_ok)
	{
		if((parse_uint(2, parameters->src, &x_offset, 0, ' ') != parse_ok) ||
			(parse_uint(3, parameters->src, &x_mirror, 0, ' ') != parse_ok))
		{
			string_append_cstr_flash(parameters->dst, help_description_display_spitft);
			return(app_action_error);
		}

		if((parse_uint(4, parameters->src, &y_size, 0, ' ') != parse_ok) ||
				(parse_uint(5, parameters->src, &y_offset, 0, ' ') != parse_ok) ||
				(parse_uint(6, parameters->src, &y_mirror, 0, ' ') != parse_ok))
		{
			string_append_cstr_flash(parameters->dst, help_description_display_spitft);
			return(app_action_error);
		}

		if(parse_uint(7, parameters->src, &rotate, 0, ' ') != parse_ok)
		{
			string_append_cstr_flash(parameters->dst, help_description_display_spitft);
			return(app_action_error);
		}

		if(parse_uint(8, parameters->src, &invert, 0, ' ') != parse_ok)
		{
			string_append_cstr_flash(parameters->dst, help_description_display_spitft);
			return(app_action_error);
		}

		if(parse_int(9, parameters->src, &speed, 0, ' ') == parse_ok)
		{
			if((speed < -1) || (speed >= spi_clock_size))
			{
				string_append_cstr_flash(parameters->dst, help_description_display_spitft);
				return(app_action_error);
			}
		}
		else
			speed = -1;

		if((parse_int(10, parameters->src, &dcx_io, 0, ' ') == parse_ok) &&
				(parse_int(11, parameters->src, &dcx_pin, 0, ' ') == parse_ok))
		{
			if((dcx_io < -1) || (dcx_io >= io_id_size) || (dcx_pin < -1) || (dcx_pin >= max_pins_per_io))
			{
				string_append_cstr_flash(parameters->dst, help_description_display_spitft);
				return(app_action_error);
			}
		}
		else
		{
			dcx_io = -1;
			dcx_pin = -1;
		}

		if((parse_int(12, parameters->src, &bright_io, 0, ' ') == parse_ok) &&
				(parse_int(13, parameters->src, &bright_pin, 0, ' ') == parse_ok))
		{
			if((bright_io < -1) || (bright_io >= io_id_size) || (bright_pin < -1) || (bright_pin >= max_pins_per_io))
			{
				string_append_cstr_flash(parameters->dst, help_description_display_spitft);
				return(app_action_error);
			}
		}
		else
		{
			bright_io = -1;
			bright_pin = -1;
		}

		if((parse_int(14, parameters->src, &cs_io, 0, ' ') == parse_ok) &&
				(parse_int(15, parameters->src, &cs_pin, 0, ' ') == parse_ok))
		{
			if((cs_io < -1) || (cs_io >= io_id_size) || (cs_pin < -1) || (cs_pin >= max_pins_per_io))
			{
				string_append_cstr_flash(parameters->dst, help_description_display_spitft);
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

			if(!config_set_uint("spitft.invert", invert, -1, -1))
				goto config_error;

			if(speed < 0)
				config_delete("spitft.speed", false, -1, -1);
			else
				if(!config_set_uint("spitft.speed", speed, -1, -1))
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
		string_format(parameters->dst, "no spi tft display configured\n");
		return(app_action_error);
	}

	if(!config_get_uint("spitft.x.offset", &x_offset, -1, -1))
		x_offset = 0;

	if(!config_get_uint("spitft.x.mirror", &x_mirror, -1, -1))
		x_mirror = 0;

	string_format(parameters->dst, "> x size: %u, offset: %u, mirror: %u\n", x_size, x_offset, x_mirror);

	if(!config_get_uint("spitft.y.size", &y_size, -1, -1))
		y_size = 0;

	if(!config_get_uint("spitft.y.offset", &y_offset, -1, -1))
		y_offset = 0;

	if(!config_get_uint("spitft.y.mirror", &y_mirror, -1, -1))
		x_mirror = 0;

	string_format(parameters->dst, "> y size: %u, offset: %u, mirror: %u\n", y_size, y_offset, y_mirror);

	if(!config_get_uint("spitft.rotate", &rotate, -1, -1))
		rotate = 0;

	string_format(parameters->dst, "> rotate: %u\n", rotate);

	if(!config_get_uint("spitft.invert", &invert, -1, -1))
		invert = 0;

	string_format(parameters->dst, "> invert: %u\n", invert);

	if(!config_get_int("spitft.speed", &speed, -1, -1))
		speed = -1;

	if(speed >= 0)
		string_format(parameters->dst, "> speed: %d\n", speed);
	else
		string_append(parameters->dst, "> speed default: 10 Mhz\n");

	if(!config_get_int("spitft.dcx.io", &dcx_io, -1, -1) ||
			!config_get_int("spitft.dcx.pin", &dcx_pin, -1, -1))
		dcx_io = dcx_pin = -1;

	if((dcx_io >= 0) && (dcx_pin >= 0))
		string_format(parameters->dst, "> dcx io: %d, pin: %d\n", dcx_io, dcx_pin);

	if(!config_get_int("spitft.bright.io", &bright_io, -1, -1) ||
			!config_get_int("spitft.bright.pin", &bright_pin, -1, -1))
		bright_io = bright_pin = -1;

	if((bright_io >= 0) && (bright_pin >= 0))
		string_format(parameters->dst, "> brightness pwm io: %d, pin: %d\n", bright_io, bright_pin);

	if(!config_get_int("spitft.cs.io", &cs_io, -1, -1) ||
			!config_get_int("spitft.cs.pin", &cs_pin, -1, -1))
		cs_io = cs_pin = -1;

	if((cs_io >= 0) && (cs_pin >= 0))
		string_format(parameters->dst, "> cs io: %d, pin: %d\n", cs_io, cs_pin);

	return(app_action_normal);

config_error:
	config_abort_write();
	string_clear(parameters->dst);
	string_append(parameters->dst, "> cannot set config\n");
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
	infostruct->pixel_mode = display_pixel_mode_24_rgb;

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
	plot,
	(void *)0,
};
