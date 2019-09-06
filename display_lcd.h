#ifndef display_lcd_h
#define display_lcd_h

#include <stdint.h>
#include <stdbool.h>

bool display_lcd_init(void);
bool display_lcd_bright(int);
bool display_lcd_begin(int slot, bool logmode);
bool display_lcd_output(unsigned int);
bool display_lcd_end(void);
bool display_lcd_picture_load(unsigned int);
bool display_lcd_layer_select(unsigned int);

#endif
