#ifndef ota_h
#define ota_h

#include "util.h"
#include "application.h"

app_action_t application_function_ota_read(const string_t *, string_t *);
app_action_t application_function_ota_write(const string_t *, string_t *);
app_action_t application_function_ota_write_dummy(const string_t *, string_t *);
app_action_t application_function_ota_send(const string_t *, string_t *);
app_action_t application_function_ota_receive(const string_t *, string_t *);
app_action_t application_function_ota_finish(const string_t *, string_t *);
app_action_t application_function_ota_commit(const string_t *, string_t *);
#endif
