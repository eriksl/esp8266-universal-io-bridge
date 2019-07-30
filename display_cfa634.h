#ifndef display_cfa634_h
#define display_cfa634_h

#include <stdint.h>
#include <stdbool.h>

bool display_cfa634_setup(unsigned int io, unsigned int pin);
bool display_cfa634_init(void);
void display_cfa634_begin(int slot, unsigned int slot_offset);
void display_cfa634_output(unsigned int);
void display_cfa634_end(void);
bool display_cfa634_bright(int brightness);

#endif
