#include "rboot-interface.h"

const rboot_if_config_t config =
{
	.magic			= rboot_if_conf_magic,
	.version		= rboot_if_conf_version,
	.boot_mode		= rboot_if_conf_mode_standard,
	.slot_current	= 0,
	.slot_count		= 2,
	.slots			= { 0x02000, 0x102000, 0x00000, 0x00000 }
};
