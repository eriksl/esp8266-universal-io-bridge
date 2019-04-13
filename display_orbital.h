#ifndef display_orbital_h
#define display_orbital_h

#include <stdint.h>
#include <stdbool.h>

bool display_orbital_init(void);
bool display_orbital_bright(int brightness);
bool display_orbital_set(const char *tag, const char *text);
bool display_orbital_show(void);

#endif
