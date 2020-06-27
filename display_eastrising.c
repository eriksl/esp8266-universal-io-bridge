#include "display.h"
#include "display_eastrising.h"
#include "i2c.h"
#include "spi.h"
#include "config.h"
#include "sys_time.h"
#include "dispatch.h"
#include "util.h"
#include "font/font_32x16.h"

#include <stdint.h>
#include <stdbool.h>

typedef struct
{
	unsigned int	unicode;
	unsigned int	internal;
} unicode_map_t;

typedef enum
{
	display_mode_disabled = 0,
	display_mode_i2c,
	display_mode_spi,
} display_mode_t;

typedef enum
{
	render_mode_internal,
	render_mode_external,
	render_mode_graphic,
} render_mode_t;

typedef enum
{
	pls_idle = 0,
	pls_start,
	pls_in_progress,
} picture_load_state_t;

enum
{
	mapeof = 0xffffffff,
};

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
	reg_sroc =		0x05,
	reg_sfclr =		0x06,
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
	reg_fncr0 =		0x21,
	reg_fncr1 =		0x22,
	reg_fldr =		0x29,
	reg_curxl =		0x2a,
	reg_curxh =		0x2b,
	reg_curyl =		0x2c,
	reg_curyh =		0x2d,
	reg_fncr2 =		0x2e,
	reg_serfont =	0x2f,
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
	reg_bgtr0 =		0x67,
	reg_bgtr1 =		0x68,
	reg_bgtr2 =		0x69,
	reg_pll_c1 =	0x88,
	reg_pll_c2 =	0x89,
	reg_p1cr =		0x8a,
	reg_p1dcr =		0x8b,
	reg_mclr =		0x8e,
	reg_sacs_mode =	0xe0,
	reg_sacs_addr = 0xe1,
	reg_sacs_data =	0xe2,

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

	reg_sroc_if_select_0 =					0b00000000,
	reg_sroc_if_select_1 =					0b10000000,
	reg_sroc_addr_mode_24 =					0b00000000,
	reg_sroc_addr_mode_32 =					0b01000000,
	reg_sroc_spi_mode_0 =					0b00000000,
	reg_sroc_spi_mode_3 =					0b00100000,
	reg_sroc_mode_4bus =					0b00000000,
	reg_sroc_mode_5bus =					0b00001000,
	reg_sroc_mode_6bus =					0b00010000,
	reg_sroc_access_font =					0b00000000,
	reg_sroc_access_dma =					0b00000100,
	reg_sroc_data_mode_dual_1 =				0b00000011,
	reg_sroc_data_mode_dual_0 =				0b00000010,
	reg_sroc_data_mode_single =				0b00000000,

	reg_sfclr_by_1 =						0b00000000,
	reg_sfclr_by_2 =						0b00000010,
	reg_sfclr_by_4 =						0b00000011,

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

	reg_mwcr0_default =						reg_mwcr0_cursor_invisible | reg_mwcr0_cursor_steady | reg_mwcr0_memory_write_direction_lrtd | reg_mwcr0_memory_write_autoincr_en | reg_mwcr0_memory_read_autoincr_en,

	reg_fncr0_font_cgrom =					0b00000000,
	reg_fncr0_font_cgram =					0b10000000,
	reg_fncr0_font_internal =				0b00000000,
	reg_fncr0_font_external =				0b00100000,
	reg_fncr0_encoding_8859_1 =				0b00000000,
	reg_fncr0_encoding_8859_2 =				0b00000001,
	reg_fncr0_encoding_8859_3 =				0b00000010,
	reg_fncr0_encoding_8859_4 =				0b00000011,

	reg_fncr1_font_align_disable =			0b00000000,
	reg_fncr1_font_align_enable =			0b10000000,
	reg_fncr1_font_transparent =			0b01000000,
	reg_fncr1_font_opaque =					0b00000000,
	reg_fncr1_font_straight =				0b00000000,
	reg_fncr1_font_rotate_90 =				0b00010000,
	reg_fncr1_font_enlarge_hor_x1 =			0b00000000,
	reg_fncr1_font_enlarge_hor_x2 =			0b00000100,
	reg_fncr1_font_enlarge_hor_x3 =			0b00001000,
	reg_fncr1_font_enlarge_hor_x4 =			0b00001100,
	reg_fncr1_font_enlarge_ver_x1 =			0b00000000,
	reg_fncr1_font_enlarge_ver_x2 =			0b00000001,
	reg_fncr1_font_enlarge_ver_x3 =			0b00000010,
	reg_fncr1_font_enlarge_ver_x4 =			0b00000011,

	reg_fncr2_font_size_16x16 =				0b00000000,
	reg_fncr2_font_size_24x24 =				0b01000000,
	reg_fncr2_font_size_32x32 =				0b10000000,

	reg_serfont_type_gt21l16tw =			0b00000000,
	reg_serfont_type_gt30l16u2w =			0b00100000,
	reg_serfont_type_gt30l24t3y =			0b01000000,
	reg_serfont_type_gt30l24m1z =			0b01100000,
	reg_serfont_type_gt30l32s4w =			0b10000000,
	reg_serfont_enc_gb2312 =				0b00000000,
	reg_serfont_enc_gb12345 =				0b00000100,
	reg_serfont_enc_big5 =					0b00001000,
	reg_serfont_enc_unicode =				0b00001100,
	reg_serfont_enc_ascii =					0b00010000,
	reg_serfont_enc_unijapanese =			0b00010100,
	reg_serfont_enc_jis0208 =				0b00011000,
	reg_serfont_enc_iso =					0b00011100,
	reg_serfont_font_alternative_1 =		0b00000000,
	reg_serfont_font_alternative_2 =		0b00000001,
	reg_serfont_font_alternative_3 =		0b00000010,
	reg_serfont_font_alternative_4 =		0b00000011,

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

	reg_vdbe1_destination_layer0 =			0b00000000,
	reg_vdbe1_destination_layer1 =			0b10000000,

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

	reg_sacs_mode_font_dma =				0b00000000,
	reg_sacs_mode_direct_access =			0b00000001,

	display_width = 480,
	display_height = 272,
	display_horizontal_blanking = 38,
	display_horizontal_blanking_fine = 4,
	display_horizontal_sync_start = 16,
	display_horizontal_sync_length = 32,
	display_vertical_blanking = 14,
	display_vertical_sync_start = 6,
	display_vertical_sync_length = 2,
	display_text_width = 26,
	display_text_height = 8,
	display_slot_height = 4,
	display_character_width = 16,
	display_character_width_padding = 3,
	display_character_height = 32,
	display_character_slot_padding = 16,
	display_logmode_text_width = 42,
	display_logmode_text_height = 17,
	display_logmode_character_width = 8,
	display_logmode_character_width_padding = 2,
	display_logmode_character_height = 16,

	display_flash_memory_map_start = 0x40200000,
};

