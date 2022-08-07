#ifndef __ota_h__
#define __ota_h__

#ifndef __espif__
#include "util.h"
#include "dispatch.h"
#endif

#include <stdint.h>

enum
{
	ota_data_offset = 32,
};

enum
{
	packet_header_soh = 0x01,
	packet_header_version = 1,
	packet_header_id1 = 0x4a,
	packet_header_id2 = 0xfb,

	packet_header_id = ((uint32_t)packet_header_id2 << 24) | ((uint32_t)packet_header_id1 << 16) | ((uint32_t)packet_header_version << 8) | ((uint32_t)packet_header_soh << 0),
	packet_header_checksum_dummy = 0xffffffff,

	packet_header_flag_md5_32_requested =	(1 << 0),
	packet_header_flag_md5_32_provided =	(1 << 1),
};

typedef struct attr_packed
{
	uint32_t	id;
	uint32_t	length;
	uint32_t	flags;
	uint32_t	checksum;
} packet_header_t;

_Static_assert(offsetof(packet_header_t, id) == 0);
_Static_assert(offsetof(packet_header_t, length) == 4);
_Static_assert(offsetof(packet_header_t, flags) == 8);
_Static_assert(offsetof(packet_header_t, checksum) == 12);
assert_size(packet_header_t, 16);

#ifndef __espif__
app_action_t application_function_flash_info(app_params_t *);
app_action_t application_function_flash_write(app_params_t *);
app_action_t application_function_flash_read(app_params_t *);
app_action_t application_function_flash_checksum(app_params_t *);
app_action_t application_function_flash_bench(app_params_t *);
app_action_t application_function_flash_select(app_params_t *);
#endif

#endif
