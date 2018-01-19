#include "display.h"
#include "display_lcd.h"
#include "io.h"
#include "config.h"

typedef struct
{
	int	io;
	int pin;
} lcd_io_t;

static bool_t inited = false;
static bool_t nibble_mode;
static lcd_io_t lcd_io_pin[io_lcd_size];

iram static bool set_pin(io_lcd_mode_t pin_use, int value)
{
	int io, pin;

	io = lcd_io_pin[pin_use].io;
	pin = lcd_io_pin[pin_use].pin;

	if((io < 0) || (pin < 0))
		return(false);

	if(io_write_pin((string_t *)0, io, pin, value) != io_ok)
		return(false);

	return(true);
}

iram static bool send_byte_raw(int byte, bool data)
{
	if(!set_pin(io_lcd_rs, data))
		return(false);

	set_pin(io_lcd_rw, 0);
	set_pin(io_lcd_d0, !!(byte & (1 << 0)));
	set_pin(io_lcd_d1, !!(byte & (1 << 1)));
	set_pin(io_lcd_d2, !!(byte & (1 << 2)));
	set_pin(io_lcd_d3, !!(byte & (1 << 3)));

	if(!set_pin(io_lcd_d4, !!(byte & (1 << 4))))
		return(false);

	if(!set_pin(io_lcd_d5, !!(byte & (1 << 5))))
		return(false);

	if(!set_pin(io_lcd_d6, !!(byte & (1 << 6))))
		return(false);

	if(!set_pin(io_lcd_d7, !!(byte & (1 << 7))))
		return(false);

	if(!set_pin(io_lcd_e, false))
		return(false);

	if(!set_pin(io_lcd_e, true))
		return(false);

	return(true);
}

iram static bool send_byte(int byte, bool data)
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

	for(pin = 0; pin < io_lcd_size; pin++)
	{
		lcd_io_pin[pin].io = -1;
		lcd_io_pin[pin].pin = -1;
	}

	for(io = 0; io < io_id_size; io++)
	{
		for(pin = 0; pin < max_pins_per_io; pin++)
		{
			pin_config = &io_config[io][pin];

			if(pin_config->mode == io_pin_lcd)
			{
				lcd_io_pin[pin_config->shared.lcd.pin_use].io = io;
				lcd_io_pin[pin_config->shared.lcd.pin_use].pin = pin;
			}
		}
	}

	if((lcd_io_pin[io_lcd_rs].io < 0) || (lcd_io_pin[io_lcd_rs].pin < 0))
		return(false);

	if((lcd_io_pin[io_lcd_e].io < 0) || (lcd_io_pin[io_lcd_e].pin < 0))
		return(false);

	if((lcd_io_pin[io_lcd_d4].io < 0) || (lcd_io_pin[io_lcd_d4].pin < 0))
		return(false);

	if((lcd_io_pin[io_lcd_d5].io < 0) || (lcd_io_pin[io_lcd_d5].pin < 0))
		return(false);

	if((lcd_io_pin[io_lcd_d6].io < 0) || (lcd_io_pin[io_lcd_d6].pin < 0))
		return(false);

	if((lcd_io_pin[io_lcd_d7].io < 0) || (lcd_io_pin[io_lcd_d7].pin < 0))
		return(false);

	nibble_mode = false;

	if((lcd_io_pin[io_lcd_d0].io < 0) || (lcd_io_pin[io_lcd_d0].pin < 0))
		nibble_mode = true;

	if((lcd_io_pin[io_lcd_d1].io < 0) || (lcd_io_pin[io_lcd_d1].pin < 0))
		nibble_mode = true;

	if((lcd_io_pin[io_lcd_d2].io < 0) || (lcd_io_pin[io_lcd_d2].pin < 0))
		nibble_mode = true;

	if((lcd_io_pin[io_lcd_d3].io < 0) || (lcd_io_pin[io_lcd_d3].pin < 0))
		nibble_mode = true;

	// robust initialisation sequence,
	// from http://web.alfredstate.edu/weimandn/lcd/lcd_initialization/lcd_initialization_index.html

	msleep(50);

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

	set_pin(io_lcd_bl, pwm); // backlight pin might be not configured, ignore error

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

iram bool_t display_lcd_show(void)
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
