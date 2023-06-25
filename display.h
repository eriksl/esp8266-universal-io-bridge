#ifndef display_h
#define display_h

#include "util.h"
#include "application.h"
#include "spi.h"

#include <stdint.h>
#include <stdbool.h>

void display_init(void);
void display_periodic(void);
bool display_detected(void);

enum
{
	display_buffer_size = 64,
};

typedef enum
{
	display_pixel_mode_none = 0,
	display_pixel_mode_1 = 1,
	display_pixel_mode_16_rgb = 16,
	display_pixel_mode_24_rgb = 24,
} display_pixel_mode_t;

typedef struct
{
	char name[32];
	unsigned int columns;
	unsigned int rows;
	unsigned int cell_width;
	unsigned int cell_height;
	unsigned int width;
	unsigned int height;
	display_pixel_mode_t pixel_mode;
} display_info_t;

typedef const struct
{
	bool (* const init_fn)(void);
	bool (* const info_fn)(display_info_t *);
	bool (* const begin_fn)(unsigned int slot, bool logmode);
	bool (* const output_fn)(unsigned int amount, const unsigned int unicode[]);
	bool (* const end_fn)(void);
	bool (* const bright_fn)(int brightness);
	bool (* const show_time_start_fn)(unsigned int, unsigned int);
	bool (* const show_time_stop_fn)(void);
	bool (* const plot_fn)(unsigned int pixel_amount, int x, int y, string_t *pixels);
	bool (* const freeze_fn)(bool active);
} display_hooks_t;

assert_size(display_hooks_t, 40);

extern uint8_t display_buffer[display_buffer_size];
bool display_get_info(display_info_t *);
bool display_load_picture_slot(unsigned int slot);

app_action_t application_function_display_brightness(app_params_t *);
app_action_t application_function_display_dump(app_params_t *);
app_action_t application_function_display_default_message(app_params_t *);
app_action_t application_function_display_flip_timeout(app_params_t *);
app_action_t application_function_display_set(app_params_t *);
app_action_t application_function_display_picture_load(app_params_t *);
app_action_t application_function_display_plot(app_params_t *);
app_action_t application_function_display_freeze(app_params_t *);
#endif
