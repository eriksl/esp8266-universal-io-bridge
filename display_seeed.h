#ifndef display_seeed_h
#define display_seeed_h

#include <stdint.h>
#include <stdbool.h>

bool display_seeed_init(void);
void display_seeed_begin(unsigned int slot);
void display_seeed_output(unsigned int);
void display_seeed_end(void);
bool display_seeed_bright(int);
bool display_seeed_inverse(bool);

#endif
