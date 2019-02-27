#include "rboot/rboot.h"

const rboot_config rboot_config_data =
{
	.magic			= BOOT_CONFIG_MAGIC,
	.version		= BOOT_CONFIG_VERSION,
	.mode			= MODE_STANDARD,
	.current_rom	= 0,
	.gpio_rom		= 0,
	.count			= 2,
	.unused			= { 0x4a, 0xfb },
	.roms			= { 0x02000, 0x102000, 0x00000, 0x00000 }
};
