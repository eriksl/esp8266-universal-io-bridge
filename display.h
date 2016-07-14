#ifndef display_h
#define display_h

#include "util.h"
#include "application.h"

#include <stdint.h>

enum
{
	display_common_buffer_rows = 4,
	display_common_buffer_columns = 20,
	display_common_udg_amount = 8,
	display_common_udg_byte_amount = 8,
	display_common_map_amount = 15,
};

typedef struct
{
	uint16_t utf16;
	uint8_t pattern[display_common_udg_byte_amount];
} display_common_udg_t;

typedef struct
{
	struct
	{
		unsigned int dirty:1;
	} row[display_common_buffer_rows];
} display_common_row_status_t;

extern const display_common_udg_t display_common_udg[display_common_udg_amount];

extern display_common_row_status_t display_common_row_status;
extern uint8_t display_common_buffer[display_common_buffer_rows][display_common_buffer_columns];

void display_init(void);
bool display_periodic(void);

bool_t display_common_set(const char *tag, const char *text);

app_action_t application_function_display_brightness(const string_t *src, string_t *dst);
app_action_t application_function_display_dump(const string_t *src, string_t *dst);
app_action_t application_function_display_default_message(const string_t *src, string_t *dst);
app_action_t application_function_display_flip_timeout(const string_t *src, string_t *dst);
app_action_t application_function_display_set(const string_t *src, string_t *dst);
#endif
