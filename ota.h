#ifndef ota_h
#define ota_h

#include "util.h"
#include "application.h"

app_action_t application_function_flash_info(const string_t *, string_t *);
app_action_t application_function_flash_erase(const string_t *, string_t *);
app_action_t application_function_flash_send(const string_t *, string_t *);
app_action_t application_function_flash_receive(const string_t *, string_t *);
app_action_t application_function_flash_write(const string_t *, string_t *);
app_action_t application_function_flash_read(const string_t *, string_t *);
app_action_t application_function_flash_verify(const string_t *, string_t *);
app_action_t application_function_flash_checksum(const string_t *, string_t *);
app_action_t application_function_flash_select(const string_t *, string_t *);
app_action_t application_function_flash_select_once(const string_t *, string_t *);
#endif
