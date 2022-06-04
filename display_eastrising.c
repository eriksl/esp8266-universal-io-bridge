#include "display.h"
#include "display_eastrising.h"
#include "i2c.h"
#include "spi.h"
#include "config.h"
#include "sys_time.h"
#include "dispatch.h"
#include "util.h"
#include "font.h"

#include <stdint.h>
#include <stdbool.h>

enum
{
	pad_1 = 0,
	pad_2 = 4,
	border_1 = 5,
	border_2 = 4,
};

typedef enum
{
	display_mode_disabled = 0,
	display_mode_i2c,
	display_mode_spi_mosi_miso,
	display_mode_spi_mosi,
	display_mode_size,
} display_mode_t;

typedef struct attr_packed
{
	unsigned int column:8;
	unsigned int row:8;
} text_t;

assert_size(text_t, 2);

typedef struct attr_packed
{
	display_mode_t	mode:3;
	unsigned int	logmode:1;
	unsigned int	graphic_mode:1;
	unsigned int	brightness:3;
	unsigned int	low_brightness:1;
	unsigned int	x_size:10;
	unsigned int	y_size:10;
} display_t;

assert_size(display_t, 4);

typedef struct attr_packed
{
	struct attr_packed
	{
		int8_t io;
		int8_t pin;
	} user_cs;
} pin_t;

assert_size(pin_t, 2);

enum
{
	i2c_addr_data =		0x06,
	i2c_addr_command =	0x07,
};

enum
{
	spi_rs_data =	0UL << 7,
	spi_rs_cmd =	1UL << 7,
	spi_rw_write =	0UL << 6,
	spi_rw_read =	1UL << 6,
};

enum
{
	reg_pwrr =		0x01,
	reg_mrwc =		0x02,
	reg_pcsr =		0x04,
	reg_sysr =		0x10,
	reg_hdwr =		0x14,
	reg_hndftr =	0x15,
	reg_hndr =		0x16,
	reg_hstr =		0x17,
	reg_hpwr =		0x18,
	reg_vdhr0 =		0x19,
	reg_vdhr1 =		0x1a,
	reg_vndr0 =		0x1b,
	reg_vndr1 =		0x1c,
	reg_vstr0 =		0x1d,
	reg_vstr1 =		0x1e,
	reg_vpwr =		0x1f,
	reg_dpcr =		0x20,
	reg_hsaw0 =		0x30,
	reg_hsaw1 =		0x31,
	reg_vsaw0 =		0x32,
	reg_vsaw1 =		0x33,
	reg_heaw0 =		0x34,
	reg_heaw1 =		0x35,
	reg_veaw0 =		0x36,
	reg_veaw1 =		0x37,
	reg_mwcr0 =		0x40,
	reg_mwcr1 =		0x41,
	reg_curh0 =		0x46,
	reg_curh1 =		0x47,
	reg_curv0 =		0x48,
	reg_curv1 =		0x49,
	reg_becr0 =		0x50,
	reg_becr1 =		0x51,
	reg_hsbe0 =		0x54,
	reg_hsbe1 =		0x55,
	reg_vsbe0 =		0x56,
	reg_vsbe1 =		0x57,
	reg_hdbe0 =		0x58,
	reg_hdbe1 =		0x59,
	reg_vdbe0 =		0x5a,
	reg_vdbe1 =		0x5b,
	reg_bewr0 =		0x5c,
	reg_bewr1 =		0x5d,
	reg_behr0 =		0x5e,
	reg_behr1 =		0x5f,
	reg_ltpr0 =		0x52,
	reg_ltpr1 =		0x53,
	reg_bgcr0 =		0x60,
	reg_bgcr1 =		0x61,
	reg_bgcr2 =		0x62,
	reg_fgcr0 =		0x63,
	reg_fgcr1 =		0x64,
	reg_fgcr2 =		0x65,
	reg_pll_c1 =	0x88,
	reg_pll_c2 =	0x89,
	reg_p1cr =		0x8a,
	reg_p1dcr =		0x8b,
	reg_mclr =		0x8e,

	reg_pllc1_plldivm_div_1 =				0b000000,
	reg_pllc1_plldivm_div_2 =				0b100000,
	reg_pllc1_plldivn_bitpos =				0x00,
	reg_pllc1_plldivn_mask =				0x1f,
	reg_pllc1_value =						reg_pllc1_plldivm_div_1 | ((11 & reg_pllc1_plldivn_mask) << reg_pllc1_plldivn_bitpos),

	reg_pllc2_plldivk_div_bitpos =			0x00,
	reg_pllc2_plldivk_div_mask =			0x07,
	reg_pllc2_value =						(2 & reg_pllc2_plldivk_div_mask) << reg_pllc2_plldivk_div_bitpos,

	reg_sysr_color_depth_8 =				0b00000000,
	reg_sysr_color_depth_16 =				0b00001000,
	reg_sysr_if_8bit =						0b00000000,
	reg_sysr_if_16bit =						0b00000010,

	reg_pcsr_sample_rising_edge =			0b00000000,
	reg_pcsr_sample_falling_edge =			0b10000000,
	reg_pcsr_clock_period_system =			0b00000000,
	reg_pcsr_clock_period_system_by_2 =		0b00000001,
	reg_pcsr_clock_period_system_by_4 =		0b00000010,
	reg_pcsr_clock_period_system_by_8 =		0b00000011,

	reg_hndftr_de_polarity_active_high =	0b00000000,
	reg_hndftr_de_polarity_active_low =		0b10000000,

	reg_hpwr_hsync_polarity_active_low =	0b00000000,
	reg_hpwr_hsync_polarity_active_high =	0b10000000,

	reg_vpwr_vsync_polarity_active_low =	0b00000000,
	reg_vpwr_vsync_polarity_active_high =	0b10000000,

