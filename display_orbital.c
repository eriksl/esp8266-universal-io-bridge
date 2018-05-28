#include "display.h"
#include "display_orbital.h"
#include "i2c.h"

static bool_t inited = false;

irom bool_t display_orbital_init(void)
{
	unsigned int ix, byte, x, y;

	for(ix = 4; ix > 0; ix--)
	{
		if(i2c_send1(0x28, 0x21) == i2c_error_ok)
			break;
		msleep(25);
	}

	if(ix == 0)
		return(false);

	for(ix = 0; ix < display_common_udg_size; ix++)
	{
		if(i2c_send3(0x28, 0xfe, 0x4e, ix) != i2c_error_ok)
			return(false);

		for(byte = 0; byte < display_common_udg_byte_size; byte++)
			if(i2c_send1(0x28, display_common_udg[ix].pattern[byte]) != i2c_error_ok)
				return(false);
	}

	inited = true;

	for(y = 0; y < display_common_buffer_rows; y++)
		for(x = 0; x < display_common_buffer_columns; x++)
			display_common_buffer[y][x] = ' ';

	for(ix = 0; ix < display_common_buffer_rows; ix++)
		display_common_row_status.row[ix].dirty = 1;

	return(display_orbital_bright(1));
}

irom bool_t display_orbital_bright(int brightness)
{
	switch(brightness)
	{
		case(0):
		{
			if(i2c_send2(0x28, 0xfe, 0x46) != i2c_error_ok)
				return(false);

			if(i2c_send3(0x28, 0xfe, 0x59, 0x03) != i2c_error_ok)
				return(false);

			break;
		}

		case(1):
		{
			if(i2c_send3(0x28, 0xfe, 0x42, 0x00) != i2c_error_ok)
				return(false);

			if(i2c_send3(0x28, 0xfe, 0x59, 0x03) != i2c_error_ok)
				return(false);

			break;
		}

		case(2):
		{
			if(i2c_send3(0x28, 0xfe, 0x42, 0x00) != i2c_error_ok)
				return(false);

			if(i2c_send3(0x28, 0xfe, 0x59, 0x02) != i2c_error_ok)
				return(false);

			break;
		}

		case(3):
		{
			if(i2c_send3(0x28, 0xfe, 0x42, 0x00) != i2c_error_ok)
				return(false);

			if(i2c_send3(0x28, 0xfe, 0x59, 0x01) != i2c_error_ok)
				return(false);

			break;
		}

		case(4):
		{
			if(i2c_send3(0x28, 0xfe, 0x42, 0x00) != i2c_error_ok)
				return(false);

			if(i2c_send3(0x28, 0xfe, 0x59, 0x00) != i2c_error_ok)
				return(false);

			break;
		}

		default:
		{
			return(false);
		}
	}

	return(true);
}

irom bool_t display_orbital_set(const char *tag, const char *text)
{
	if(!inited)
		return(false);

	return(display_common_set(tag, text,
				display_common_map_size, display_common_map,
				display_common_udg_size, display_common_udg));
}

irom bool_t display_orbital_show(void)
{
	int x, y;

	if(!inited)
		return(false);

	for(y = 0; y < display_common_buffer_rows; y++)
		if(display_common_row_status.row[y].dirty)
			break;

	if(y >= display_common_buffer_rows)
		return(false);

	if(i2c_send2(0x28, 0xfe, 0x44) != i2c_error_ok) // line wrap off
		return(false);

	if(i2c_send2(0x28, 0xfe, 0x52) != i2c_error_ok) // scroll off
		return(false);

	if(i2c_send2(0x28, 0xfe, 0x54) != i2c_error_ok) // cursor off
		return(false);

	if(i2c_send4(0x28, 0xfe, 0x47, 1, y + 1) != i2c_error_ok)
		return(false);

	for(x = 0; x < display_common_buffer_columns; x++)
		if(i2c_send1(0x28, display_common_buffer[y][x]) != i2c_error_ok)
			return(false);

	display_common_row_status.row[y].dirty = 0;

	return(true);
}