roflash static const unicode_map_t unicode_map_internal_font[] =
{
	{	0x263a, 0x01	},	//	☺ 
	{	0x263b, 0x02	},	//	☻ 
	{	0x2665, 0x03	},	//	♥ 
	{	0x25c6, 0x04	},	//	◆ 
	{	0x2663, 0x05	},	//	♣ 
	{	0x2660, 0x06	},	//	♠ 
	{	0x25cf, 0x07	},	//	● 
	{	0x25c8, 0x08	},	//	◈
	{	0x25ef, 0x09	},	//	◯ 
	{	0x25d9, 0x0a	},	//	◙ 
	{	0x2642, 0x0b	},	//	♂ 
	{	0x2640, 0x0c	},	//	♀ 
	{	0x266a,	0x0d	},	//	♪ 
	{	0x266b,	0x0e	},	//	♫ 
	{	0x263c,	0x0f	},	//	☼ 
	{	0x25b6,	0x10	},	//	▶ 
	{	0x25c0,	0x11	},	//	◀ 
	{	0x2195,	0x12	},	//	↕ 
	{	0x203c,	0x13	},	//	‼ 
	{	0x00b6,	0x14	},	//	¶ 
	{	0x00a7,	0x15	},	//	§ 
	{	0x2580,	0x16	},	//	▀ 
	{	0x21a8,	0x17	},	//	↨  
	{	0x2191,	0x18	},	//	↑ 
	{	0x2193,	0x19	},	//	↓ 
	{	0x2192,	0x1a	},	//	→ 
	{	0x2190,	0x1b	},	//	←
	{	0x250c,	0x1c	},	//	┌ 
	{	0x2194,	0x1d	},	//	↔  
	{	0x25b2,	0x1e	},	//	▲ 
	{	0x25bc,	0x1f	},	//	▼ 
	{	0x2588,	0x7f	},	//	█ 
	{	0x20ac,	0x80	},	//	€ 
	{	0x201a,	0x82	},	//	‚ 
	{	0x0192,	0x83	},	//	ƒ
	{	0x201e,	0x84	},	//	„
	{	0x2026,	0x85	},	//	…
	{	0x2020,	0x86	},	//	† 
	{	0x2021,	0x87	},	//	‡ 
	{	0x02c6,	0x88	},	//	ˆ 
	{	0x2030,	0x89	},	//	‰ 
	{	0x0160,	0x8a	},	//	Š
	{	0x2039,	0x8b	},	//	‹ 
	{	0x0152,	0x8c	},	//	Œ 
	{	0x017d,	0x8e	},	//	Ž
	{	0x2018,	0x91	},	//	‘ 
	{	0x2019,	0x92	},	//	’
	{	0x201c,	0x93	},	//	“ 
	{	0x201d,	0x94	},	//	” 
	{	0x2022,	0x95	},	//	•
	{	0x2013,	0x96	},	//	–
	{	0x2014,	0x97	},	//	—
	{	0x02dc,	0x98	},	//	˜
	{	0x2122,	0x99	},	//	™ 
	{	0x0161,	0x9a	},	//	š 
	{	0x203a,	0x9b	},	//	›
	{	0x0153,	0x9c	},	//	œ 
	{	0x017e,	0x9e	},	//	ž
	{	0x0178,	0x9f	},	//	Ÿ
	{	mapeof,	0x00	},	// EOF
};

roflash static const unicode_map_t unicode_map_external_font_chip[] =
{
	{	0x00e0,	0xa8a4	},	//	à
	{	0x00e1,	0xa8a2	},	//	á
	{	0x00e8,	0xa8a8	},	//	è
	{	0x00e9,	0xa8a6	},	//	é
	{	0x00ea,	0xa8ba	},	//	ê
	{	0x00ec,	0xa8ac	},	//	ì
	{	0x00ed,	0xa8aa	},	//	í
	{	0x00f2,	0xa8b0	},	//	ò
	{	0x00f3,	0xa8ae	},	//	ó
	{	0x00f9,	0xa8b4	},	//	ù
	{	0x00fa,	0xa8b2	},	//	ú
	{	0x00fc,	0xa8b9	},	//	ü
	{	mapeof,	0x00	},	// EOF
};

static display_mode_t display_mode;
static unsigned int display_user_cs_io;
static unsigned int display_user_cs_pin;
static bool display_use_fontchip;
static bool display_low_brightness = false;
static bool display_logmode = false;
static bool display_font_valid = false;
static unsigned int display_text_current;
static unsigned int display_x, display_y;
static unsigned int display_current_slot;
static picture_load_state_t picture_load_state = pls_idle;
static unsigned int picture_load_index = 0;
static unsigned int picture_load_flash_sector = 0, picture_load_sector_offset = 0, picture_load_current = 0;

static bool attr_result_used display_write_glyphs_16x32(bool ucs2, unsigned int length, const uint8_t *text);

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

static render_mode_t display_render_mode(void)
{
	if(display_logmode)
		return(render_mode_internal);

	if(display_use_fontchip)
		return(render_mode_external);

	if(display_font_valid)
		return(render_mode_graphic);

	return(render_mode_internal);
}

static bool attr_result_used display_write_command(uint8_t cmd)
{
	switch(display_mode)
	{
		case(display_mode_disabled): return(false);
		case(display_mode_i2c):
		{
			return(i2c_send1(i2c_addr_command, cmd) == i2c_error_ok);
		}

		case(display_mode_spi):
		{
			string_new(, error, 64);

			if(spi_send_receive(spi_clock_10M, spi_mode_0, false, display_user_cs_io, display_user_cs_pin,
						true, spi_rs_cmd | spi_rw_write, 1, &cmd, 0, 0, (uint8_t *)0, &error))
				return(true);

			logf("eastrising write command: %s\n", string_to_cstr(&error));
		}
	}

	return(false);
}

static bool display_write_data(uint8_t data)
{
	switch(display_mode)
	{
		case(display_mode_disabled): return(false);
		case(display_mode_i2c):
		{
			return(i2c_send1(i2c_addr_data, data) == i2c_error_ok);
		}

		case(display_mode_spi):
		{
			string_new(, error, 64);

			if(spi_send_receive(spi_clock_10M, spi_mode_0, false, display_user_cs_io, display_user_cs_pin,
						true, spi_rs_data | spi_rw_write,
						1, &data, 0, 0, (uint8_t *)0, &error))
				return(true);

			logf("eastrising write data: %s\n", string_to_cstr(&error));
		}
	}

	return(false);
}

static bool display_write(uint8_t cmd, uint8_t data)
{
	if(!display_write_command(cmd))
		return(false);

	return(display_write_data(data));
}

static bool attr_result_used display_write_string(bool raw_pixel_data, unsigned int amount, const uint8_t *data)
{
	string_new(, error, 64);

	if(!display_write_command(reg_mrwc))
		return(false);

	switch(display_mode)
	{
		case(display_mode_disabled): return(false);
		case(display_mode_i2c):
		{
			return(i2c_send(i2c_addr_data, amount, data) == i2c_error_ok);
		}

		case(display_mode_spi):
		{
			unsigned int offset, left, chunk;
			spi_clock_t clock;

			if(raw_pixel_data)
				clock = spi_clock_20M;
			else
			{
				switch(display_render_mode())
				{
					case(render_mode_internal):	clock = spi_clock_500k;	break;
					case(render_mode_external):	clock = spi_clock_50k;	break;
					default:					clock = spi_clock_100k;	break; // never hit
				}
			}

			for(offset = 0, left = amount; (left > 0) && (offset < amount);)
			{
				chunk = left;

				if(chunk > 32)
					chunk = 32;

				if(!spi_send_receive(clock, spi_mode_0, false, display_user_cs_io, display_user_cs_pin,
							true, spi_rs_data | spi_rw_write,
							chunk, &data[offset],
							0,
							0, (uint8_t *)0, &error))
				{
					logf("eastrising write string: %s\n", string_to_cstr(&error));
					return(false);
				}

				offset += chunk;
				left -= chunk;
			}

			return(true);
		}
	}

	return(false);
}

