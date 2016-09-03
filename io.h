#ifndef io_h
#define io_h

#include "util.h"
#include "config.h"
#include "io_config.h"
#include "application.h"

#include <stdint.h>

typedef enum
{
	io_ok = 0,
	io_error,
} io_error_t;

assert_size(io_error_t, 4);

void		io_init(void);
void		io_periodic(void);
io_error_t	io_read_pin(string_t *, int, int, int *);
io_error_t	io_write_pin(string_t *, int, int, int);
io_error_t	io_trigger_pin(string_t *, int, int, io_trigger_t);
void		io_config_dump(string_t *dst, const config_t *cfg, int io_id, int pin_id, bool html);
void		io_config_export(const config_t *cfg, string_t *dst);
void		io_string_from_ll_mode(string_t *, io_pin_ll_mode_t);

app_action_t application_function_io_mode(const string_t *src, string_t *dst);
app_action_t application_function_io_read(const string_t *src, string_t *dst);
app_action_t application_function_io_write(const string_t *src, string_t *dst);
app_action_t application_function_io_trigger(const string_t *src, string_t *dst);
app_action_t application_function_io_set_flag(const string_t *src, string_t *dst);
app_action_t application_function_io_clear_flag(const string_t *src, string_t *dst);

#endif
