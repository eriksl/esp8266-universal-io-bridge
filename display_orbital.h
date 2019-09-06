#ifndef display_orbital_h
#define display_orbital_h

#include <stdint.h>
#include <stdbool.h>

bool display_orbital_init(void);
bool display_orbital_bright(int);
bool display_orbital_begin(int slot, bool logmode);
bool display_orbital_output(unsigned int);
bool display_orbital_end(void);
bool display_orbital_picture_load(unsigned int);
bool display_orbital_layer_select(unsigned int);

#endif
