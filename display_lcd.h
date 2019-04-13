#ifndef display_lcd_h
#define display_lcd_h

#include <stdint.h>
#include <stdbool.h>

bool display_lcd_init(void);
bool display_lcd_bright(int brightness);
bool display_lcd_set(const char *tag, const char *text);
bool display_lcd_show(void);

#endif
