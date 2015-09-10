#ifndef application_h
#define application_h

#include "config.h"

#include <stdint.h>

app_action_t application_content(const char *src, unsigned int size, char *dst);

#endif
