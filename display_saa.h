#ifndef display_saa_h
#define display_saa_h

#include <stdint.h>
#include <stdbool.h>

bool display_saa1064_init(void);
bool display_saa1064_bright(int brightness);
bool display_saa1064_set(const char *tag, const char *text);

#endif
