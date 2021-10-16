#include "display.h"
#include "display_seeed.h"
#include "i2c.h"
#include "config.h"
#include "sys_time.h"
#include "dispatch.h"

#include <stdint.h>
#include <stdbool.h>

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

enum
{
	display_address = 0x51,
	display_width = 128,
	display_height = 64,
	display_text_width = 21,
	display_font_width = 6,
	display_font_height = 8,
	mapeof = 0xffffffff,
};

enum
{
	reg_FontModeRegAddr =			0x01,
	reg_CharXPosRegAddr =			0x02,
	reg_CharYPosRegAddr =			0x03,
	reg_CursorConfigRegAddr =		0x16,
	reg_DisRAMAddr =				0x20,
	reg_WriteRAM_XPosRegAddr =		0x23,
	reg_WriteRAM_YPosRegAddr =		0x24,
	reg_DisplayConfigRegAddr =		0x80,
	reg_WorkingModeRegAddr =		0x81,
	reg_BackLightConfigRegAddr =	0x82,
	reg_ContrastConfigRegAddr =		0x83,
};

enum
{
	font_6x8 =		0x00,
	font_6x12 =		0x01,
	font_8x16_1 =	0x02,
	font_8x16_2 =	0x03,
	font_10x20 =	0x04,
	font_12x24 =	0x05,
	font_16_32 =	0x06,
};

enum
{
	render_autonl_autoincr =		0b00000000,
	render_autonl_manualincr =		0b00010000,
	render_manualnl_autoincr =		0b00100000,
	render_manualnl_manualincr =	0b00110000,
};

enum
{
	charmode_white_inc_background =		0b00000000,
	charmode_white_excl_background =	0b01000000,
	charmode_black_inc_background =		0b10000000,
	charmode_black_excl_background =	0b11000000,
};

enum
{
	cursor_off =	0b00000000,
	cursor_on =		0b10000000,
};

enum
{
	display_mode_reverse =	0b00000000,
	display_mode_normal =	0b10000000,
};

enum
{
	workmode_char =				0b00000000,
	workmode_bitmap =			0b00000001,
	workmode_ram =				0b00000010,
	workmode_backlight_off =	0b00000000,
	workmode_backlight_on =		0b00000100,
	workmode_logo_off =			0b00000000,
	workmode_logo_on =			0b00001000,
	workmode_flip =				0b01000000,
	workmode_extra =			0b01010000,
};

roflash static const unicode_map_t unicode_map[] =
{
	{	0x2190, 0x1c	},	//	← 
	{	0x2192, 0x1d	},	//	→ 
	{	0x2191,	0x1e	},	//	↑ 
	{	0x2193,	0x1f	},	//	↓ 
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
		mapeof, 0, // EOF
		{ },
	},
};

static bool display_inited = false;
static bool display_logmode = false;
static bool display_disable_text = false;
static bool display_picture_valid = false;
static unsigned int display_slot_offset;
static unsigned int display_x, display_y;
static unsigned int display_buffer_current;
static unsigned int display_picture_load_flash_sector;

static const char pbm_header[] = "P4\n128 64\n";

attr_inline unsigned int text_height(void)
{
	if(display_logmode)
		return(8);
	else
		return(4);
}

static bool attr_result_used display_data_flush(void)
{
	if((display_buffer_current > 1) && i2c_send(display_address, display_buffer_current, display_buffer) != i2c_error_ok)
		return(false);

	display_buffer_current = 0;
	display_buffer[display_buffer_current++] = reg_DisRAMAddr;

	return(true);
}

attr_inline bool display_data_output(unsigned int byte)
{
	if(((display_buffer_current + 1) >= display_buffer_size) && !display_data_flush())
		return(false);

	display_buffer[display_buffer_current++] = (uint8_t)byte;

	return(true);
}

static bool attr_result_used display_cursor(unsigned int x, unsigned int y)
{
	if(!display_data_flush())
		return(false);

	if(i2c_send3(display_address, reg_CharXPosRegAddr, x, y) != i2c_error_ok)
		return(false);

	return(true);
}

static bool attr_result_used text_goto(int x, int y)
{
	if(x >= 0)
		display_x = x;

	if(y >= 0)
		display_y = y;

	x = display_x;
	y = display_y;

	if(((unsigned int)x >= display_text_width) || ((unsigned int)y >= text_height()) || (display_slot_offset > 1))
		return(true);

	if(!display_cursor(x * display_font_width, (display_slot_offset * 4 * display_font_height) + (y * display_font_height)))
		return(false);

	return(true);
}

