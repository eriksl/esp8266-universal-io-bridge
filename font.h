#ifndef _font_h_
#define _font_h_

#include "sys_string.h"
#include "dispatch.h"

#include <stdbool.h>

typedef enum
{
	font_cell_height = 32,
	font_cell_width = 32,
} font_enum_t;

typedef struct
{
	unsigned int width;
	unsigned int height;
} font_info_t;

typedef uint8_t font_cell_t[font_cell_height][font_cell_width];

assert_size(font_cell_t, 1024);

void font_init(void);
bool font_select(bool logging);
bool font_get_info(font_info_t *);
bool font_render(unsigned int code, font_cell_t cell);

extern const char help_description_display_font_select[];
app_action_t application_function_display_font_select(app_params_t *);

#endif
