#ifndef display_seeed_h
#define display_seeed_h

#include <stdint.h>
#include <stdbool.h>

bool display_seeed_init(void);
bool display_seeed_begin(int slot, bool logmode);
bool display_seeed_output(unsigned int);
bool display_seeed_end(void);
bool display_seeed_bright(int);
bool display_seeed_standout(bool);
bool display_seeed_picture_load(unsigned int);
bool display_seeed_layer_select(unsigned int);

#endif