static bool attr_result_used display_read(uint8_t cmd, uint8_t *data)
{
	if(!display_write_command(cmd))
		return(false);

	switch(display_mode)
	{
		case(display_mode_disabled): return(false);
		case(display_mode_i2c):
		{
			return(i2c_receive(i2c_addr_data, 1, data) == i2c_error_ok);
		}

		case(display_mode_spi):
		{
			string_new(, error, 64);

			if(spi_send_receive(spi_clock_10M, spi_mode_0, false, display_user_cs_io, display_user_cs_pin,
						true, spi_rs_data | spi_rw_read,
						0, (const uint8_t *)0, 0, 1, data, &error))

				return(true);

			logf("eastrising read: %s\n", string_to_cstr(&error));
		}
	}

	return(false);
}

static bool attr_result_used display_scroll(unsigned int x0, unsigned int y0, unsigned int x1, unsigned int y1, unsigned int width, unsigned height)
{
	uint8_t data;
	unsigned int timeout;

	if(!display_write(reg_hsbe0, (x0 >> 0) & 0xff))
		return(false);

	if(!display_write(reg_hsbe1, (x0 >> 8) & 0x03))
		return(false);

	if(!display_write(reg_vsbe0, (y0 >> 0) & 0xff))
		return(false);

	if(!display_write(reg_vsbe1, ((y0 >> 8) & 0x01) | reg_vsbe1_source_layer_0))
		return(false);

	if(!display_write(reg_hdbe0, (x1 >> 0) & 0xff))
		return(false);

	if(!display_write(reg_hdbe1, (x1 >> 8) & 0x03))
		return(false);

	if(!display_write(reg_vdbe0, (y1 >> 0) & 0xff))
		return(false);

	if(!display_write(reg_vdbe1, ((y1 >> 8) & 0x01) | reg_vsbe1_source_layer_0))
		return(false);

	if(!display_write(reg_bewr0, (width >> 0) & 0xff))
		return(false);

	if(!display_write(reg_bewr1, (width >> 8) & 0x03))
		return(false);

	if(!display_write(reg_behr0, (height >> 0) & 0xff))
		return(false);

	if(!display_write(reg_behr1, (height >> 8) & 0x03))
		return(false);

	// config BTE

	if(!display_write(reg_becr1, reg_becr1_rop_code_move_pos | reg_becr1_rop_func_s))
		return(false);

	// start BTE

	if(!display_write(reg_becr0, reg_becr0_busy | reg_becr0_src_block | reg_becr0_dst_block))
		return(false);

	for(timeout = 5; timeout > 0; timeout--)
	{
		if(!display_read(reg_becr0, &data))
			return(false);

		if(!(data & reg_becr0_busy))
			break;

		msleep(10);
	}

	if(timeout == 0)
		log("scroll BTE timeout\n");

	return(true);
}

static bool attr_result_used display_set_active_layer(unsigned int layer)
{
	return(display_write(reg_mwcr1, reg_mwcr1_graphic_cursor_disable | reg_mwcr1_write_destination_layer | (layer & 0x01)));
}

static bool attr_result_used display_fgcolour_get(unsigned int *r, unsigned int *g, unsigned int *b)
{
	uint8_t R, G, B;

	if(!display_read(reg_fgcr0, &R))
		return(false);

	if(!display_read(reg_fgcr1, &G))
		return(false);

	if(!display_read(reg_fgcr2, &B))
		return(false);

	*r = (R << 3);
	*g = (G << 2);
	*b = (B << 3);

	return(true);
}

static bool attr_result_used display_bgcolour_get(unsigned int *r, unsigned int *g, unsigned int *b)
{
	uint8_t R, G, B;

	if(!display_read(reg_bgcr0, &R))
		return(false);

	if(!display_read(reg_bgcr1, &G))
		return(false);

	if(!display_read(reg_bgcr2, &B))
		return(false);

	*r = (R << 3);
	*g = (G << 2);
	*b = (B << 3);

	return(true);
}

static bool attr_result_used display_fgcolour_set(unsigned int r, unsigned int g, unsigned int b)
{
	r = (r >> 3) & 0xff;
	g = (g >> 2) & 0xff;
	b = (b >> 3) & 0xff;

	if(!display_write(reg_fgcr0, r))
		return(false);

	if(!display_write(reg_fgcr1, g))
		return(false);

	if(!display_write(reg_fgcr2, b))
		return(false);

	return(true);
}

static bool attr_result_used display_bgcolour_set(unsigned int r, unsigned int g, unsigned int b)
{
	r = (r >> 3) & 0xff;
	g = (g >> 2) & 0xff;
	b = (b >> 3) & 0xff;

	if(!display_write(reg_bgcr0, r))
		return(false);

	if(!display_write(reg_bgcr1, g))
		return(false);

	if(!display_write(reg_bgcr2, b))
		return(false);

	return(true);
}

static bool attr_result_used display_clear_area(unsigned int layer, unsigned int r, unsigned int g, unsigned int b)
{
	unsigned int timeout;
	uint8_t data;
	unsigned int fg_r, fg_g, fg_b;
	unsigned int bg_r, bg_g, bg_b;

	if(!display_set_active_layer(layer))
		return(false);

	if(!display_fgcolour_get(&fg_r, &fg_g, &fg_b))
		return(false);

	if(!display_bgcolour_get(&bg_r, &bg_g, &bg_b))
		return(false);

	if(!display_fgcolour_set(r, g, b))
		return(false);

	if(!display_bgcolour_set(r, g, b))
		return(false);

	if(!display_write(reg_mclr, reg_mclr_memory_clear_start | reg_mclr_memory_area_active_window))
		return(false);

	for(timeout = 20; timeout > 0; timeout--)
	{
		if(!display_read(reg_mclr, &data))
			return(false);

		if(!(data & reg_mclr_memory_clear_start))
			break;

		msleep(1);
	}

	if(!display_set_active_layer(0))
		return(false);

	if(!display_fgcolour_set(fg_r, fg_g, fg_b))
		return(false);

	if(!display_bgcolour_set(bg_r, bg_g, bg_b))
		return(false);

	return(true);
}

static bool attr_result_used display_set_active_window(unsigned int x0, unsigned int y0, unsigned int x1, unsigned int y1)
{
	x0 = umin(x0, display_width - 1);
	x1 = umin(x1, display_width - 1);
	y0 = umin(y0, display_height - 1);
	y1 = umin(y1, display_height - 1);

	if(!display_write(reg_hsaw0, (x0 >> 0) & 0xff))
		return(false);

	if(!display_write(reg_hsaw1, (x0 >> 8) & 0x03))
		return(false);

	if(!display_write(reg_vsaw0, (y0 >> 0) & 0xff))
		return(false);

	if(!display_write(reg_vsaw1, (y0 >> 8) & 0x01))
		return(false);

	if(!display_write(reg_heaw0, (x1 >> 0) & 0xff))
		return(false);

	if(!display_write(reg_heaw1, (x1 >> 8) & 0x03))
		return(false);

	if(!display_write(reg_veaw0, (y1 >> 0) & 0xff))
		return(false);

	if(!display_write(reg_veaw1, (y1 >> 8) & 0x01))
		return(false);

	return(true);
}

