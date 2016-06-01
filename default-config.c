#include <stdint.h>

#include "config.h"

#define STRINGIFY_1(x) #x
#define STRINGIFY(x) STRINGIFY_1(x)

const config_t default_config_data =
{
	.magic = config_magic,
	.version = config_version,
	.flags = 1 << config_flag_print_debug,
	.uart.stop_bits = 1,
	.uart.parity = parity_none,
	.uart.data_bits = 8,
	.uart.baud_rate = 115200,
};
