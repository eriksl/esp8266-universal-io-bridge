#ifndef http_h
#define http_h

#include <util.h>

typedef enum
{
	http_action_normal,
	http_action_error
} http_action_t;

typedef struct
{
	const char *location;
	http_action_t (*handler)(const string_t *src, string_t *dst);
} http_handler_t;

http_action_t http_process_request(const string_t *src, string_t *dst);

#endif