static bool attr_result_used display_fill_box(unsigned int layer, unsigned int x0, unsigned int y0, unsigned int x1, unsigned int y1, unsigned int r, unsigned int g, unsigned int b)
{
	if(!display_set_active_window(x0, y0, x1, y1))
		return(false);

	if(!display_clear_area(layer, r, g, b))
		return(false);

	if(!display_set_active_window(0, 0, display_width, display_height))
		return(false);

	return(true);
}

static unsigned int attr_result_used display_text_to_graphic_x(unsigned int text_x)
{
	unsigned int width =	display_logmode ? display_logmode_character_width :			display_character_width;
	unsigned int padding =	display_logmode ? display_logmode_character_width_padding : display_character_width_padding;

	return(text_x * (width + padding));
}

static unsigned int attr_result_used display_text_to_graphic_y(unsigned int text_y)
{
	unsigned int cell_height = display_logmode ? display_logmode_character_height : display_character_height;
	unsigned int graphic_y;

	graphic_y = text_y * cell_height;

	if(!display_logmode)
	{
		graphic_y += display_slot_height * cell_height;
		graphic_y += display_character_slot_padding;
	}

	return(graphic_y);
}

static void display_data_clear(void)
{
	display_text_current = 0;
}

static bool attr_result_used display_flush(void)
{
	bool result = false;

	switch(display_render_mode())
	{
		case(render_mode_graphic):
		{
			if((display_text_current == 0) || (display_y >= display_slot_height))
			{
				result = true;
				goto done;
			}

			if(!display_write_glyphs_16x32(true, display_text_current, display_buffer))
				goto done;

			break;
		}

		case(render_mode_internal):
		case(render_mode_external):
		{
			if(!display_write_string(false, display_text_current, display_buffer))
				goto done;

			break;
		}
	}

	result = true;
done:
	display_data_clear();
	return(result);
}

static void display_data_output(unsigned int text)
{
	if((display_text_current + 2) >= display_buffer_size)
		return;

	display_buffer[display_text_current++] = text & 0xff;
}

attr_inline unsigned int text_width(void)
{
	if(display_logmode)
		return(display_logmode_text_width);
	else
		return(display_text_width);
}

attr_inline unsigned int text_height(void)
{
	if(display_logmode)
		return(display_logmode_text_height);
	else
		return(display_slot_height);
}

static bool attr_result_used text_goto_line(unsigned int y)
{
	unsigned int gx, gy;

	display_x = 0;
	display_y = y;

	gx = display_text_to_graphic_x(display_x) + 4;
	gy = display_text_to_graphic_y(display_y);

	if(!display_write(reg_curxl, (gx >> 0) & 0xff))
		return(false);

	if(!display_write(reg_curxh, (gx >> 8) & 0x03))
		return(false);

	if(!display_write(reg_curyl, (gy >> 0) & 0xff))
		return(false);

	if(!display_write(reg_curyh, (gy >> 8) & 0x03))
		return(false);

	return(true);
}

static bool display_newline(void)
{
	unsigned int char_height =	display_logmode ? display_logmode_character_height : display_character_height;
	unsigned int y;

	if(!display_flush())
		return(false);

	y = display_y + 1;

	if(display_logmode)
	{
		if(y >= text_height())
		{
			static const	unsigned int x0 = 0;
							unsigned int y0 = 1 * char_height;
			static const	unsigned int x1 = 0;
			static const	unsigned int y1 = 0;
			static const	unsigned int width = display_width;
							unsigned int height = text_height() * char_height;

			if(!display_scroll(x0, y0, x1, y1, width, height))
				return(false);

			if(!display_fill_box(0, 0, display_text_to_graphic_y(text_height() - 1), display_width, display_height, 0x00, 0x00, 0x00))
				return(false);

			y = text_height() - 1;
		}
	}

	if(!text_goto_line(y))
		return(false);

	return(true);
}

static bool attr_result_used display_show_layer(unsigned int layer)
{
	unsigned int value = reg_ltpr0_scroll_both | reg_ltpr0_floatwin_transparency_dis;

	if(!display_write(reg_ltpr1, reg_ltpr1_transparency_layer_2_8_8 | reg_ltpr1_transparency_layer_1_8_8))
		return(false);

	value |= (layer == 0) ? reg_ltpr0_visible_layer_1 : reg_ltpr0_visible_layer_2;

	return(display_write(reg_ltpr0, value));
}

static bool attr_result_used display_blit(unsigned int layer, unsigned int x, unsigned int y, unsigned int width, unsigned int height, uint8_t *data)
{
	bool success = false;

	if((x + width) > display_width)
		return(true);

	if((y + height) > display_height)
		return(true);

	if(!display_write(reg_hdbe0, (x >> 0) & 0xff))
		goto error;

	if(!display_write(reg_hdbe1, (x >> 8) & 0x03))
		goto error;

	if(!display_write(reg_vdbe0, (y >> 0) & 0xff))
		goto error;

	if(!display_write(reg_vdbe1, ((y >> 8) & 0x01) | ((layer > 0) ? reg_vdbe1_destination_layer1 : reg_vdbe1_destination_layer0)))
		goto error;

	if(!display_write(reg_bewr0, (width >> 0) & 0xff))
		goto error;

	if(!display_write(reg_bewr1, (width >> 8) & 0x03))
		goto error;

	if(!display_write(reg_behr0, (height >> 0) & 0xff))
		goto error;

	if(!display_write(reg_behr1, (height >> 8) & 0x03))
		goto error;

	// config BTE

	if(!display_write(reg_becr1, reg_becr1_rop_code_expand_colour_transp | reg_becr1_rop_expand_col_bit_start_0))
		goto error;

	// start BTE

	if(!display_write(reg_becr0, reg_becr0_busy))
		goto error;

	if(!display_write_command(reg_mrwc))
		goto error;

	for(y = 0; y < 32; y++)
	{
		switch(display_mode)
		{
			case(display_mode_spi):
			{
				if(!spi_send_receive(spi_clock_10M, spi_mode_0, false, display_user_cs_io, display_user_cs_pin,
							true, spi_rs_data | spi_rw_write,
							width / 8, &data[y * (width / 8)],
							0,
							0, (uint8_t *)0,
							(string_t *)0))
					goto error;

				break;
			}

			case(display_mode_i2c):
			{
				if(i2c_send(i2c_addr_data, width / 8, &data[y * (width / 8)]) != i2c_error_ok)
					goto error;

				break;
			}

			default:
			{
				goto error;
			}
		}
	}

	success = true;

error:
	if(!display_write(reg_becr0, 0))
		success = false;

	return(success);
}

static bool attr_result_used display_write_glyph(unsigned int column, unsigned int row, unsigned int codepoint)
{
	const font_bitmap_t			*font_bitmap;
	const font_bitmap_entry_t	*font_bitmap_entry;
	unsigned int				x, y;
	uint32_t					bitmap;
	uint8_t						data[32 * 2];

	// note: this will always use either mirror 0 or mirror 1 depending on which image/slot is loaded, due to the flash mapping window
	font_bitmap = (const font_bitmap_t *)(display_flash_memory_map_start + FONT_FLASH_OFFSET_0);

	for(font_bitmap_entry = font_bitmap->entries; font_bitmap_entry->codepoint != (uint32_t)font_codepoint_last_entry; font_bitmap_entry++)
		if(font_bitmap_entry->codepoint == codepoint)
			break;

	if(font_bitmap_entry->codepoint == (uint32_t)font_codepoint_last_entry)
		font_bitmap_entry = font_bitmap->entries - 1;

	for(y = 0; y < 32; y++)
	{
		bitmap = font_bitmap_entry->bitmap[y / 2];

		if((y & 0x01) == 0x00)
		{
			data[(y * 2) + 0] = (bitmap & 0xff000000) >> 24;
			data[(y * 2) + 1] = (bitmap & 0x00ff0000) >> 16;
		}
		else
		{
			data[(y * 2) + 0] = (bitmap & 0x0000ff00) >> 8;
			data[(y * 2) + 1] = (bitmap & 0x000000ff) >> 0;
		}
	}

	x = (column * 18) + 6;
	y = row * 32;

	if(row >= display_slot_height)
		y += 32 / 2;

	return(display_blit(0, x, y, 16, 32, data));
}

