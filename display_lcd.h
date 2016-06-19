#ifndef display_lcd_h
#define display_lcd_h

bool_t display_lcd_init(void);
bool_t display_lcd_bright(int brightness);
bool_t display_lcd_set(const char *tag, const char *text);
bool_t display_lcd_show(void);

#endif
