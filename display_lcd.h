#ifndef display_lcd_h
#define display_lcd_h

#include <stdint.h>
#include <stdbool.h>

bool display_lcd_init(void);
bool display_lcd_bright(int);
bool display_lcd_begin(unsigned int slot, bool logmode);
bool display_lcd_output(unsigned int);
bool display_lcd_end(void);
bool display_lcd_picture_load(unsigned int);
bool display_lcd_layer_select(unsigned int);
bool display_lcd_start_show_time(unsigned int hour, unsigned int minute);
bool display_lcd_stop_show_time(void);

#endif