	reg_p1cr_pwm1_enable =					0b10000000,
	reg_p1cr_disable_level_low =			0b00000000,
	reg_p1cr_disable_level_high =			0b01000000,
	reg_p1cr_function_pwm1 =				0b00000000,
	reg_p1cr_function_fixed =				0b00010000,
	reg_p1cr_clock_ratio_1 =				0b00000000,
	reg_p1cr_clock_ratio_2 =				0b00000001,
	reg_p1cr_clock_ratio_4 =				0b00000010,
	reg_p1cr_clock_ratio_8 =				0b00000011,
	reg_p1cr_clock_ratio_16 =				0b00000100,
	reg_p1cr_clock_ratio_32 =				0b00000101,
	reg_p1cr_clock_ratio_64 =				0b00000110,
	reg_p1cr_clock_ratio_128 =				0b00000111,
	reg_p1cr_clock_ratio_256 =				0b00001000,
	reg_p1cr_clock_ratio_512 =				0b00001001,
	reg_p1cr_clock_ratio_1024 =				0b00001010,
	reg_p1cr_clock_ratio_2048 =				0b00001011,
	reg_p1cr_clock_ratio_4096 =				0b00001100,
	reg_p1cr_clock_ratio_8192 =				0b00001101,
	reg_p1cr_clock_ratio_16384 =			0b00001110,
	reg_p1cr_clock_ratio_32768 =			0b00001111,

	reg_pwrr_display_enable =				0b10000000,
	reg_pwrr_display_disable =				0b00000000,
	reg_pwrr_display_sleep_mode_enable =	0b00000010,
	reg_pwrr_display_sleep_mode_disable =	0b00000000,
	reg_pwrr_display_reset_start =			0b00000001,
	reg_pwrr_display_reset_complete =		0b00000000,

	reg_mwcr1_graphic_cursor_disable =		0b00000000,
	reg_mwcr1_graphic_cursor_enable =		0b10000000,
	reg_mwcr1_graphic_cursor_select_0 =		0b00000000,
	reg_mwcr1_graphic_cursor_select_1 =		0b00010000,
	reg_mwcr1_graphic_cursor_select_2 =		0b00100000,
	reg_mwcr1_graphic_cursor_select_3 =		0b00110000,
	reg_mwcr1_graphic_cursor_select_4 =		0b01000000,
	reg_mwcr1_graphic_cursor_select_5 =		0b01010000,
	reg_mwcr1_graphic_cursor_select_6 =		0b01100000,
	reg_mwcr1_graphic_cursor_select_7 =		0b01110000,
	reg_mwcr1_write_destination_layer =		0b00000000,
	reg_mwcr1_write_destination_cgram =		0b00000100,
	reg_mwcr1_write_destination_cursor =	0b00001000,
	reg_mwcr1_write_destination_pattern =	0b00001100,
	reg_mwcr1_write_destination_layer_0 =	0b00000000,
	reg_mwcr1_write_destination_layer_1 =	0b00000001,

	reg_mclr_memory_clear_start =			0b10000000,
	reg_mclr_memory_clear_complete =		0b00000000,
	reg_mclr_memory_area_active_window =	0b01000000,
	reg_mclr_memory_area_full_window =		0b00000000,

	reg_mwcr0_mode_text =					0b10000000,
	reg_mwcr0_mode_graphic =				0b00000000,
	reg_mwcr0_cursor_visible =				0b01000000,
	reg_mwcr0_cursor_invisible =			0b00000000,
	reg_mwcr0_cursor_blink =				0b00100000,
	reg_mwcr0_cursor_steady =				0b00000000,
	reg_mwcr0_memory_write_direction_lrtd =	0b00000000,
	reg_mwcr0_memory_write_direction_rltd =	0b00000100,
	reg_mwcr0_memory_write_direction_tdlr =	0b00001000,
	reg_mwcr0_memory_write_direction_dtlr =	0b00001100,
	reg_mwcr0_memory_write_autoincr_en =	0b00000000,
	reg_mwcr0_memory_write_autoincr_dis =	0b00000010,
	reg_mwcr0_memory_read_autoincr_en =		0b00000000,
	reg_mwcr0_memory_read_autoincr_dis =	0b00000001,

	reg_ltpr0_scroll_both =					0b00000000,
	reg_ltpr0_scroll_layer_1 =				0b01000000,
	reg_ltpr0_scroll_layer_2 =				0b10000000,
	reg_ltpr0_scroll_buffer =				0b11000000,
	reg_ltpr0_floatwin_transparency_dis =	0b00000000,
	reg_ltpr0_floatwin_transparency_en =	0b00100000,
	reg_ltpr0_visible_layer_1 =				0b00000000,
	reg_ltpr0_visible_layer_2 =				0b00000001,
	reg_ltpr0_visible_layer_lighten =		0b00000010,
	reg_ltpr0_visible_layer_transparent =	0b00000011,
	reg_ltpr0_visible_layer_or =			0b00000100,
	reg_ltpr0_visible_layer_and =			0b00000101,
	reg_ltpr0_visible_layer_floatwin =		0b00000110,
	reg_ltpr0_visible_layer_unused =		0b00000111,

