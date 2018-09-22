#ifndef display_cfa634_h
#define display_cfa634_h

_Bool display_cfa634_setup(unsigned int io, unsigned int pin);
_Bool display_cfa634_init(void);
_Bool display_cfa634_bright(int brightness);
_Bool display_cfa634_set(const char *tag, const char *text);
_Bool display_cfa634_show(void);

#endif
