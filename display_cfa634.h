#ifndef display_cfa634_h
#define display_cfa634_h

bool_t display_cfa634_setup(unsigned int io, unsigned int pin);
bool_t display_cfa634_init(void);
bool_t display_cfa634_bright(int brightness);
bool_t display_cfa634_set(const char *tag, const char *text);
bool_t display_cfa634_show(void);

#endif