	reg_ltpr1_transparency_layer_2_8_8 =	0b00000000,
	reg_ltpr1_transparency_layer_2_7_8 =	0b00010000,
	reg_ltpr1_transparency_layer_2_6_8 =	0b00100000,
	reg_ltpr1_transparency_layer_2_5_8 =	0b00110000,
	reg_ltpr1_transparency_layer_2_4_8 =	0b01000000,
	reg_ltpr1_transparency_layer_2_3_8 =	0b01010000,
	reg_ltpr1_transparency_layer_2_2_8 =	0b01100000,
	reg_ltpr1_transparency_layer_2_1_8 =	0b01110000,
	reg_ltpr1_transparency_layer_2_0_8 =	0b10000000,
	reg_ltpr1_transparency_layer_1_8_8 =	0b00000000,
	reg_ltpr1_transparency_layer_1_7_8 =	0b00000001,
	reg_ltpr1_transparency_layer_1_6_8 =	0b00000010,
	reg_ltpr1_transparency_layer_1_5_8 =	0b00000011,
	reg_ltpr1_transparency_layer_1_4_8 =	0b00000100,
	reg_ltpr1_transparency_layer_1_3_8 =	0b00000101,
	reg_ltpr1_transparency_layer_1_2_8 =	0b00000110,
	reg_ltpr1_transparency_layer_1_1_8 =	0b00000111,
	reg_ltpr1_transparency_layer_1_0_8 =	0b00001000,

	reg_vsbe1_source_layer_0 =				0b00000000,
	reg_vsbe1_source_layer_1 =				0b10000000,

	reg_vdbe1_destination_layer_0 =			0b00000000,
	reg_vdbe1_destination_layer_1 =			0b10000000,

	reg_dpcr_one_layer =					0b00000000,
	reg_dpcr_two_layer =					0b10000000,
	reg_dpcr_hor_scan_ltor =				0b00000000,
	reg_dpcr_hor_scan_rtol =				0b00001000,
	reg_dpcr_vert_scan_ltor =				0b00000000,
	reg_dpcr_vert_scan_rtol =				0b00000100,

	reg_becr0_idle =						0b00000000,
	reg_becr0_busy =						0b10000000,
	reg_becr0_src_block =					0b00000000,
	reg_becr0_src_lineair =					0b01000000,
	reg_becr0_dst_block =					0b00000000,
	reg_becr0_dst_lineair =					0b00100000,

	reg_becr1_rop_code_write =				0b00000000,
	reg_becr1_rop_code_read =				0b00000001,
	reg_becr1_rop_code_move_pos =			0b00000010,
	reg_becr1_rop_code_move_neg =			0b00000011,
	reg_becr1_rop_code_write_transp =		0b00000100,
	reg_becr1_rop_code_move_transp =		0b00000101,
	reg_becr1_rop_code_fill_pattern =		0b00000110,
	reg_becr1_rop_code_fill_pattern_transp=	0b00000111,
	reg_becr1_rop_code_expand_colour =		0b00001000,
	reg_becr1_rop_code_expand_colour_transp=0b00001001,
	reg_becr1_rop_code_move_expand_colour=	0b00001010,
	reg_becr1_rop_code_move_expand_transp=	0b00001011,
	reg_becr1_rop_code_fill =				0b00001100,

	reg_becr1_rop_expand_col_bit_start_0 =	0b01110000,

	reg_becr1_rop_func_black =				0b00000000,
	reg_becr1_rop_func_ns_and_nd =			0b00010000,
	reg_becr1_rop_func_ns_and_d =			0b00100000,
	reg_becr1_rop_func_ns =					0b00110000,
	reg_becr1_rop_func_s_and_nd =			0b01000000,
	reg_becr1_rop_func_nd =					0b01010000,
	reg_becr1_rop_func_s_xor_d =			0b01100000,
	reg_becr1_rop_func_ns_or_nd =			0b01110000,
	reg_becr1_rop_func_s_and_d =			0b10000000,
	reg_becr1_rop_func_n_s_xor_d =			0b10010000,
	reg_becr1_rop_func_d =					0b10100000,
	reg_becr1_rop_func_ns_or_d =			0b10110000,
	reg_becr1_rop_func_s =					0b11000000,
	reg_becr1_rop_func_s_or_nd =			0b11010000,
	reg_becr1_rop_func_s_or_d =				0b11100000,

	horizontal_blanking = 38,
	horizontal_blanking_fine = 4,
	horizontal_sync_start = 16,
	horizontal_sync_length = 32,
	vertical_blanking = 14,
	vertical_sync_start = 6,
	vertical_sync_length = 2,

	display_slot_lines = 4,
};

static text_t text;
static display_t display;
static pin_t pin;

attr_result_used static bool if_can_read(void)
{
	switch(display.mode)
	{
		case(display_mode_i2c): return(true);
		case(display_mode_spi_mosi_miso): return(true);
		case(display_mode_spi_mosi): return(false);
		default:
		{
			log("eastrising: if_can_read: invalid mode\n");
			return(false);
		}
	}
}

static void set_i2c_speed(int speed)
{
	unsigned int config_speed;

	if(speed < 0)
	{
		if(config_get_uint("i2c.speed_delay", &config_speed, -1, -1))
			speed = config_speed;
		else
			speed = 1000;
	}

	i2c_speed_delay(speed);
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
}

static bool attr_result_used write_command(uint8_t cmd)
{
	switch(display.mode)
	{
		case(display_mode_disabled): return(false);
		case(display_mode_i2c):
		{
			return(i2c_send1(i2c_addr_command, cmd) == i2c_error_ok);
		}

		case(display_mode_spi_mosi_miso):
		case(display_mode_spi_mosi):
		{
			string_new(, error, 64);

			if(!spi_start(&error))
			{
				log("eastrising command spi start: %s\n", string_to_cstr(&error));
				return(false);
			}

			if(!spi_write(8, spi_rs_cmd | spi_rw_write))
			{
				log("eastrising command spi write failed\n");
				return(false);
			}

			if(!spi_write(8, cmd))
			{
				log("eastrising command spi write failed\n");
				return(false);
			}

			if(!spi_transmit(&error, spi_clock_10M, 0, 0, 0, 0, 0, 0))
			{
				log("eastrising command spi transmit: %s\n", string_to_cstr(&error));
				return(false);
			}

			if(!spi_finish(&error))
			{
				log("eastrising command spi finish failed\n");
				return(false);
			}

			break;
		}

		default:
		{
			log("eastrising command: invalid mode\n");
			return(false);
		}
	}

	return(true);
}

