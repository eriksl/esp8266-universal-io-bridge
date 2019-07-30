#ifndef display_lcd_h
#define display_lcd_h

#include <stdint.h>
#include <stdbool.h>

bool display_lcd_init(void);
bool display_lcd_bright(int);
void display_lcd_begin(int slot, unsigned int slot_offset);
void display_lcd_output(unsigned int);
void display_lcd_end(void);


#endif