static bool attr_result_used display_write_glyphs_16x32(bool ucs2, unsigned int length, const uint8_t *text)
{
	unsigned int charlength;
	unsigned int codepoint;
	unsigned int entry;

	if(display_render_mode() != render_mode_graphic)
		return(false);

	charlength = ucs2 ? length / 2 : length;

	for(entry = 0; entry < charlength; entry++)
	{
		if(ucs2)
			codepoint = ((text[(entry * 2) + 0] & 0xff) << 8) | ((text[(entry * 2) + 1] & 0xff) << 0);
		else
			if(codepoint > 0xff)
				codepoint = ' ';
			else
				codepoint = text[entry];

		if(!display_write_glyph(entry, display_y + 4, codepoint))
			return(false);
	}

	return(true);
}

bool display_eastrising_init(void)
{
	const font_bitmap_t *font_bitmap;
	uint8_t cmd = reg_mrwc;
	unsigned int mode, fontchip;
	int user_cs_io, user_cs_pin;

	if(!config_get_uint("display_eastrising.mode", &mode, -1, -1))
		display_mode = display_mode_disabled;
	else
		display_mode = (display_mode_t)mode;

	if(!config_get_uint("display_eastrising.fontchip", &fontchip, -1, -1))
		display_use_fontchip = false;
	else
		display_use_fontchip = fontchip != 0;

	if(!config_get_int("display_eastrising.io", &user_cs_io, -1, -1) ||
			!config_get_int("display_eastrising.pin", &user_cs_pin, -1, -1))
	{
		display_user_cs_io = -1;
		display_user_cs_pin = -1;
	}
	else
	{
		display_user_cs_io = user_cs_io;
		display_user_cs_pin = user_cs_pin;
	}

	switch(display_mode)
	{
		case(display_mode_i2c):
		{
			break;
		}

		case(display_mode_spi):
		{
			if(!spi_send_receive(spi_clock_1M, spi_mode_0, false, display_user_cs_io, display_user_cs_pin,
					true, spi_rs_cmd | spi_rw_write, 1, &cmd, 0, 0, (uint8_t *)0, (string_t *)0))
				goto error;

			break;
		}

		default:
		{
			goto error;
		}
	}

	// note: this will always use either mirror 0 or mirror 1 depending on which image/slot is loaded, due to the flash mapping window
	font_bitmap = (const font_bitmap_t *)(display_flash_memory_map_start + FONT_FLASH_OFFSET_0);

	if((font_bitmap->magic == font_magic) && (font_bitmap->version == font_version))
		display_font_valid = true;

	// init PLL

	if(display_mode == display_mode_i2c)
		set_i2c_speed(10000);

	if(!display_write_command(reg_pll_c1))
		goto error;

	display_write_data(reg_pllc1_value);

	msleep(1);

	if(!display_write_command(reg_pll_c2))
		goto error;

	display_write_data(reg_pllc2_value);

	msleep(1);

	if(display_mode == display_mode_i2c)
		set_i2c_speed(-1);

	if(!display_write(reg_sysr, reg_sysr_color_depth_16 | reg_sysr_if_8bit))
		goto error;

	if(!display_write(reg_pcsr, reg_pcsr_sample_falling_edge | reg_pcsr_clock_period_system_by_8))
		goto error;

	// horizontal

	if(!display_write(reg_hdwr, (display_width / 8) - 1))
		goto error;

	if(!display_write(reg_hndftr, reg_hndftr_de_polarity_active_high | (display_horizontal_blanking_fine / 2)))
		goto error;

	if(!display_write(reg_hndr, (display_horizontal_blanking / 8) - 1))
		goto error;

	if(!display_write(reg_hstr, (display_horizontal_sync_start / 8) - 1))
		goto error;

	if(!display_write(reg_hpwr, reg_hpwr_hsync_polarity_active_low | ((display_horizontal_sync_length / 8) - 1)))
		goto error;

	// vertical

	if(!display_write(reg_vdhr0, ((display_height >> 0) & 0xff) + 1))
		goto error;

	if(!display_write(reg_vdhr1, ((display_height >> 8) & 0x01) + 0))
		goto error;

	if(!display_write(reg_vndr0, ((display_vertical_blanking >> 0) & 0xff) + 1))
		goto error;

	if(!display_write(reg_vndr1, ((display_vertical_blanking >> 8) & 0x01) + 0))
		goto error;

	if(!display_write(reg_vstr0, ((display_vertical_sync_start >> 0) & 0xff) + 1))
		goto error;

	if(!display_write(reg_vstr1, ((display_vertical_sync_start >> 8) & 0x01) + 0))
		goto error;

	if(!display_write(reg_vpwr, reg_vpwr_vsync_polarity_active_low | (display_vertical_sync_length - 1)))
		goto error;

	// PWM

	if(!display_write(reg_p1cr, reg_p1cr_pwm1_enable | reg_p1cr_function_pwm1 | reg_p1cr_clock_ratio_2048)) // 114 Hz refresh
		goto error;

	if(!display_write(reg_pwrr, reg_pwrr_display_enable | reg_pwrr_display_sleep_mode_disable | reg_pwrr_display_reset_complete))
		goto error;

	// MISC

	if(!display_write(reg_sfclr, reg_sfclr_by_1))
		goto error;

	if(!display_write(reg_sroc, reg_sroc_if_select_1 | reg_sroc_addr_mode_24 | reg_sroc_spi_mode_0 | reg_sroc_mode_6bus | reg_sroc_access_font | reg_sroc_data_mode_dual_0))
		goto error;

	if(!display_write(reg_serfont, reg_serfont_type_gt30l32s4w | reg_serfont_enc_gb2312 | reg_serfont_font_alternative_2))
		goto error;

	if(!display_write(reg_dpcr, reg_dpcr_two_layer | reg_dpcr_hor_scan_ltor | reg_dpcr_vert_scan_ltor))
		goto error;

	if(!display_show_layer(0))
		goto error;

	if(!display_fill_box(0, 0, 0, display_width, display_height, 0x00, 0x00, 0x00))
		goto error;

	if(!display_fill_box(1, 0, 0, display_width, display_height, 0x90, 0xa0, 0x90))
		goto error;

	if(!display_eastrising_bright(1))
		goto error;

	return(true);

error:
	if(display_mode == display_mode_i2c)
		set_i2c_speed(-1);
	display_mode = display_mode_disabled;
	return(false);
}