static bool attr_result_used write_data(unsigned int length, const uint8_t *data)
{
	switch(display.mode)
	{
		case(display_mode_disabled): return(false);
		case(display_mode_i2c):
		{
			return(i2c_send(i2c_addr_data, length, data) == i2c_error_ok);
		}

		case(display_mode_spi_mosi_miso):
		case(display_mode_spi_mosi):
		{
			string_new(, error, 64);
			unsigned int current;

			if(!spi_start(&error))
			{
				log("eastrising data spi start: %s\n", string_to_cstr(&error));
				return(false);
			}

			for(current = 0; current < length; current++)
			{
				if(spi_write(8, data[current]))
					continue;

				if(!spi_transmit(&error, spi_clock_10M, 8, spi_rs_data | spi_rw_write, 0, 0, 0, 0))
				{
					log("eastrising data spi transmit: %s\n", string_to_cstr(&error));
					return(false);
				}

				if(!spi_finish(&error))
				{
					log("eastrising data spi finish failed\n");
					return(false);
				}

				if(!spi_start(&error))
				{
					log("eastrising data spi start: %s\n", string_to_cstr(&error));
					return(false);
				}

				if(!spi_write(8, data[current]))
				{
					log("eastrising data spi write failed\n");
					return(false);
				}
			}

			if(!spi_transmit(&error, spi_clock_10M, 8, spi_rs_data | spi_rw_write, 0, 0, 0, 0))
			{
				log("eastrising data spi transmit: %s\n", string_to_cstr(&error));
				return(false);
			}

			if(!spi_finish(&error))
			{
				log("eastrising data spi finish failed\n");
				return(false);
			}

			break;
		}

		default:
		{
			log("eastrising data: invalid mode\n");
			return(false);
		}
	}

	return(true);
}

static bool attr_result_used write_data_1(uint8_t data)
{
	return(write_data(1, &data));
}

static bool attr_result_used write_command_data_1(uint8_t cmd, uint8_t data)
{
	if(!write_command(cmd))
		return(false);

	return(write_data_1(data));
}

static bool attr_result_used read_data(uint8_t cmd, uint8_t *data)
{
	if(!write_command(cmd))
		return(false);

	switch(display.mode)
	{
		case(display_mode_disabled): return(false);
		case(display_mode_i2c):
		{
			return(i2c_receive(i2c_addr_data, 1, data) == i2c_error_ok);
		}

		case(display_mode_spi_mosi_miso):
		{
			string_new(, error, 64);

			if(!spi_start(&error))
			{
				log("eastrising data read spi start: %s\n", string_to_cstr(&error));
				return(false);
			}

			if(!spi_write(8, spi_rs_data | spi_rw_read))
			{
				log("eastrising data read spi write failed\n");
				return(false);
			}

			if(!spi_transmit(&error, spi_clock_10M, 0, 0, 0, 0, 0, 1))
			{
				log("eastrising data read spi transmit: %s\n", string_to_cstr(&error));
				return(false);
			}

			if(!spi_receive(&error, 1, data))
			{
				log("eastrising data read spi receive: %s\n", string_to_cstr(&error));
				return(false);
			}

			if(!spi_finish(&error))
			{
				log("eastrising data read spi finish failed\n");
				return(false);
			}

			break;
		}

		default:
		{
			log("eastrising read data: invalid mode\n");
			return(false);
		}
	}

	return(true);
}

static bool attr_result_used fgcolour_set(unsigned int r, unsigned int g, unsigned int b)
{
	r = (r >> 3) & 0xff;
	g = (g >> 2) & 0xff;
	b = (b >> 3) & 0xff;

	if(!write_command_data_1(reg_fgcr0, r))
		return(false);

	if(!write_command_data_1(reg_fgcr1, g))
		return(false);

	if(!write_command_data_1(reg_fgcr2, b))
		return(false);

	return(true);
}

static bool attr_result_used bgcolour_set(unsigned int r, unsigned int g, unsigned int b)
{
	r = (r >> 3) & 0xff;
	g = (g >> 2) & 0xff;
	b = (b >> 3) & 0xff;

	if(!write_command_data_1(reg_bgcr0, r))
		return(false);

	if(!write_command_data_1(reg_bgcr1, g))
		return(false);

	if(!write_command_data_1(reg_bgcr2, b))
		return(false);

	return(true);
}

static bool attr_result_used set_active_layer(unsigned int layer)
{
	return(write_command_data_1(reg_mwcr1, reg_mwcr1_graphic_cursor_disable | reg_mwcr1_write_destination_layer | (layer & 0x01)));
}

static bool attr_result_used show_layer(unsigned int layer)
{
	unsigned int value = reg_ltpr0_scroll_layer_1 | reg_ltpr0_floatwin_transparency_dis;

	if(!write_command_data_1(reg_ltpr1, reg_ltpr1_transparency_layer_2_8_8 | reg_ltpr1_transparency_layer_1_8_8))
		return(false);

	value |= (layer == 0) ? reg_ltpr0_visible_layer_1 : reg_ltpr0_visible_layer_2;

	return(write_command_data_1(reg_ltpr0, value));
}

