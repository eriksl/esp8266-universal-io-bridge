#ifndef app_action_h
#define app_action_h

typedef enum
{
	app_action_normal,
	app_action_error,
	app_action_empty,
	app_action_disconnect,
	app_action_http_ok,
	app_action_reset,
} app_action_t;

assert_size(app_action_t, 4);
#endif
