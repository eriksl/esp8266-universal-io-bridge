#ifndef display_orbital_h
#define display_orbital_h

#include <stdint.h>
#include <stdbool.h>

bool display_orbital_init(void);
bool display_orbital_bright(int);
void display_orbital_begin(void);
void display_orbital_output(unsigned int);
void display_orbital_end(void);

#endif