bool display_eastrising_begin(unsigned int slot, bool logmode)
{
	unsigned int fncr0, fncr1, fncr2, mwcr0;

	switch(display_mode)
	{
		case(display_mode_i2c):
		case(display_mode_spi):
			break;

		default:
		{
			log("display eastrising not inited\n");
			return(false);
		}
	}

	display_current_slot = slot;
	display_logmode = logmode;

	display_data_clear();

	fncr0 = reg_fncr0_font_cgrom;
	fncr1 = reg_fncr1_font_align_enable | reg_fncr1_font_straight;
	fncr2 = display_character_width_padding;
	mwcr0 = reg_mwcr0_default;

	switch(display_render_mode())
	{
		case(render_mode_internal):
		{
			fncr0 |= reg_fncr0_font_internal | reg_fncr0_encoding_8859_1;

			if(display_logmode)
				fncr1 |= reg_fncr1_font_opaque | reg_fncr1_font_enlarge_hor_x1 | reg_fncr1_font_enlarge_ver_x1;
			else
				fncr1 |= reg_fncr1_font_transparent | reg_fncr1_font_enlarge_hor_x2 | reg_fncr1_font_enlarge_ver_x2;

			fncr2 |= reg_fncr2_font_size_16x16;
			mwcr0 |= reg_mwcr0_mode_text;

			break;
		}

		case(render_mode_external):
		{
			fncr0 |= reg_fncr0_font_external;
			fncr1 |= reg_fncr1_font_transparent | reg_fncr1_font_enlarge_hor_x1 | reg_fncr1_font_enlarge_ver_x1;
			fncr2 |= reg_fncr2_font_size_32x32;
			mwcr0 |= reg_mwcr0_mode_text;

			break;
		}

		case(render_mode_graphic):
		{
			fncr0 |= 0;
			fncr1 |= 0;
			fncr2 |= 0;
			mwcr0 |= reg_mwcr0_mode_graphic;

			break;
		}
	}

	if(!display_write(reg_fncr0, fncr0))
		return(false);

	if(!display_write(reg_fncr1, fncr1))
		return(false);

	if(!display_write(reg_fncr2, fncr2))
		return(false);

	if(!display_write(reg_mwcr0, mwcr0))
		return(false);

	if(!text_goto_line(0))
		return(false);

	if(!display_logmode)
	{
		unsigned int x0 = 0;
		unsigned int y0 = (display_slot_height * display_character_height) + display_character_slot_padding;
		unsigned int x1 = 0;
		unsigned int y1 = 0;
		unsigned int width = display_width;
		unsigned int height = display_slot_height * display_character_height;

		if(!display_scroll(x0, y0, x1, y1, width, height))
			return(false);

		x0 = 0;
		y0 = (display_slot_height * display_character_height) + 0;
		x1 = display_width;
		y1 = (display_slot_height * display_character_height) + display_character_slot_padding;

		if(!display_fill_box(0, x0, y0, x1, y1, 0x00, 0x00, 0x00))
			return(false);
	}

	return(true);
}

typedef enum
{
	map_none,
	map_transparent,
	map_font_internal,
	map_font_external
} map_t;

bool display_eastrising_output(unsigned int unicode)
{
	const unicode_map_t *unicode_map_ptr = (unicode_map_t *)0;
	bool mapped = false;

	if(unicode == '\n')
		return(display_newline());

	if((display_y < text_height()) && (display_x < text_width()))
	{
		switch(display_render_mode())
		{
			case(render_mode_internal):	unicode_map_ptr = unicode_map_internal_font; break;
			case(render_mode_external):	unicode_map_ptr = unicode_map_external_font_chip; break;
			case(render_mode_graphic):	unicode_map_ptr = (unicode_map_t *)0; break;
		}

		for(; unicode_map_ptr && (unicode_map_ptr->unicode != mapeof); unicode_map_ptr++)
			if(unicode_map_ptr->unicode == unicode)
			{
				unicode = unicode_map_ptr->internal;
				mapped = true;
				break;
			}

		switch(display_render_mode())
		{
			case(render_mode_internal):
			{
				if(mapped)
					display_data_output(unicode);
				else
					if((unicode < ' ') || ((unicode > '}') && (unicode < 0xa1)) || (unicode > 0xff))
						display_data_output(' ');
					else
						display_data_output(unicode);

				break;
			}

			case(render_mode_external):
			{
				if(mapped)
				{
					if((unicode & 0xff00) != 0x0000)
						display_data_output((unicode & 0xff00) >> 8);

					display_data_output((unicode & 0x00ff) >> 0);
				}
				else
					if((unicode < ' ') || ((unicode > '}') && (unicode < 0xa1)) || (unicode > 0xff))
						display_data_output(' ');
					else
						display_data_output((unicode & 0x00ff) >> 0);

				break;
			}

			case(render_mode_graphic):
			{
				display_data_output((unicode & 0xff00) >> 8);
				display_data_output((unicode & 0x00ff) >> 0);

				break;
			}
		}

		display_x++;
	}

	return(true);
}

bool display_eastrising_end(void)
{
	while(display_y < text_height())
		if(!display_newline())
			break;

	display_newline();

	return(true);
}

bool display_eastrising_bright(int brightness)
{
	roflash static const unsigned int bright_level[5] = { 0, 5, 20, 110, 255 };
	roflash static const unsigned int bright_power[5] = { reg_pwrr_display_disable, reg_pwrr_display_enable, reg_pwrr_display_enable, reg_pwrr_display_enable, reg_pwrr_display_enable };
	roflash static const unsigned int bright_low[5] = { 0, 1, 1, 0, 0 };

	if(brightness > 4)
		return(false);

	if(!display_write(reg_p1dcr, bright_level[brightness]))
		return(false);

	if(!display_write(reg_pwrr, bright_power[brightness] | reg_pwrr_display_sleep_mode_disable | reg_pwrr_display_reset_complete))
		return(false);

	display_low_brightness = bright_low[brightness] ? true : false;

	return(true);
}

typedef struct
{
	unsigned int r, g, b;
} rgb_t;

typedef struct
{
	rgb_t fg, bg;
} rgb_fg_bg_t;

typedef struct
{
	rgb_fg_bg_t standout;
	rgb_fg_bg_t normal;
} colours_t;

typedef struct
{
	colours_t low_bright;
	colours_t normal_bright;
} theme_t;