static bool attr_result_used set_active_window(unsigned int x0, unsigned int y0, unsigned int x1, unsigned int y1)
{
	x0 = umin(x0, display.x_size - 1);
	x1 = umin(x1, display.x_size - 1);
	y0 = umin(y0, display.y_size - 1);
	y1 = umin(y1, display.y_size - 1);

	if(!write_command_data_1(reg_hsaw0, (x0 >> 0) & 0xff))
		return(false);

	if(!write_command_data_1(reg_hsaw1, (x0 >> 8) & 0x03))
		return(false);

	if(!write_command_data_1(reg_vsaw0, (y0 >> 0) & 0xff))
		return(false);

	if(!write_command_data_1(reg_vsaw1, (y0 >> 8) & 0x01))
		return(false);

	if(!write_command_data_1(reg_heaw0, (x1 >> 0) & 0xff))
		return(false);

	if(!write_command_data_1(reg_heaw1, (x1 >> 8) & 0x03))
		return(false);

	if(!write_command_data_1(reg_veaw0, (y1 >> 0) & 0xff))
		return(false);

	if(!write_command_data_1(reg_veaw1, (y1 >> 8) & 0x01))
		return(false);

	return(true);
}

static bool attr_result_used box(unsigned int r, unsigned int g, unsigned int b, unsigned int from_x, unsigned int from_y, unsigned int to_x, unsigned int to_y)
{
	bool success = false;
	unsigned int timeout;
	uint8_t data;

	if(!set_active_window(from_x, from_y, to_x, to_y))
		goto error;

	if(!bgcolour_set(r, g, b))
		goto error;

	if(!write_command_data_1(reg_mclr, reg_mclr_memory_clear_start | reg_mclr_memory_area_active_window))
		goto error;

	if(if_can_read())
	{
		for(timeout = 10; timeout > 0; timeout--)
		{
			if(!read_data(reg_mclr, &data))
				goto error;

			if(!(data & reg_mclr_memory_clear_start))
				break;

			os_delay_us(500);
		}

		if(timeout == 0)
			log("BTE clear area timeout\n");
		else
			success = true;
	}
	else
	{
		msleep(3);
		success = true;
	}

error:
	if(!write_command_data_1(reg_becr0, 0))
		success = false;

	if(!set_active_window(0, 0, display.x_size - 1, display.y_size - 1))
		success = false;

	return(success);
}

static bool clear_screen(void)
{
	return(box(0x00, 0x00, 0x00, 0, 0, display.x_size - 1, display.y_size - 1));
}

static bool attr_result_used scroll(unsigned int x0, unsigned int y0, unsigned int x1, unsigned int y1, unsigned int width, unsigned height)
{
	uint8_t data;
	unsigned int timeout;
	bool success = false;

	if(!write_command_data_1(reg_hsbe0, (x0 >> 0) & 0xff))
		goto error;

	if(!write_command_data_1(reg_hsbe1, (x0 >> 8) & 0x03))
		goto error;

	if(!write_command_data_1(reg_vsbe0, (y0 >> 0) & 0xff))
		goto error;

	if(!write_command_data_1(reg_vsbe1, ((y0 >> 8) & 0x01) | reg_vsbe1_source_layer_0))
		goto error;

	if(!write_command_data_1(reg_hdbe0, (x1 >> 0) & 0xff))
		goto error;

	if(!write_command_data_1(reg_hdbe1, (x1 >> 8) & 0x03))
		goto error;

	if(!write_command_data_1(reg_vdbe0, (y1 >> 0) & 0xff))
		goto error;

	if(!write_command_data_1(reg_vdbe1, ((y1 >> 8) & 0x01) | reg_vdbe1_destination_layer_0))
		goto error;

	if(!write_command_data_1(reg_bewr0, (width >> 0) & 0xff))
		goto error;

	if(!write_command_data_1(reg_bewr1, (width >> 8) & 0x03))
		goto error;

	if(!write_command_data_1(reg_behr0, (height >> 0) & 0xff))
		goto error;

	if(!write_command_data_1(reg_behr1, (height >> 8) & 0x03))
		goto error;

	// config BTE

	if(!write_command_data_1(reg_becr1, reg_becr1_rop_code_move_pos | reg_becr1_rop_func_s))
		goto error;

	// start BTE

	if(!write_command_data_1(reg_becr0, reg_becr0_busy | reg_becr0_src_block | reg_becr0_dst_block))
		goto error;

	if(if_can_read())
	{
		for(timeout = 0; timeout < 10; timeout++)
		{
			if(!read_data(reg_becr0, &data))
				goto error;

			if(!(data & reg_becr0_busy))
				break;

			msleep(10);
		}

		if(timeout >= 10)
			log("scroll: BTE timeout\n");
		else
			success = true;
	}
	else
	{
		msleep(15);
		success = true;
	}

error:
	if(!write_command_data_1(reg_becr0, 0))
		success = false;

	return(success);
}

static bool attr_result_used copy_layer(unsigned int source_layer, unsigned int destination_layer)
{
	uint8_t data;
	unsigned int timeout;
	bool success = false;

	if(!write_command_data_1(reg_hsbe0, 0))
		goto error;

	if(!write_command_data_1(reg_hsbe1, 0))
		goto error;

	if(!write_command_data_1(reg_vsbe0, 0))
		goto error;

	if(!write_command_data_1(reg_vsbe1, 0 | (source_layer ? reg_vsbe1_source_layer_1 : reg_vsbe1_source_layer_0)))
		goto error;

	if(!write_command_data_1(reg_hdbe0, 0))
		goto error;

	if(!write_command_data_1(reg_hdbe1, 0))
		goto error;

	if(!write_command_data_1(reg_vdbe0, 0))
		goto error;

	if(!write_command_data_1(reg_vdbe1, 0 | (destination_layer ? reg_vdbe1_destination_layer_1 : reg_vdbe1_destination_layer_0)))
		goto error;

	if(!write_command_data_1(reg_bewr0, (display.x_size >> 0) & 0xff))
		goto error;

	if(!write_command_data_1(reg_bewr1, (display.x_size >> 8) & 0x03))
		goto error;

	if(!write_command_data_1(reg_behr0, (display.y_size >> 0) & 0xff))
		goto error;

	if(!write_command_data_1(reg_behr1, (display.y_size >> 8) & 0x03))
		goto error;

	// config BTE

	if(!write_command_data_1(reg_becr1, reg_becr1_rop_code_move_pos | reg_becr1_rop_func_s))
		goto error;

	// start BTE

	if(!write_command_data_1(reg_becr0, reg_becr0_busy | reg_becr0_src_block | reg_becr0_dst_block))
		goto error;

	if(if_can_read())
	{
		for(timeout = 0; timeout < 10; timeout++)
		{
			if(!read_data(reg_becr0, &data))
				goto error;

			if(!(data & reg_becr0_busy))
				break;

			msleep(10);
		}

		if(timeout >= 10)
			log("copy_layer: BTE timeout\n");
		else
			success = true;
	}
	else
	{
		msleep(15);
		success = true;
	}

error:
	if(!write_command_data_1(reg_becr0, 0))
		success = false;

	return(success);
}

