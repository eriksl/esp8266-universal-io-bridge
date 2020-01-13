#ifndef _font_bitmap_h_
#define _font_bitmap_h_

#define assert_size(type, size) _Static_assert(sizeof(type) == size, "sizeof(" #type ") != " #size)

#include <stdint.h>

enum
{
	font_magic = 0x4afb4afb,
	font_version = 0,
	font_codepoint_last_entry = ~0,
};

typedef struct
{
	uint32_t codepoint;
	uint32_t bitmap[16]; // each entry is 16 bits of the even line and 16 bits of the following odd line
} font_bitmap_entry_t;

assert_size(font_bitmap_entry_t, 68);

typedef struct
{
	uint32_t			magic;
	uint32_t			version;
	font_bitmap_entry_t	entries[];
} font_bitmap_t;

#endif /*_font_bitmap_h_*/
