#ifndef mailbox_h
#define mailbox_h

#include "util.h"
#include "application.h"

extern string_t mailbox_socket_receive_buffer;
extern string_t mailbox_socket_send_buffer;

void mailbox_init(unsigned int mailbox_port);

app_action_t application_function_mailbox_info(app_params_t *);
app_action_t application_function_mailbox_reset(app_params_t *);
app_action_t application_function_mailbox_read(app_params_t *);
app_action_t application_function_mailbox_bench(app_params_t *);
app_action_t application_function_mailbox_checksum(app_params_t *);
app_action_t application_function_mailbox_simulate(app_params_t *);
app_action_t application_function_mailbox_write(app_params_t *);
app_action_t application_function_mailbox_select(app_params_t *);
#endif
