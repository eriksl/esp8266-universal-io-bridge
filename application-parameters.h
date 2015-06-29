#ifndef application_parameters_h
#define application_parameters_h

#include <stdint.h>

typedef enum __attribute__ ((__packed__))
{
	app_action_normal,
	app_action_error,
	app_action_empty,
	app_action_disconnect,
	app_action_reset
} app_action_t;

_Static_assert(sizeof(app_action_t) == 1, "sizeof(app_action_t) != 1");

enum
{
	application_num_args = 8,
	application_length_args = 16,
};

typedef char args_t[application_num_args][application_length_args];

typedef struct
{
	const char *cmdline;
	uint8_t nargs;
	args_t *args;
	uint16_t size;
	char *dst;
} application_parameters_t;

#endif
