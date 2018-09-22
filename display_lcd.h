#ifndef display_lcd_h
#define display_lcd_h

_Bool display_lcd_init(void);
_Bool display_lcd_bright(int brightness);
_Bool display_lcd_set(const char *tag, const char *text);
_Bool display_lcd_show(void);

#endif
