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
		unsigned int x;
		unsigned int y;
	} graphic_dimensions;
	const struct
	{
		unsigned int columns;
		unsigned int rows;
	} text_dimensions;
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
	bool (* const periodic_fn)(void);
	bool (* const picture_load_fn)(unsigned int);
	bool (* const layer_select_fn)(unsigned int);
	bool (* const show_time_start_fn)(unsigned int, unsigned int);
	bool (* const show_time_stop_fn)(void);
	bool (* const canvas_start_fn)(unsigned int timeout);
	bool (* const canvas_goto_fn)(unsigned int x, unsigned int y);
	bool (* const canvas_plot_fn)(const string_t *pixels);
	bool (* const canvas_show_fn)(void);
	bool (* const canvas_stop_fn)(void);
	bool (* const picture_valid_fn)(void);
} display_hooks_t;

assert_size(display_hooks_t, 68);

typedef const struct
{
	const display_properties_t properties;
	const display_hooks_t hooks;
} display_info_t;

assert_size(display_info_t, 92);

app_action_t application_function_display_brightness(string_t *src, string_t *dst);
app_action_t application_function_display_dump(string_t *src, string_t *dst);
app_action_t application_function_display_default_message(string_t *src, string_t *dst);
app_action_t application_function_display_flip_timeout(string_t *src, string_t *dst);
app_action_t application_function_display_set(string_t *src, string_t *dst);
app_action_t application_function_display_picture_switch_layer(string_t *src, string_t *dst);
app_action_t application_function_display_picture_autoload(string_t *src, string_t *dst);
app_action_t application_function_display_picture_load(string_t *src, string_t *dst);
app_action_t application_function_display_canvas_start(string_t *src, string_t *dst);
app_action_t application_function_display_canvas_goto(string_t *src, string_t *dst);
app_action_t application_function_display_canvas_plot(string_t *src, string_t *dst);
app_action_t application_function_display_canvas_show(string_t *src, string_t *dst);
app_action_t application_function_display_canvas_stop(string_t *src, string_t *dst);
#endif
