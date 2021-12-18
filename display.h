#ifndef display_h
#define display_h

#include "util.h"
#include "application.h"

#include <stdint.h>
#include <stdbool.h>

void display_init(void);
void display_periodic(void);
bool display_detected(void);

enum
{
	display_buffer_size = 64,
};

extern uint8_t display_buffer[display_buffer_size];

typedef const struct
{
	const char *const name;
	const struct
	{
		unsigned int columns;
		unsigned int rows;
	} text_dimensions;
	const struct
	{
		unsigned int x;
		unsigned int y;
	} graphic_dimensions;
	const unsigned int colour_depth;
} display_properties_t;

assert_size(display_properties_t, 24);

typedef const struct
{
	bool (* const init_fn)(void);
	bool (* const begin_fn)(unsigned int slot, bool logmode);
	bool (* const output_fn)(unsigned int amount, const unsigned int unicode[]);
	bool (* const end_fn)(void);
	bool (* const bright_fn)(int brightness);
	bool (* const standout_fn)(bool);
	bool (* const show_time_start_fn)(unsigned int, unsigned int);
	bool (* const show_time_stop_fn)(void);
	bool (* const plot_fn)(unsigned int pixel_amount, int x, int y, string_t *pixels);
	bool (* const freeze_fn)(bool active);
} display_hooks_t;

assert_size(display_hooks_t, 40);

typedef const struct
{
	const display_properties_t properties;
	const display_hooks_t hooks;
} display_info_t;

assert_size(display_info_t, 64);

const display_properties_t *display_get_properties(void);
bool						display_load_picture_slot(unsigned int slot);

app_action_t application_function_display_brightness(string_t *src, string_t *dst);
app_action_t application_function_display_dump(string_t *src, string_t *dst);
app_action_t application_function_display_default_message(string_t *src, string_t *dst);
app_action_t application_function_display_flip_timeout(string_t *src, string_t *dst);
app_action_t application_function_display_set(string_t *src, string_t *dst);
app_action_t application_function_display_picture_load(string_t *src, string_t *dst);
app_action_t application_function_display_plot(string_t *src, string_t *dst);
app_action_t application_function_display_freeze(string_t *src, string_t *dst);
#endif
