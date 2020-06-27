#ifndef display_saa_h
#define display_saa_h

#include <stdint.h>
#include <stdbool.h>

bool display_saa1064_init(void);
bool display_saa1064_bright(int brightness);
bool display_saa1064_begin(unsigned int slot, bool logmode);
bool display_saa1064_output(unsigned int);
bool display_saa1064_end(void);

#endif