static bool attr_result_used blit(unsigned int x, unsigned int y, unsigned int width, unsigned int height,
		unsigned int length, uint8_t *data)
{
	bool success = false;
	uint8_t rdata;

	if((x + width) >= display.x_size)
		return(true);

	if((y + height) >= display.y_size)
		return(true);

	if(!write_command_data_1(reg_hdbe0, (x >> 0) & 0xff))
		goto error;

	if(!write_command_data_1(reg_hdbe1, (x >> 8) & 0x03))
		goto error;

	if(!write_command_data_1(reg_vdbe0, (y >> 0) & 0xff))
		goto error;

	if(!write_command_data_1(reg_vdbe1, ((y >> 8) & 0x01) | reg_vdbe1_destination_layer_0))
		goto error;

	if(!write_command_data_1(reg_bewr0, (width >> 0) & 0xff))
		goto error;

	if(!write_command_data_1(reg_bewr1, (width >> 8) & 0x03))
		goto error;

	if(!write_command_data_1(reg_behr0, (height >> 0) & 0xff))
		goto error;

	if(!write_command_data_1(reg_behr1, (height >> 8) & 0x03))
		goto error;

	// config BTE

	if(!write_command_data_1(reg_becr1, reg_becr1_rop_code_expand_colour_transp | reg_becr1_rop_expand_col_bit_start_0))
		goto error;

	// start BTE

	if(!write_command_data_1(reg_becr0, reg_becr0_busy | reg_becr0_src_block | reg_becr0_dst_block))
		goto error;

	if(!write_command(reg_mrwc))
		goto error;

	if(!write_data(length, data))
		goto error;

	success = true;

error:
	if(!write_command_data_1(reg_becr0, 0))
	{
		log("display eastrising: can't stop BTE\n");
		success = false;
	}

	if(if_can_read())
	{
		if(!read_data(reg_becr0, &rdata))
		{
			log("display eastrising: can't read BTE status\n");
			success = false;
		}

		if(rdata & reg_becr0_busy)
		{
			log("display eastrising: BTE still busy\n");
			success = false;
		}
	}

	return(success);
}

static attr_result_used bool text_send(unsigned int code)
{
	font_info_t font_info;
	font_cell_t font_cell;
	unsigned int x, y, max_x, max_y;
	unsigned int x2, y2, byte, bit, colour;
	uint8_t data[(32 / 8) * 32];

	if(code == ' ')
		goto skip;

	if(!font_get_info(&font_info))
		return(false);

	x = text.column * font_info.width;
	y = text.row * font_info.height;

	max_x = 0;
	max_y = 0;

	colour = 0x00;

	if(display.logmode)
		colour = 0xff;
	else
	{
		if(text.row == 0)
		{
			colour = 0xff;

			x += border_1 + pad_1;
			y += border_1 + pad_1;

			max_x = x + font_info.width + border_1 + pad_1;
			max_y = y + font_info.height + border_1 + pad_1;
		}
		else
		{
			x += border_2 + pad_2;
			y += (2 * border_1) + pad_2;

			max_x = x + font_info.width + border_2 + pad_2;
			max_y = y + font_info.height + border_2 + pad_2;
		}
	}

	if((max_x >= display.x_size) || (max_y >= display.y_size))
		goto skip;

	if(!fgcolour_set(colour, colour, colour))
		return(false);

	if(!font_render(code, font_cell))
		return(false);

	for(byte = 0; byte < sizeof(data); byte++)
		data[byte] = 0;

	byte = 0;
	bit = 7;

	for(y2 = 0; y2 < font_info.height; y2++)
	{
		for(x2 = 0; x2 < font_info.width; x2++)
		{
			if(font_cell[y2][x2])
				data[byte] |= (1 << bit);

			if(bit == 0)
			{
				bit = 7;
				byte++;
			}
			else
				bit--;
		}

		if(bit != 7)
		{
			bit = 7;
			byte++;
		}
	}

	if(bit != 7)
		byte++;

	if(!blit(x, y, font_info.width, font_info.height, byte, data))
		return(false);

skip:
	text.column++;

	return(true);
}

