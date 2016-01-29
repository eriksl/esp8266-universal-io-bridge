#ifndef ota_h
#define ota_h

#include "util.h"
#include "application.h"

bool ota_active(void);

app_action_t application_function_ota_write(string_t *, string_t *);
app_action_t application_function_ota_verify(string_t *, string_t *);
app_action_t application_function_ota_send(string_t *, string_t *);
app_action_t application_function_ota_finish(string_t *, string_t *);
app_action_t application_function_ota_commit(string_t *, string_t *);
#endif
