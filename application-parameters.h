#ifndef application_parameters_h
#define application_parameters_h

#include <stdint.h>

enum
{
	application_num_args = 8,
	application_length_args = 16,
};

typedef uint8_t args_t[application_num_args][application_length_args];

typedef struct
{
	const char *cmdline;
	uint8_t nargs;
	args_t *args;
	uint16_t size;
	char *dst;
} application_parameters_t;

#endif
