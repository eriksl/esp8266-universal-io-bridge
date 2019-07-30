#ifndef display_saa_h
#define display_saa_h

#include <stdint.h>
#include <stdbool.h>

bool display_saa1064_init(void);
bool display_saa1064_bright(int brightness);
void display_saa1064_begin(int slot, unsigned int slot_offset);
void display_saa1064_output(unsigned int);
void display_saa1064_end(void);

#endif
