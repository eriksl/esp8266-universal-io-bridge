#ifndef display_h
#define display_h

#include "util.h"
#include "application.h"

#include <stdint.h>

enum
{
	display_common_buffer_rows = 4,
	display_common_buffer_columns = 20,
	display_common_udg_size = 8,
	display_common_udg_byte_size = 8,
	display_common_map_size = 15,
};

typedef struct
{
	uint32_t utf16;
	uint32_t to;
} display_map_t;

typedef struct
{
	uint32_t utf16;
	uint32_t pattern[display_common_udg_byte_size];
} display_udg_t;

typedef struct
{
	struct
	{
		unsigned int dirty:1;
	} row[display_common_buffer_rows];
} display_common_row_status_t;

extern roflash const display_map_t display_common_map[display_common_map_size];
extern roflash const display_udg_t display_common_udg[display_common_udg_size];

extern display_common_row_status_t display_common_row_status;
extern uint8_t display_common_buffer[display_common_buffer_rows][display_common_buffer_columns];

void display_init(void);
_Bool display_periodic(void);
_Bool display_detected(void);

_Bool display_common_set(const char *tag, const char *text,
			int map_size, const display_map_t *map,
			int udg_size, const display_udg_t *udg);

app_action_t application_function_display_brightness(string_t *src, string_t *dst);
app_action_t application_function_display_dump(string_t *src, string_t *dst);
app_action_t application_function_display_default_message(string_t *src, string_t *dst);
app_action_t application_function_display_flip_timeout(string_t *src, string_t *dst);
app_action_t application_function_display_set(string_t *src, string_t *dst);
#endif
