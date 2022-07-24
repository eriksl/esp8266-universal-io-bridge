#ifndef _remote_trigger_h_
#define _remote_trigger_h_

#include <stdbool.h>

#include "dispatch.h"

bool remote_trigger_init(void);
app_action_t application_function_trigger_remote(app_params_t *);
bool remote_trigger_add(unsigned int index, unsigned int io, unsigned int pin, io_trigger_t);
void remote_trigger_send(unsigned int argument);

#endif
