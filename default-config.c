#include <stdint.h>

#include "config.h"

#define STRINGIFY_1(x) #x
#define STRINGIFY(x) STRINGIFY_1(x)

const config_t default_config_data =
{
	.magic = config_magic,
	.major_version = config_major_version,
	.minor_version = 0,
	.ssid = STRINGIFY(CONFIG_SSID),
	.passwd = STRINGIFY(CONFIG_PASSWD),
	.uart.stop_bits = 1,
	.uart.parity = parity_none,
	.uart.data_bits = 8,
	.uart.baud_rate = 115200,
	.flags = 0,
};