static bool attr_result_used text_send(unsigned int byte)
{
	if((display_x < display_text_width) && (display_y < text_height()) && !display_data_output(byte))
		return(false);

	display_x++;

	return(true);
}

static bool attr_result_used text_newline(void)
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

static bool attr_result_used udg_send(unsigned int udg)
{
	uint8_t ram_byte[9];
	unsigned int byte, bit;
	unsigned int gx = display_x * display_font_width;
	unsigned int gy = display_y;

	for(byte = 0; byte < sizeof(ram_byte); byte++)
		ram_byte[byte] = 0;

	ram_byte[0] = reg_DisRAMAddr;

	for(byte = 0; byte < 8; byte++)
		for(bit = 0; bit < 6; bit++)
			if(udg_map[udg].pattern[byte] & (1 << (5 - bit)))
				ram_byte[bit + 1] |= 1 << byte;

	if(gx > 0)
		gx--;

	if(!display_data_output(' '))
		return(false);

	if(!display_data_flush())
		return(false);

	if(i2c_send2(display_address, reg_WorkingModeRegAddr, workmode_extra | workmode_ram | workmode_backlight_on | workmode_logo_off) != i2c_error_ok)
		return(false);

	if(i2c_send3(display_address, reg_WriteRAM_XPosRegAddr, gx, gy) != i2c_error_ok)
		return(false);

	if(i2c_send(display_address, 7, ram_byte) != i2c_error_ok)
		return(false);

	if(i2c_send2(display_address, reg_WorkingModeRegAddr, workmode_extra | workmode_char | workmode_backlight_on | workmode_logo_off) != i2c_error_ok)
		return(false);

	return(true);
}

static bool standout(bool onoff)
{
	unsigned int value = render_manualnl_autoincr | font_6x8 | (onoff ? charmode_white_inc_background : charmode_black_inc_background);

	return(i2c_send2(display_address, reg_FontModeRegAddr, value) == i2c_error_ok);
}

static bool bright(int brightness)
{
	roflash static const unsigned int bright_to_internal[5][2] =
	{
		{	0,	0	},
		{	25,	20	},
		{	25,	40	},
		{	25,	80	},
		{	25,	127	},
	};

	if(brightness > 4)
		return(false);

	if(i2c_send2(display_address, reg_ContrastConfigRegAddr, bright_to_internal[brightness][0]) != i2c_error_ok)
		return(false);

	if(i2c_send2(display_address, reg_BackLightConfigRegAddr, bright_to_internal[brightness][1]) != i2c_error_ok)
		return(false);

	return(true);
}

static bool picture_load(unsigned int picture_load_index)
{
	bool success = false;

	display_picture_load_flash_sector = (picture_load_index ? PICTURE_FLASH_OFFSET_1 : PICTURE_FLASH_OFFSET_0) / SPI_FLASH_SEC_SIZE;

	if(string_size(&flash_sector_buffer) < SPI_FLASH_SEC_SIZE)
	{
		log("display seeed: load picture: sector buffer too small: %u\n", flash_sector_buffer_use);
		goto error;
	}

	if((flash_sector_buffer_use != fsb_free) && (flash_sector_buffer_use != fsb_config_cache))
	{
		log("display seeed: load picture: flash buffer not free, used by: %u\n", flash_sector_buffer_use);
		goto error;
	}

	flash_sector_buffer_use = fsb_display_picture;

	if(spi_flash_read(display_picture_load_flash_sector * SPI_FLASH_SEC_SIZE, string_buffer_nonconst(&flash_sector_buffer), SPI_FLASH_SEC_SIZE) != SPI_FLASH_RESULT_OK)
	{
		log("display seeed: load picture: failed to read sector: 0x%x\n", display_picture_load_flash_sector);
		goto error;
	}

	string_setlength(&flash_sector_buffer, sizeof(pbm_header) - 1);

	if(!string_match_cstr(&flash_sector_buffer, pbm_header))
	{
		log("display seeed: load picture: invalid image header: %s\n", string_to_cstr(&flash_sector_buffer));
		goto error;
	}

	success = true;

error:
	if(flash_sector_buffer_use == fsb_display_picture)
		flash_sector_buffer_use = fsb_free;
	display_picture_valid = success;
	return(success);
}


