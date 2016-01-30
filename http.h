#ifndef http_h
#define http_h

#include "util.h"
#include "application.h"

typedef struct
{
	const char *location;
	app_action_t (*handler)(const string_t *src, string_t *dst);
} http_handler_t;

app_action_t application_function_http_get(const string_t *src, string_t *dst);

#endif
