#include "display.h"
#include "display_lcd.h"
#include "io.h"
#include "config.h"

static bool_t inited = false;
static bool_t nibble_mode;

static int bl_io;
static int bl_pin;
static int lcd_io;
static int lcd_pin[io_lcd_size];
static unsigned int pin_mask;

attr_inline unsigned int bit_to_pin(unsigned int value, unsigned int src_bitindex, unsigned int function)
{
	unsigned int pin = 0;

	if(value & (1 << src_bitindex))
		pin = 1;

	return(pin << lcd_pin[function]);
}

irom static bool send_byte_raw(int byte, bool_t data)
{
	unsigned int pins = 0;

	io_set_mask((string_t *)0, lcd_io, pin_mask, 0);

	pins |= bit_to_pin(!!data, 0, io_lcd_rs);
	pins |= bit_to_pin(0, 0, io_lcd_rw);
	pins |= bit_to_pin(byte, 0, io_lcd_d0);
	pins |= bit_to_pin(byte, 1, io_lcd_d1);
	pins |= bit_to_pin(byte, 2, io_lcd_d2);
	pins |= bit_to_pin(byte, 3, io_lcd_d3);
	pins |= bit_to_pin(byte, 4, io_lcd_d4);
	pins |= bit_to_pin(byte, 5, io_lcd_d5);
	pins |= bit_to_pin(byte, 6, io_lcd_d6);
	pins |= bit_to_pin(byte, 7, io_lcd_d7);
	pins |= bit_to_pin(1, 0, io_lcd_e);

	io_set_mask((string_t *)0, lcd_io, pin_mask, pins);

	return(true);
}

irom static bool send_byte(int byte, bool data)
{
	if(nibble_mode)
	{
		if(!send_byte_raw((byte & 0xf0) << 0, data))
			return(false);

		if(!send_byte_raw((byte & 0x0f) << 4, data))
			return(false);

		return(true);
	}

	return(send_byte_raw(byte, data));
}

irom bool_t display_lcd_init(void)
{
	io_config_pin_entry_t *pin_config;
	int io, pin, ix, byte, x, y;

	bl_io	= -1;
	bl_pin	= -1;
	lcd_io	= -1;
	pin_mask = 0;

	for(pin = 0; pin < io_lcd_size; pin++)
		lcd_pin[pin] = -1;

	for(io = 0; io < io_id_size; io++)
	{
		for(pin = 0; pin < max_pins_per_io; pin++)
		{
			pin_config = &io_config[io][pin];

			if(pin_config->mode == io_pin_lcd)
			{
				if(pin_config->shared.lcd.pin_use == io_lcd_bl)
				{
					bl_io = io;
					bl_pin = pin;
				}
				else
				{
					if((lcd_io >= 0) && (io != lcd_io))
					{
						log("* lcd: pins must be on same device\n");
						continue;
					}

					lcd_io = io;
					lcd_pin[pin_config->shared.lcd.pin_use] = pin;
					pin_mask |= 1 << pin;
				}
			}
		}
	}

	if(lcd_io < 0)
		return(false);

	if(lcd_pin[io_lcd_rs] < 0)
		return(false);

	if(lcd_pin[io_lcd_e] < 0)
		return(false);

	if(lcd_pin[io_lcd_d4] < 0)
		return(false);

	if(lcd_pin[io_lcd_d5] < 0)
		return(false);

	if(lcd_pin[io_lcd_d6] < 0)
		return(false);

	if(lcd_pin[io_lcd_d7] < 0)
		return(false);

	nibble_mode = false;

	if(lcd_pin[io_lcd_d0] < 0)
		nibble_mode = true;

	if(lcd_pin[io_lcd_d1] < 0)
		nibble_mode = true;

	if(lcd_pin[io_lcd_d2] < 0)
		nibble_mode = true;

	if(lcd_pin[io_lcd_d3] < 0)
		nibble_mode = true;

	// robust initialisation sequence,
	// from http://web.alfredstate.edu/weimandn/lcd/lcd_initialization/lcd_initialization_index.html

	for(pin = 3; pin > 0; pin--)
	{
		if(!send_byte_raw(0b00110000, false))	// 3 x special "reset" command, low nibble ignored
			return(false);

		msleep(5);
	}

	if(nibble_mode)
	{
		if(!send_byte_raw(0b00100000, false))	// set 4 bit mode, low nibble ignored
			return(false);

		msleep(2);

		if(!send_byte(0b00101000, false))		// set 4 bit mode / two lines / 5x8 font
			return(false);
	}
	else
		if(!send_byte(0b00111000, false))		// set 8 bit mode / two lines / 5x8 font
			return(false);

	if(!send_byte(0b00000001, false))			// clear screen
		return(false);

	msleep(2);

	if(!send_byte(0b00000110, false))			// cursor move direction = LTR / no display shift
		return(false);

	if(!send_byte(0b00001100, false))			// display on, cursor off, blink off
		return(false);

	if(!send_byte(0b01000000, false))			// start writing to CGRAM @ 0
		return(false);

	for(ix = 0; ix < display_common_udg_size; ix++)
		for(byte = 0; byte < display_common_udg_byte_size; byte++)
			if(!send_byte(display_common_udg[ix].pattern[byte], true))
				return(false);

	inited = true;

	for(y = 0; y < display_common_buffer_rows; y++)
		for(x = 0; x < display_common_buffer_columns; x++)
			display_common_buffer[y][x] = ' ';

	for(ix = 0; ix < display_common_buffer_rows; ix++)
		display_common_row_status.row[ix].dirty = 1;

	return(display_lcd_bright(1));
}

typedef enum
{
	cmd_off_off_off = 0b00001000,	// display off, cursor off, blink off
	cmd_on_off_off = 0b00001100,	// display on, cursor off, blink off
} cmd_t;

irom bool_t display_lcd_bright(int brightness)
{
	static const unsigned int bls[5] = { 0, 1024, 4096, 16384, 65535 };
	static const cmd_t cmds[5] = { cmd_off_off_off, cmd_on_off_off, cmd_on_off_off, cmd_on_off_off, cmd_on_off_off };
	unsigned int pwm, pwm_period;
	string_init(varname_pwmperiod, "pwm.period");

	if((brightness < 0) || (brightness > 4))
		return(false);

	if(!send_byte(cmds[brightness], false))
		return(false);

	if(!config_get_int(&varname_pwmperiod, -1, -1, &pwm_period))
		pwm_period = 65536;

	pwm = bls[brightness] / (65536 / pwm_period);

	if((bl_io >= 0) && (bl_pin >= 0))
		io_write_pin((string_t *)0, bl_io, bl_pin, pwm);

	return(true);
}

irom bool_t display_lcd_set(const char *tag, const char *text)
{
	if(!inited)
		return(false);

	return(display_common_set(tag, text,
				display_common_map_size, display_common_map,
				display_common_udg_size, display_common_udg));
}

irom bool_t display_lcd_show(void)
{
	static const uint8_t offset[4][2] =
	{
		{ 0,  0 },
		{ 0,  64 },
		{ 20,  0 },
		{ 20, 64 }
	};

	int x, y;

	if(!inited)
		return(false);

	for(y = 0; y < display_common_buffer_rows; y++)
		if(display_common_row_status.row[y].dirty)
			break;

	if(y >= display_common_buffer_rows)
		return(false);

	if(!send_byte(0x80 + offset[y][0] + offset[y][1], false))
		return(false);

	for(x = 0; x < display_common_buffer_columns; x++)
		if(!send_byte(display_common_buffer[y][x], true))
			return(false);

	display_common_row_status.row[y].dirty = 0;

	return(true);
}
