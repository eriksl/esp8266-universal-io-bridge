#ifndef __i2s_h__
#define __i2s_h__

#include <stdint.h>
#include <stdbool.h>

bool i2s_init(void);
bool i2s_send(unsigned int length, const uint8_t *data);
void i2s_flush(void);

#endif
