#include "font.h"
#include "util.h"
#include "sys_string.h"
#include "sys_time.h"
#include "stats.h"
#include "application.h"
#include "config.h"

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

enum
{
	font_magic_1 = 0x4afb4afb,
	font_magic_2 = 0x4afb4afc,
	font_version_0 = 0,
	font_version_1 = 1,
	font_codepoint_last_entry = 0xffffffff,
	fonts = 16,
};

typedef struct
{
	uint32_t	magic2;
	uint32_t	version;
	uint32_t	width;
	uint32_t	height;
	uint32_t	font_bitmap[];	// start with a unicode utf8 codepoint, with bit 31 set to one, then one entry (4 bytes) per line.
} font_t;

typedef struct
{
	uint32_t	magic1;
	uint32_t	version;
	uint32_t	font[16];	// this is not a real pointer but an offset from the start of the font area
} font_root_t;

assert_size(font_root_t, 72);

_Static_assert(offsetof(font_root_t, magic1) == 0, "offsetof(font_root_t, magic1) != 0)");
_Static_assert(offsetof(font_root_t, version) == 4, "offsetof(font_root_t, version) != 4)");
_Static_assert(offsetof(font_root_t, font[0]) == 8, "offsetof(font_root_t, font[0]) != 8)");
_Static_assert(offsetof(font_root_t, font[1]) == 12, "offsetof(font_root_t, font[1]) != 12)");
_Static_assert(offsetof(font_root_t, font[2]) == 16, "offsetof(font_root_t, font[2]) != 16)");
_Static_assert(offsetof(font_root_t, font[3]) == 20, "offsetof(font_root_t, font[3]) != 20)");
_Static_assert(offsetof(font_root_t, font[4]) == 24, "offsetof(font_root_t, font[4]) != 24)");
_Static_assert(offsetof(font_root_t, font[5]) == 28, "offsetof(font_root_t, font[5]) != 28)");
_Static_assert(offsetof(font_root_t, font[6]) == 32, "offsetof(font_root_t, font[6]) != 32)");

static bool init_done = false;
static const font_t *font[fonts];
unsigned int font_logging;
unsigned int font_other;
unsigned int font_selected;

void font_init(void)
{
	const font_root_t *font_root = flash_cache_pointer(FONT_FLASH_OFFSET_0);

	if((font_root->magic1 != font_magic_1) ||
		(font_root->version != font_version_1))
	{
		log("font root invalid\n");
		return;
	}

	for(unsigned int ix = 0; ix < fonts; ix++)
	{
		font[ix] = flash_cache_pointer(FONT_FLASH_OFFSET_0 + font_root->font[ix]);

		if((font[ix]->magic2 != font_magic_2) ||
				(font[ix]->version != font_version_1))
			font[ix] = (font_t *)0;
	}

	if(!config_get_uint("font.id.logging", &font_logging, -1, -1))
		font_logging = 0;

	if(!config_get_uint("font.id.other", &font_other, -1, -1))
		font_other = 0;

	init_done = true;
}

bool font_select(bool logging)
{
	unsigned int font_new;

	font_new = logging ? font_logging : font_other;

	if(font_new > fonts)
		return(false);

	if(!font[font_new])
		return(false);

	font_selected = font_new;

	return(true);
}

bool font_get_info(font_info_t *info)
{
	if((font_selected >= fonts) || (!font[font_selected]))
		return(false);

	info->width = font[font_selected]->width;
	info->height = font[font_selected]->height;

	return(true);
}

bool font_render(unsigned int code, font_cell_t cell)
{
	unsigned int sync_brake;
	unsigned int font_code;
	unsigned int width, height;
	unsigned int x, y;
	const uint32_t *current;
	uint64_t start, spent;

	start = time_get_us();

	if((font_selected >= fonts) || (!font[font_selected]))
		return(false);

	width = font[font_selected]->width;
	height = font[font_selected]->height;

	if((width > font_cell_width) || (height > font_cell_height))
	{
		log("font: invalid width/height\n");
		return(false);
	}

	current = &font[font_selected]->font_bitmap[0];

	for(;;)
	{
		for(sync_brake = 0; sync_brake < 64; sync_brake++, current++)
			if(*current & (1U << 31))
				break;
			else
				log("skip %04x %08x\n", (unsigned int)((current - (const uint32_t *)flash_cache_pointer(FONT_FLASH_OFFSET_0)) * 4), *current);

		if(sync_brake >= 64)
		{
			log("font_render: sync brake\n");
			return(false);
		}

		if(font_code == font_codepoint_last_entry)
		{
			log("font_render: code not found (1)\n");
			return(false);
		}

		font_code = *current & ~(1U << 31);

		if(font_code > code)
		{
			log("font_render: code not found (2)\n");
			return(false);
		}

		if(font_code == code)
			break;

		current += height + 1;
	}

	current++;

	for(y = 0; y < height; y++)
		for(x = 0; x < width; x++)
			cell[y][x] = current[y] & (1 << x) ? ~0 : 0;

	spent = time_get_us() - start;

	if(spent > stat_font_render_time)
		stat_font_render_time = spent;

	return(true);
}

roflash const char help_description_display_font_select[] = "select display font <0-2>\n";

app_action_t application_function_display_font_select(string_t *src, string_t *dst)
{
	unsigned int font_logging_local, font_other_local, ix;

	if((parse_uint(1, src, &font_logging_local, 0, ' ') == parse_ok) && parse_uint(2, src, &font_other_local, 0, ' ') == parse_ok)
	{
		if((font_logging_local >= fonts) || (font_other_local >= fonts))
		{
			string_format(dst, "> font must be 0 - %u\n", fonts - 1U);
			return(app_action_error);
		}

		if(!config_open_write())
		{
			string_append(dst, "> cannot set config (open)\n");
			return(app_action_error);
		}

		if((font_logging_local == 0) && (font_other_local == 0))
			config_delete("font.id", true, -1, -1);
		else
			if(!config_set_int("font.id.logging", font_logging_local, -1, -1) ||
					!config_set_int("font.id.other", font_other_local, -1, -1))
			{
				config_abort_write();
				string_append(dst, "> cannot set config\n");
				return(app_action_error);
			}

		if(!config_close_write())
		{
			string_append(dst, "> cannot set config (close)\n");
			return(app_action_error);
		}

		font_logging = font_logging_local;
		font_other = font_other_local;
	}

	if(!config_get_uint("font.id.logging", &font_logging_local, -1, -1))
		font_logging_local = 0;

	if(!config_get_uint("font.id.other", &font_other_local, -1, -1))
		font_other_local = 0;

	for(ix = 0; ix < fonts; ix++)
	{
		string_format(dst, "> font %2u: ", ix);

		if(!font[ix])
			string_append(dst, "unavailable");
		else
		{
			string_format(dst, "%2u x %2u %-7s %-5s",
				font[ix]->width, font[ix]->height,
				ix == font_logging_local ? "logging" : "",
				ix == font_other_local ? "other" : "");
		}

		string_format(dst, "\n");
	}

	return(app_action_normal);
}
