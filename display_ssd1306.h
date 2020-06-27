#ifndef display_ssd1306_h
#define display_ssd1306_h

#include <stdint.h>
#include <stdbool.h>

bool display_ssd1306_init(void);
bool display_ssd1306_begin(unsigned int slot, bool logmode);
bool display_ssd1306_output(unsigned int);
bool display_ssd1306_end(void);
bool display_ssd1306_bright(int);
bool display_ssd1306_standout(bool);
bool display_ssd1306_picture_load(unsigned int);
bool display_ssd1306_layer_select(unsigned int);

#endif
