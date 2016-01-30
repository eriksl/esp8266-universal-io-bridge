#ifndef ota_h
#define ota_h

#include "util.h"
#include "application.h"

bool ota_active(void);

app_action_t application_function_ota_write(const string_t *, string_t *);
app_action_t application_function_ota_verify(const string_t *, string_t *);
app_action_t application_function_ota_send(const string_t *, string_t *);
app_action_t application_function_ota_finish(const string_t *, string_t *);
app_action_t application_function_ota_commit(const string_t *, string_t *);
#endif
