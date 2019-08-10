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

app_action_t application_function_display_brightness(string_t *src, string_t *dst);
app_action_t application_function_display_dump(string_t *src, string_t *dst);
app_action_t application_function_display_default_message(string_t *src, string_t *dst);
app_action_t application_function_display_flip_timeout(string_t *src, string_t *dst);
app_action_t application_function_display_set(string_t *src, string_t *dst);
app_action_t application_function_display_picture_switch_layer(string_t *src, string_t *dst);
app_action_t application_function_display_picture_autoload(string_t *src, string_t *dst);
app_action_t application_function_display_picture_load(string_t *src, string_t *dst);
#endif
