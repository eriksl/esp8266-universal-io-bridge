#ifndef display_eastrising_h
#define display_eastrising_h

#include <stdint.h>
#include <stdbool.h>

bool display_eastrising_init(void);
bool display_eastrising_begin(unsigned int slot, bool logmode);
bool display_eastrising_output(unsigned int);
bool display_eastrising_end(void);
bool display_eastrising_bright(int);
bool display_eastrising_periodic(void);
bool display_eastrising_picture_load(unsigned int);
bool display_eastrising_picture_valid(void);
bool display_eastrising_layer_select(unsigned int);
bool display_eastrising_canvas_start(unsigned int timeout);
bool display_eastrising_canvas_goto(unsigned int x, unsigned int y);
bool display_eastrising_canvas_plot(const string_t *pixels);
bool display_eastrising_canvas_show(void);
bool display_eastrising_canvas_stop(void);

app_action_t application_function_display_eastrising(string_t *src, string_t *dst);
#endif
