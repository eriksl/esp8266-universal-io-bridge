#ifndef display_cfa634_h
#define display_cfa634_h

#include <stdint.h>
#include <stdbool.h>

bool display_cfa634_setup(unsigned int io, unsigned int pin);
bool display_cfa634_init(void);
bool display_cfa634_bright(int brightness);
bool display_cfa634_set(const char *tag, const char *text);
bool display_cfa634_show(void);

#endif
