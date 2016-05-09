#ifndef display_h
#define display_h

#include "util.h"
#include "application.h"

#include <stdint.h>

void display_init(void);
bool display_periodic(void);

app_action_t application_function_display_brightness(const string_t *src, string_t *dst);
app_action_t application_function_display_dump(const string_t *src, string_t *dst);
app_action_t application_function_display_default_message(const string_t *src, string_t *dst);
app_action_t application_function_display_set(const string_t *src, string_t *dst);
#endif
