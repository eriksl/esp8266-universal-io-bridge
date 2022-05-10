#ifndef application_h
#define application_h

#include "app_action.h"

#include <stdint.h>

void			application_init(void);
app_action_t	application_content(string_t *src, string_t *dst);
#endif