roflash static const theme_t themes[8] =
{
	{ // slot 0	theme grey
		{ // low_bright
			{ // standout
				{	0xff, 0xff, 0xff	},	// fg
				{	0x28, 0x28, 0x28	},	// bg
			},
			{ // normal
				{	0xff, 0xff, 0xff	},	// fg
				{	0x18, 0x18, 0x18	},	// bg
			}
		},
		{ // normal_bright
			{ // standout
				{	0x00, 0x00, 0x00	},	// fg
				{	0xc0, 0xd0, 0xc0	},	// bg
			},
			{ // normal
				{	0x00, 0x00, 0x00	},	// fg
				{	0xff, 0xff, 0xff	},	// bg
			}
		}
	},
	{ // slot 1 theme blue
		{ // low bright
			{ // standout
				{	0xff, 0xff, 0xff	},	// fg
				{	0x00, 0x00, 0x50	},	// bg
			},
			{ // normal
				{	0xff, 0xff, 0xff	},	// fg
				{	0x00, 0x00, 0x30	},	// bg
			}
		},
		{ // normal bright
			{ // standout
				{	0x00, 0x00, 0x00	},	// fg
				{	0x40, 0xa9, 0xff	},	// bg
			},
			{ // normal
				{	0x00, 0x00, 0x00	},	// fg
				{	0x90, 0xb4, 0xff	},	// bg
			}
		}
	},
	{ // slot 2 theme red
		{ // low bright
			{ // standout
				{	0xff, 0xff, 0xff	},	// fg
				{	0x50, 0x00, 0x00	},	// bg
			},
			{ // normal
				{	0xff, 0xff, 0xff	},	// fg
				{	0x30, 0x00, 0x00	},	// bg
			}
		},
		{ // normal bright
			{ // standout
				{	0xff, 0xff, 0xff	},	// fg
				{	0xff, 0x50, 0x20	},	// bg
			},
			{ // normal
				{	0x00, 0x00, 0x00	},	// fg
				{	0xff, 0x90, 0x60	},	// bg
			}
		}
	},
	{ // slot 3 theme green
		{ // low bright
			{ // standout
				{	0xff, 0xff, 0xff	},	// fg
				{	0x00, 0x30, 0x00	},	// bg
			},
			{ // normal
				{	0xff, 0xff, 0xff	},	// fg
				{	0x00, 0x20, 0x00	},	// bg
			}
		},
		{ // normal bright
			{ // standout
				{	0x00, 0x00, 0x00	},	// fg
				{	0x40, 0xff, 0x00	},	// bg
			},
			{ // normal
				{	0x00, 0x00, 0x00	},	// fg
				{	0x70, 0xff, 0x40	},	// bg
			}
		}
	},
	{ // slot 4 theme brown
		{ // low bright
			{ // standout
				{	0xff, 0xff, 0xff	},	// fg
				{	0x40, 0x30, 0x20	},	// bg
			},
			{ // normal
				{	0xff, 0xff, 0xff	},	// fg
				{	0x30, 0x20, 0x10	},	// bg
			}
		},
		{ // normal bright
			{ // standout
				{	0x00, 0x00, 0x00	},	// fg
				{	0xff, 0x80, 0x30	},	// bg
			},
			{ // normal
				{	0x00, 0x00, 0x00	},	// fg
				{	0xff, 0xa0, 0x50	},	// bg
			}
		}
	},
	{ // slot 5 theme cyan
		{ // low bright
			{ // standout
				{	0xff, 0xff, 0xff	},	// fg
				{	0x00, 0x30, 0x30	},	// bg
			},
			{ // normal
				{	0xff, 0xff, 0xff	},	// fg
				{	0x00, 0x20, 0x20	},	// bg
			}
		},
		{ // normal bright
			{ // standout
				{	0x00, 0x00, 0x00	},	// fg
				{	0x00, 0xff, 0xff	},	// bg
			},
			{ // normal
				{	0x00, 0x00, 0x00	},	// fg
				{	0x90, 0xff, 0xff	},	// bg
			}
		}
	},
	{ // slot 6 theme yellow
		{ // low bright
			{ // standout
				{	0xff, 0xff, 0xff	},	// fg
				{	0x40, 0x30, 0x00	},	// bg
			},
			{ // normal
				{	0xff, 0xff, 0xff	},	// fg
				{	0x30, 0x20, 0x00	},	// bg
			}
		},
		{ // normal bright
			{ // standout
				{	0x00, 0x00, 0x00	},	// fg
				{	0xff, 0xff, 0x00	},	// bg
			},
			{ // normal
				{	0x00, 0x00, 0x00	},	// fg
				{	0xff, 0xff, 0x50	},	// bg
			}
		}
	},
	{ // slot 7 theme purple
		{ // low bright
			{ // standout
				{	0xff, 0xff, 0xff	},	// fg
				{	0x50, 0x00, 0x40	},	// bg
			},
			{ // normal
				{	0xff, 0xff, 0xff	},	// fg
				{	0x30, 0x00, 0x20	},	// bg
			}
		},
		{ // normal bright
			{ // standout
				{	0xff, 0xff, 0xff	},	// fg
				{	0xff, 0x50, 0xff	},	// bg
			},
			{ // normal
				{	0x00, 0x00, 0x00	},	// fg
				{	0xff, 0xb0, 0xff	},	// bg
			}
		}
	}
};

bool display_eastrising_standout(bool standout)
{
	unsigned int y0, y1;
	const colours_t *colours;
	const rgb_t *fg, *bg;
	roflash static const rgb_t colour_black = { 0x00, 0x00, 0x00 };
	roflash static const rgb_t colour_white = { 0xff, 0xff, 0xff };

	switch(display_mode)
	{
		case(display_mode_i2c):
		case(display_mode_spi):
			break;

		default:
		{
			return(false);
		}
	}

	if(display_logmode)
	{
		fg = &colour_white;
		bg = &colour_black;
	}
	else
	{
		if(display_y >= display_slot_height)
			return(true);

		if((display_current_slot < 0))
		{
			fg = &colour_white;
			bg = &colour_black;
		}
		else
		{
			if(display_low_brightness)
				colours = &themes[display_current_slot].low_bright;
			else
				colours = &themes[display_current_slot].normal_bright;

			if(standout)
			{
				fg = &colours->standout.fg;
				bg = &colours->standout.bg;
			}
			else
			{
				fg = &colours->normal.fg;
				bg = &colours->normal.bg;
			}
		}

		if(display_y == 0)
		{
			y0 = display_text_to_graphic_y(0);
			y1 = display_text_to_graphic_y(1) + 2;
		}
		else
		{
			y0 = display_text_to_graphic_y(display_y) + 2;
			y1 = display_text_to_graphic_y(4) + 0;
		}

		if(!display_fill_box(0, 0, y0, display_width, y1, bg->r, bg->g, bg->b))
			return(false);
	}

	if(!display_fgcolour_set(fg->r, fg->g, fg->b))
		return(false);

	if(!display_bgcolour_set(bg->r, bg->g, bg->b))
		return(false);

	return(true);
}