static attr_result_used bool text_newline(void)
{
	unsigned int x1, x2;
	unsigned int y1, y2;
	unsigned int rows;
	font_info_t font_info;

	if(!font_get_info(&font_info))
		return(false);

	rows = display.y_size / font_info.height;

	if(display.logmode)
	{
		if((text.row + 1U) >= rows)
		{
			if(!scroll(0, font_info.height, 0, 0, display.x_size, display.y_size - font_info.height))
				return(false);
		}
		else
			text.row++;

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

static bool bright(int brightness);

static bool init(void)
{
	string_new(, error, 64);
	unsigned int mode;
	int user_cs_io, user_cs_pin;
	unsigned int mwcr0;

	if(!config_get_uint("display_eastrising.mode", &mode, -1, -1))
		goto error;

	display.mode = (display_mode_t)mode;

	if(!config_get_int("display_eastrising.io", &user_cs_io, -1, -1) ||
			!config_get_int("display_eastrising.pin", &user_cs_pin, -1, -1))
	{
		pin.user_cs.io = user_cs_io = -1;
		pin.user_cs.pin = user_cs_pin = -1;
	}
	else
	{
		pin.user_cs.io = user_cs_io;
		pin.user_cs.pin = user_cs_pin;
	}

	if(!spi_configure(&error, spi_mode_0, true, pin.user_cs.io, pin.user_cs.pin))
	{
		log("eastrising init configure: %s\n", string_to_cstr(&error));
		return(false);
	}

	display.logmode = 1;
	display.graphic_mode = 1;
	display.brightness = 4;
	display.low_brightness = 0;
	display.x_size = 480;
	display.y_size = 272;

	// init PLL

	if(display.mode == display_mode_i2c)
		set_i2c_speed(10000);

	if(!write_command(reg_pll_c1))
		goto error;

	if(!write_data_1(reg_pllc1_value))
		(void)0;

	msleep(1);

	if(!write_command(reg_pll_c2))
		goto error;

	if(!write_data_1(reg_pllc2_value))
		(void)0;

	msleep(1);

	if(display.mode == display_mode_i2c)
		set_i2c_speed(-1);

	if(!write_command_data_1(reg_sysr, reg_sysr_color_depth_16 | reg_sysr_if_8bit))
		goto error;

	if(!write_command_data_1(reg_pcsr, reg_pcsr_sample_falling_edge | reg_pcsr_clock_period_system_by_8))
		goto error;

	// horizontal

	if(!write_command_data_1(reg_hdwr, (display.x_size / 8) - 1))
		goto error;

	if(!write_command_data_1(reg_hndftr, reg_hndftr_de_polarity_active_high | (horizontal_blanking_fine / 2)))
		goto error;

	if(!write_command_data_1(reg_hndr, (horizontal_blanking / 8) - 1))
		goto error;

	if(!write_command_data_1(reg_hstr, (horizontal_sync_start / 8) - 1))
		goto error;

	if(!write_command_data_1(reg_hpwr, reg_hpwr_hsync_polarity_active_low | ((horizontal_sync_length / 8) - 1)))
		goto error;

	// vertical

	if(!write_command_data_1(reg_vdhr0, ((display.y_size >> 0) & 0xff) + 1))
		goto error;

	if(!write_command_data_1(reg_vdhr1, ((display.y_size >> 8) & 0x01) + 0))
		goto error;

	if(!write_command_data_1(reg_vndr0, ((vertical_blanking >> 0) & 0xff) + 1))
		goto error;

	if(!write_command_data_1(reg_vndr1, ((vertical_blanking >> 8) & 0x01) + 0))
		goto error;

	if(!write_command_data_1(reg_vstr0, ((vertical_sync_start >> 0) & 0xff) + 1))
		goto error;

	if(!write_command_data_1(reg_vstr1, ((vertical_sync_start >> 8) & 0x01) + 0))
		goto error;

	if(!write_command_data_1(reg_vpwr, reg_vpwr_vsync_polarity_active_low | (vertical_sync_length - 1)))
		goto error;

	// PWM

	if(!write_command_data_1(reg_p1cr, reg_p1cr_pwm1_enable | reg_p1cr_function_pwm1 | reg_p1cr_clock_ratio_2048)) // 114 Hz refresh
		goto error;

	if(!write_command_data_1(reg_pwrr, reg_pwrr_display_enable | reg_pwrr_display_sleep_mode_disable | reg_pwrr_display_reset_complete))
		goto error;

	// MISC

	if(!write_command_data_1(reg_dpcr, reg_dpcr_two_layer | reg_dpcr_hor_scan_ltor | reg_dpcr_vert_scan_ltor))
		goto error;

	mwcr0 =	reg_mwcr0_mode_graphic					|
			reg_mwcr0_cursor_invisible				|
			reg_mwcr0_cursor_steady					|
			reg_mwcr0_memory_write_direction_lrtd	|
			reg_mwcr0_memory_write_autoincr_en		|
			reg_mwcr0_memory_read_autoincr_en;

	if(!write_command_data_1(reg_mwcr0, mwcr0))
		return(false);

	if(!show_layer(1))
		goto error;

	if(!set_active_layer(1))
		return(false);

	if(!box(0xff, 0x00, 0x00, 0, 0, display.x_size - 1, display.y_size - 1))
		goto error;

	if(!show_layer(0))
		goto error;

	if(!set_active_layer(0))
		return(false);

	if(!box(0x00, 0x00, 0x00, 0, 0, display.x_size - 1, display.y_size - 1))
		goto error;

	if(!fgcolour_set(0xff, 0xff, 0xff))
		goto error;

	if(!bright(4))
		goto error;

	return(true);

error:
	if(display.mode == display_mode_i2c)
		set_i2c_speed(-1);
	display.mode = display_mode_disabled;
	return(false);
}

static bool begin(unsigned int slot, bool logmode)
{
	unsigned int x1, x2, y1, y2;
	unsigned int r, g, b;
	font_info_t font_info;

	if((display.mode != display_mode_i2c) &&
			(display.mode != display_mode_spi_mosi_miso) &&
			(display.mode != display_mode_spi_mosi))
		return(false);

	text.column = text.row = 0;
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
		x2 = display.x_size - 1 - border_2 - 1;
		y2 = display.y_size - 1 - border_2 - 1;

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
		}
		else
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
	if(string_length(pixels) == 0)
		return(true);

	if(x >= display.x_size)
		return(false);

	if(y >= display.y_size)
		return(false);

	if((unsigned int)string_length(pixels) < (pixel_amount * 2))
		return(false);

	string_setlength(pixels, pixel_amount * 2);

	if((x >= 0) && (y >= 0))
	{
		if(!write_command_data_1(reg_curh1, (x & 0xff00) >> 8))
			return(false);

		if(!write_command_data_1(reg_curh0, (x & 0x00ff) >> 0))
			return(false);

		if(!write_command_data_1(reg_curv1, (y & 0xff00) >> 8))
			return(false);

		if(!write_command_data_1(reg_curv0, (y & 0x00ff) >> 0))
			return(false);
	}

	display.graphic_mode = 1;

	if(!write_command(reg_mrwc))
		return(false);

	if(!write_data(string_length(pixels), (const unsigned char *)string_buffer(pixels)))
		return(false);

	return(true);
}

static bool freeze(bool active)
{
	if(active)
	{
		if(!copy_layer(0, 1))
			return(false);
		if(!show_layer(1))
			return(false);
	}
	else
		if(!show_layer(0))
			return(false);

	return(true);
}

static bool bright(int brightness)
{
	roflash static const unsigned int bright_level[5] = { 0, 5, 20, 110, 255 };
	roflash static const unsigned int bright_power[5] = { reg_pwrr_display_disable, reg_pwrr_display_enable,
				reg_pwrr_display_enable, reg_pwrr_display_enable, reg_pwrr_display_enable };
	roflash static const unsigned int bright_low[5] = { 0, 1, 1, 0, 0 };

	if(brightness > 4)
		return(false);

	if(!write_command_data_1(reg_p1dcr, bright_level[brightness]))
		return(false);

	if(!write_command_data_1(reg_pwrr, bright_power[brightness] | reg_pwrr_display_sleep_mode_disable | reg_pwrr_display_reset_complete))
		return(false);

	display.low_brightness = bright_low[brightness] ? true : false;

	return(true);
}

roflash const char help_description_display_eastrising[] =
		"display eastrising <mode=0=disabled|1=i2c|2=hspi-mosi-miso|3=hspi-mosi [<user cs io> <user cs pin>]";

app_action_t application_function_display_eastrising(string_t *src, string_t *dst)
{
	display_mode_t mode;
	int user_cs_io, user_cs_pin;

	if(parse_uint(1, src, &mode, 0, ' ') == parse_ok)
	{
		if(mode > display_mode_size)
		{
			string_append_cstr_flash(dst, help_description_display_eastrising);
			return(app_action_error);
		}

		if((parse_int(2, src, &user_cs_io, 0, ' ') == parse_ok) && (parse_int(3, src, &user_cs_pin, 0, ' ') == parse_ok))
		{
			if(mode != 2)
			{
				string_append_cstr_flash(dst, help_description_display_eastrising);
				return(app_action_error);
			}

			if((user_cs_io < 0) || (user_cs_io >= io_id_size) || (user_cs_pin < 0) || (user_cs_pin >= max_pins_per_io))
			{
				string_append_cstr_flash(dst, help_description_display_eastrising);
				return(app_action_error);
			}
		}
		else
		{
			user_cs_io = -1;
			user_cs_pin = -1;
		}

		if(!config_open_write())
			goto config_error;

		if(mode == 0)
			config_delete("display_eastrising.", true, -1, -1);
		else
		{
			if(!config_set_uint("display_eastrising.mode", mode, -1, -1))
				goto config_error;

			if((user_cs_io < 0) || (user_cs_pin < 0))
			{
				config_delete("display_eastrising.io", false, -1, -1);
				config_delete("display_eastrising.pin", false, -1, -1);
			}
			else
			{
				if(!config_set_uint("display_eastrising.io", user_cs_io, -1, -1))
					goto config_error;

				if(!config_set_uint("display_eastrising.pin", user_cs_pin, -1, -1))
					goto config_error;
			}
		}

		if(!config_close_write())
			goto config_error;
	}

	if(!config_get_uint("display_eastrising.mode", &mode, -1, -1))
		mode = 0;

	if(!config_get_int("display_eastrising.io", &user_cs_io, -1, -1) ||
			!config_get_int("display_eastrising.pin", &user_cs_pin, -1, -1))
		user_cs_io = user_cs_pin = -1;

	switch(mode)
	{
		case(display_mode_disabled): string_append(dst, "> mode 0 (disabled)"); break;
		case(display_mode_i2c): string_append(dst, "> mode 1 (i2c)"); break;
		case(display_mode_spi_mosi_miso):
		case(display_mode_spi_mosi):
		{
			if(mode == display_mode_spi_mosi_miso)
				string_append(dst, "> mode 2 (spi using output/mosi and input/miso), ");
			else
				string_append(dst, "> mode 3 (spi using output/mosi only), ");

			if((user_cs_io >= 0) && (user_cs_pin >= 0))
				string_format(dst, "user cs pin: %d/%d", user_cs_io, user_cs_pin);
			else
				string_append(dst, "default cs pin");

			break;
		}

		default: string_append(dst, "> unknown mode"); break;
	}

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

	strncpy(infostruct->name, "Eastrising TFT", sizeof(infostruct->name));

	infostruct->columns = columns;
	infostruct->rows = rows;
	infostruct->cell_width = cell_width;
	infostruct->cell_height = cell_height;
	infostruct->width = display.x_size;
	infostruct->height = display.y_size;
	infostruct->pixel_mode = display_pixel_mode_16_rgb;

	return(true);
}

roflash const display_hooks_t display_hooks_eastrising =
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
	freeze,
};