static bool init(void)
{
	if(!display_data_flush()) // init data buffer
		return(false);

	if(!standout(0))
		return(false);

	if(i2c_send2(display_address, reg_CursorConfigRegAddr, cursor_off | 0) != i2c_error_ok)
		return(false);

	if(i2c_send2(display_address, reg_DisplayConfigRegAddr, display_mode_normal) != i2c_error_ok)
		return(false);

	if(i2c_send2(display_address, reg_WorkingModeRegAddr, workmode_extra | workmode_char | workmode_backlight_on | workmode_logo_off) != i2c_error_ok)
		return(false);

	if(!picture_load(0))
		return(false);

	if(!bright(1))
		return(false);

	display_inited = true;

	return(true);
}

static bool begin(unsigned int select_slot, bool logmode)
{
	static unsigned int counter = 0;

	if(!display_inited)
	{
		log("! display seeed not inited\n");
		return(false);
	}

	if(display_disable_text)
		return(true);

	display_logmode = logmode;
	display_slot_offset = (counter++) & 0x01 ? 1 : 0;

	if(!text_goto(0, 0))
		return(false);

	return(true);
}

static bool output(unsigned int length, const unsigned int unicode[])
{
	const unicode_map_t *unicode_map_ptr;
	const udg_map_t *udg_map_ptr;
	unsigned int current_index, current;

	if(display_disable_text)
		return(true);

	for(current_index = 0; current_index < length; current_index++)
	{
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

		for(udg_map_ptr = udg_map; udg_map_ptr->unicode != mapeof; udg_map_ptr++)
			if((udg_map_ptr->unicode == current))
			{
				if(!udg_send(udg_map_ptr->internal))
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

static bool picture_valid(void)
{
	return(display_picture_valid);
}

static bool layer_select(unsigned int layer)
{
	bool success = false;
	unsigned int row, column, output, bit, offset, bitoffset;
	const uint8_t *bitmap;

	if(layer == 0)
	{
		display_disable_text = false;
		return(true);
	}

	if(!display_picture_valid)
	{
		log("display seeed: load picture: invalid image\n");
		return(false);
	}

	display_disable_text = true;

	if((flash_sector_buffer_use != fsb_free) && (flash_sector_buffer_use != fsb_config_cache))
	{
		log("display seeed: load picture: flash buffer not free, used by: %u\n", flash_sector_buffer_use);
		return(false);
	}

	flash_sector_buffer_use = fsb_display_picture;

	if(spi_flash_read(display_picture_load_flash_sector * SPI_FLASH_SEC_SIZE, string_buffer_nonconst(&flash_sector_buffer), SPI_FLASH_SEC_SIZE) != SPI_FLASH_RESULT_OK)
	{
		log("display seeed: load picture: failed to read sector: 0x%x\n", display_picture_load_flash_sector);
		goto error;
	}

	string_setlength(&flash_sector_buffer, sizeof(pbm_header) - 1);

	if(!string_match_cstr(&flash_sector_buffer, pbm_header))
	{
		log("display seeed: show picture: invalid image header: %s\n", string_to_cstr(&flash_sector_buffer));
		goto error;
	}

	string_setlength(&flash_sector_buffer, SPI_FLASH_SEC_SIZE);
	bitmap = (const uint8_t *)string_buffer(&flash_sector_buffer) + (sizeof(pbm_header) - 1);

	i2c_send2(display_address, reg_WorkingModeRegAddr, workmode_extra | workmode_ram | workmode_backlight_on | workmode_logo_off);

	for(row = 0; row < (display_height / 8); row++)
	{
		if(!display_data_flush())
			return(false);

		if(i2c_send3(display_address, reg_WriteRAM_XPosRegAddr, 0, row) != i2c_error_ok)
			return(false);

		for(column = 0; column < display_width; column++)
		{
			output = 0;

			for(bit = 0; bit < 8; bit++)
			{
				offset		= (row * 8) * (display_width / 8);
				offset		+= bit * (display_width / 8);
				offset		+= column / 8;
				bitoffset	= column % 8;

				if(bitmap[offset] & (1 << (7 - bitoffset)))
					output |= 1 << bit;
			}

			if(!display_data_output(output))
				return(false);
		}
	}

	success = true;

error:
	flash_sector_buffer_use = fsb_free;
	if(!display_data_flush())
		return(false);
	if(i2c_send2(display_address, reg_WorkingModeRegAddr, workmode_extra | workmode_char | workmode_backlight_on | workmode_logo_off) != i2c_error_ok)
		return(false);
	if(!text_goto(-1, -1))
		return(false);
	return(success);
}

roflash const display_info_t display_info_seeed =
{
	"seeed LCD", "128x64 LCD",
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
	picture_valid
};
