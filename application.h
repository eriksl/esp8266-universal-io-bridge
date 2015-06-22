#ifndef application_h
#define application_h


#include <stdint.h>

void application_init(void);
void application_periodic(void);
uint8_t application_content(const char *src, uint16_t size, char *dst);

#endif
