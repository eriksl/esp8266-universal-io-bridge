#ifndef _display_font_6x8_h_
#define _display_font_6x8_h_

#include <stdint.h>

enum
{
	display_font_width = 6,
	display_font_height = 8,
	display_font_6x8_size = 256,
};

extern const uint8_t display_font_6x8[display_font_6x8_size * 8];

enum
{
	unicode_map_6x8_eof = 0xffffffff,
};

typedef struct
{
	unsigned int unicode;
	unsigned int internal;
} unicode_map_6x8_t;

extern const unicode_map_6x8_t unicode_map_6x8[];

#endif
