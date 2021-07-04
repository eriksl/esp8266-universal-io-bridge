#ifndef ota_h
#define ota_h

#include "util.h"
#include "application.h"

app_action_t application_function_flash_info(string_t *, string_t *);
app_action_t application_function_flash_erase(string_t *, string_t *);
app_action_t application_function_flash_send(string_t *, string_t *);
app_action_t application_function_flash_receive(string_t *, string_t *);
app_action_t application_function_flash_write(string_t *, string_t *);
app_action_t application_function_flash_read(string_t *, string_t *);
app_action_t application_function_flash_verify(string_t *, string_t *);
app_action_t application_function_flash_checksum(string_t *, string_t *);
app_action_t application_function_flash_select(string_t *, string_t *);
app_action_t application_function_flash_select_once(string_t *, string_t *);

void ota_init(unsigned int mailbox_port);

app_action_t application_function_mailbox_info(string_t *, string_t *);
app_action_t application_function_mailbox_reset(string_t *, string_t *);
app_action_t application_function_mailbox_read(string_t *, string_t *);
app_action_t application_function_mailbox_bench(string_t *, string_t *);
app_action_t application_function_mailbox_checksum(string_t *, string_t *);
app_action_t application_function_mailbox_simulate(string_t *, string_t *);
app_action_t application_function_mailbox_write(string_t *, string_t *);
app_action_t application_function_mailbox_select(string_t *, string_t *);
#endif
