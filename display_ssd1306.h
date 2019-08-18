#ifndef display_ssd1306_h
#define display_ssd1306_h

#include <stdint.h>
#include <stdbool.h>

bool display_ssd1306_init(void);
void display_ssd1306_begin(int slot, bool logmode);
void display_ssd1306_output(unsigned int);
void display_ssd1306_end(void);
bool display_ssd1306_bright(int);
bool display_ssd1306_standout(bool);

#endif
