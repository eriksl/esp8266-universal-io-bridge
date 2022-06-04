#ifndef display_cfa634_h
#define display_cfa634_h

#include "display.h"

bool display_cfa634_setup(unsigned int io, unsigned int pin);

extern const char help_description_display_cfa634[];
app_action_t application_function_display_cfa634(string_t *src, string_t *dst);

extern display_hooks_t display_hooks_cfa;

#endif
