#ifndef __ota_h__
#define __ota_h__

#ifndef __espif__
#include "util.h"
#include "dispatch.h"
#include "attribute.h"
#endif

#include <stdint.h>

enum
{
	packet_header_soh = 0x01,
	packet_header_version = 2,
	packet_header_id = 0x4afb,
};

typedef struct attr_packed
{
	uint8_t soh;						// 0
	uint8_t version;					// 1
	uint16_t id;						// 2
	uint16_t length;					// 4
	uint16_t data_offset;				// 6
	uint16_t data_pad_offset;			// 8
	uint16_t oob_data_offset;			// 10
	uint16_t broadcast_groups;			// 12
	union
	{
		struct attr_packed
		{
			unsigned int md5_32_requested:1;
			unsigned int md5_32_provided:1;
			unsigned int transaction_id_provided:1;
			unsigned int spare_3:1;
			unsigned int spare_4:1;
			unsigned int spare_5:1;
			unsigned int spare_6:1;
			unsigned int spare_7:1;
			unsigned int spare_8:1;
			unsigned int spare_9:1;
			unsigned int spare_10:1;
			unsigned int spare_11:1;
			unsigned int spare_12:1;
			unsigned int spare_13:1;
			unsigned int spare_14:1;
			unsigned int spare_15:1;
		} flag;							// 14
		uint16_t flags;					// 14
	};
	uint32_t transaction_id;			// 16
	uint32_t spare_0;					// 20
	uint32_t spare_1;					// 24
	uint32_t checksum;					// 28
} packet_header_t;

assert_field(packet_header_t, soh, 0);
assert_field(packet_header_t, version, 1);
assert_field(packet_header_t, id, 2);
assert_field(packet_header_t, length, 4);
assert_field(packet_header_t, data_offset, 6);
assert_field(packet_header_t, data_pad_offset, 8);
assert_field(packet_header_t, oob_data_offset, 10);
assert_field(packet_header_t, broadcast_groups, 12);
assert_field(packet_header_t, flag, 14);
assert_field(packet_header_t, flags, 14);
assert_field(packet_header_t, transaction_id, 16);
assert_field(packet_header_t, spare_0, 20);
assert_field(packet_header_t, spare_1, 24);
assert_field(packet_header_t, checksum, 28);
assert_size(packet_header_t, 32);

#ifndef __espif__
app_action_t application_function_flash_info(app_params_t *);
app_action_t application_function_flash_write(app_params_t *);
app_action_t application_function_flash_read(app_params_t *);
app_action_t application_function_flash_checksum(app_params_t *);
app_action_t application_function_flash_bench(app_params_t *);
app_action_t application_function_flash_select(app_params_t *);
#endif

#endif
