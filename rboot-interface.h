#ifndef rboot_interface_h
#define rboot_interface_h

#include "attribute.h"
#include "util.h"

#include <stdint.h>
#include <stdbool.h>

// Make sure these entries exactly mirror the ones in rboot/*.h

enum
{
	rboot_if_conf_magic =			0xe1,
	rboot_if_conf_version =			0x01,
	rboot_if_conf_mode_standard =	0x00,
	rboot_if_conf_mode_unused_1 =	0x01,
	rboot_if_conf_mode_temp_rom =	0x02,
	rboot_if_conf_mode_unused_2 =	0x04,
	rboot_if_conf_mode_unused_3 =	0x08,
	rboot_if_rtc_address		=	0x40,
	rboot_if_rtc_checksum_init	=	0xef,
	rboot_if_rtc_magic			=	0x2334ae68,
};

typedef struct
{
	uint8_t		magic;			//	this should be rboot_if_conf_magic
	uint8_t		version;		//	this should be rboot_if_conf_version
	uint8_t		boot_mode;		//	this should be either rboot_if_conf_mode_standard or rboot_if_conf_mode_temp_rom
	uint8_t		slot_current;	//	current active slot, will be used for next boot
	uint8_t		unused_0;
	uint8_t		slot_count;		//	amount of slots, currently always four for compatibility, although only two are used
	uint8_t		unused_1;
	uint8_t		unused_2;
	uint32_t	slots[4];		//	addresses in flash where slots are loaded
	uint8_t		padding[40];	//	padding
} rboot_if_config_t;

assert_size(rboot_if_config_t, 64);

typedef struct
{
	uint32_t	magic;			//	this should be rboot_if_rtc_magic
	uint8_t		next_mode;		//	use rboot_if_conf_mode_standard or rboot_if_conf_mode_temp_rom
	uint8_t		last_mode;		//	reflects last boot mode
	uint8_t		last_slot;		//	reflects last boot slot
	uint8_t		temporary_slot;	//	the next slot to boot when next_mode == rboot_if_conf_mode_temp_rom
	uint8_t		checksum;
} rboot_if_rtc_config_t;

assert_size(rboot_if_rtc_config_t, 12);

const char *	rboot_if_boot_mode(unsigned int index);
bool			rboot_if_read_config(rboot_if_config_t *config);
bool			rboot_if_write_config(const rboot_if_config_t *config, string_t *buffer);
bool			rboot_if_read_rtc_ram(rboot_if_rtc_config_t *config);
bool			rboot_if_write_rtc_ram(rboot_if_rtc_config_t *config);
void			rboot_if_info(string_t *dst);
unsigned int	rboot_if_mapped_slot(void);
#endif