#if IMAGE_OTA == 0
bool display_eastrising_periodic(void)
{
	return(true);
}
#else
bool display_eastrising_periodic(void)
{
	static const char ppm_header[] = "P6\n480 272\n255\n";
	bool success = false;
	uint8_t mwcr0;

	switch(picture_load_state)
	{
		case(pls_idle):
		{
			return(true);
		}

		case(pls_start):
		{
			if(string_size(&flash_sector_buffer) < SPI_FLASH_SEC_SIZE)
			{
				logf("display eastrising: load picture: sector buffer too small: %u\n", flash_sector_buffer_use);
				goto error1;
			}

			if((flash_sector_buffer_use != fsb_free) && (flash_sector_buffer_use != fsb_config_cache))
				return(true); // buffer currently in use, try again later

			picture_load_flash_sector = (picture_load_index ? PICTURE_FLASH_OFFSET_1 : PICTURE_FLASH_OFFSET_0) / SPI_FLASH_SEC_SIZE;
			flash_sector_buffer_use = fsb_display_picture;

			if(spi_flash_read(picture_load_flash_sector * SPI_FLASH_SEC_SIZE, string_buffer_nonconst(&flash_sector_buffer), SPI_FLASH_SEC_SIZE) != SPI_FLASH_RESULT_OK)
			{
				logf("display eastrising: load picture: failed to read first sector: 0x%x\n", picture_load_flash_sector);
				goto error2;
			}

			string_setlength(&flash_sector_buffer, sizeof(ppm_header) - 1);

			if(!string_match_cstr(&flash_sector_buffer, ppm_header))
			{
				logf("display eastrising: show picture: invalid image header: %s\n", string_to_cstr(&flash_sector_buffer));
				success = true;
				goto error2;
			}

			string_setlength(&flash_sector_buffer, SPI_FLASH_SEC_SIZE);

			picture_load_sector_offset = sizeof(ppm_header) - 1;
			picture_load_current = 0;
			picture_load_state = pls_in_progress;

			if(!display_write(reg_curh0, 0))
				goto error2;

			if(!display_write(reg_curh1, 0))
				goto error2;

			if(!display_write(reg_curv0, 0))
				goto error2;

			if(!display_write(reg_curv1, 0))
				goto error2;

			break;
		}

		case(pls_in_progress):
		{
			static const unsigned int picture_ppm_data_length = display_width * display_height * 3;
			unsigned int chunk_length, chunk_offset;
			unsigned int output_buffer_offset;
			unsigned int rgb_offset;
			unsigned int rgb[3];
			uint8_t *sector_buffer = (uint8_t *)string_buffer_nonconst(&flash_sector_buffer);

			if(picture_load_current >= picture_ppm_data_length)
			{
				success = true;
				goto error2;
			}

			if((flash_sector_buffer_use != fsb_free) && (flash_sector_buffer_use != fsb_config_cache) && (flash_sector_buffer_use != fsb_display_picture))
				return(true); // buffer currently in use, try again later

			if(flash_sector_buffer_use != fsb_display_picture)
			{
				flash_sector_buffer_use = fsb_display_picture;

				if(spi_flash_read(picture_load_flash_sector * SPI_FLASH_SEC_SIZE, sector_buffer, SPI_FLASH_SEC_SIZE) != SPI_FLASH_RESULT_OK)
				{
					logf("display eastrising: show picture: failed to re-read sector: 0x%x\n", picture_load_flash_sector);
					goto error2;
				}
			}

			chunk_length = umin(picture_ppm_data_length - picture_load_current, 1024 /*sizeof(flash_dram_buffer)*/ / 4);
			output_buffer_offset = 0;

			for(chunk_offset = 0; chunk_offset < chunk_length; chunk_offset++)
			{
				for(rgb_offset = 0; rgb_offset < 3; rgb_offset++)
				{
					if(picture_load_sector_offset >= SPI_FLASH_SEC_SIZE)
					{
						picture_load_flash_sector++;

						if(spi_flash_read(picture_load_flash_sector * SPI_FLASH_SEC_SIZE, sector_buffer, SPI_FLASH_SEC_SIZE) != SPI_FLASH_RESULT_OK)
						{
							logf("display eastrising: show picture: failed to read sector: 0x%x\n", picture_load_flash_sector);
							goto error2;
						}

						picture_load_sector_offset = 0;
					}

					rgb[rgb_offset] = sector_buffer[picture_load_sector_offset++];
					picture_load_current++;
				}

				if((output_buffer_offset + 2) >= 1024 /*sizeof(flash_dram_buffer)*/)
				{
					log("display eastrising: show picture: flash_dram_buffer overflow\n");
					goto error2;
				}

				flash_dram_buffer[output_buffer_offset++] = ((rgb[0] & 0xf8) << 0) | ((rgb[1] & 0xe0) >> 5);
				flash_dram_buffer[output_buffer_offset++] = ((rgb[1] & 0x1c) << 3) | ((rgb[2] & 0xf8) >> 3);
			}

			if(!display_read(reg_mwcr0, &mwcr0))
				goto error2;

			if(!display_write(reg_mwcr0, reg_mwcr0_mode_graphic | reg_mwcr0_default))
				goto error3;

			if(!display_set_active_layer(1))
				goto error3;

			if(!display_write_string(true, output_buffer_offset, (uint8_t *)flash_dram_buffer))
				goto error3;

			if(!display_write(reg_mwcr0, mwcr0))
				goto error2;

			if(!display_set_active_layer(0))
				goto error2;

			dispatch_post_task(2, task_display_update, 0);

			break;
		}
	}

	return(true);

error3:
	display_write(reg_mwcr0, mwcr0);
error2:
	flash_sector_buffer_use = fsb_free;
error1:
	picture_load_state = pls_idle;

	return(success);
}
#endif

bool display_eastrising_layer_select(unsigned int layer)
{
	if(layer > 1)
	{
		if(!display_fill_box(1, 0, 0, display_width, display_height, (layer >> 16) & 0xff, (layer >> 8) & 0xff, (layer >> 0) & 0xff))
			return(false);

		return(display_show_layer(1));
	}

	return(display_show_layer(layer));
}

bool display_eastrising_picture_load(unsigned int entry)
{
	if(entry > 1)
		return(false);

	if(picture_load_state != pls_idle)
		return(false);

	picture_load_state = pls_start;
	picture_load_index = entry;

	return(true);
}

extern roflash const char help_description_display_eastrising[];

app_action_t application_function_display_eastrising(string_t *src, string_t *dst)
{
	unsigned int mode, fontchip;
	int user_cs_io, user_cs_pin;

	if(parse_uint(1, src, &mode, 0, ' ') == parse_ok)
	{
		if(mode > 2)
		{
			string_append_cstr_flash(dst, help_description_display_eastrising);
			return(app_action_error);
		}

		if(parse_uint(2, src, &fontchip, 0, ' ') != parse_ok)
		{
			fontchip = 0;
			user_cs_io = -1;
			user_cs_pin = -1;
		}
		else
		{
			if((parse_int(3, src, &user_cs_io, 0, ' ') == parse_ok) && (parse_int(4, src, &user_cs_pin, 0, ' ') == parse_ok))
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
		}

		if(!config_open_write())
			goto config_error;

		if(mode == 0)
		{
			config_delete("display_eastrising.mode", false, -1, -1);
			config_delete("display_eastrising.fontchip", false, -1, -1);
			config_delete("display_eastrising.io", false, -1, -1);
			config_delete("display_eastrising.pin", false, -1, -1);
		}
		else
		{
			if(!config_set_uint("display_eastrising.mode", mode, -1, -1))
				goto config_error;

			if(fontchip == 0)
				config_delete("display_eastrising.fontchip", false, -1, -1);
			else
				if(!config_set_uint("display_eastrising.fontchip", fontchip, -1, -1))
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

	if(!config_get_uint("display_eastrising.fontchip", &fontchip, -1, -1))
		fontchip = 0;

	display_use_fontchip = fontchip != 0;

	if(!config_get_int("display_eastrising.io", &user_cs_io, -1, -1) ||
			!config_get_int("display_eastrising.pin", &user_cs_pin, -1, -1))
		user_cs_io = user_cs_pin = -1;

	switch(mode)
	{
		case(display_mode_disabled): string_append(dst, "> mode 0 (disabled)"); break;
		case(display_mode_i2c): string_append(dst, "> mode 1 (i2c)"); break;
		case(display_mode_spi):
		{
			string_append(dst, "> mode 2 (spi), ");

			if((user_cs_io >= 0) && (user_cs_pin >= 0))
				string_format(dst, "user cs pin: %d/%d", user_cs_io, user_cs_pin);
			else
				string_append(dst, "default cs pin");

			break;
		}

		default: string_append(dst, "> unknown mode"); break;
	}

	string_format(dst, "\n> font chip is enabled: %s\n", yesno(fontchip));

	return(app_action_normal);

config_error:
	config_abort_write();
	string_clear(dst);
	string_append(dst, "> cannot set config\n");
	return(app_action_error);
}
