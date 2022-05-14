#ifndef display_ssd1306_h
#define display_ssd1306_h

#include "display.h"

extern const char help_description_display_ssd1306[];
extern const display_hooks_t display_hooks_ssd1306;

app_action_t application_function_display_ssd1306(string_t *src, string_t *dst);
#endif
