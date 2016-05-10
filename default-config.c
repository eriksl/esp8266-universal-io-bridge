#include <stdint.h>

#include "config.h"

#define STRINGIFY_1(x) #x
#define STRINGIFY(x) STRINGIFY_1(x)

const config_t default_config_data =
{
	.magic = config_magic,
	.major_version = config_major_version,
	.minor_version = config_minor_version,
	.ssid = STRINGIFY(CONFIG_SSID),
	.passwd = STRINGIFY(CONFIG_PASSWD),
	.flags = 0,
	.uart.parity = parity_none,
	.uart.stop_bits = 1,
	.uart.data_bits = 8,
	.uart.baud_rate = 115200,
	.tcp_port.bridge = 23,
	.tcp_port.command = 24,
	.ntp_server.addr = 0,
	.ntp_timezone = 0,
	.display_default_msg = "%%%%",
	.status_trigger_io = { -1, -1 },
};
