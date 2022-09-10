#include "ota.h"
#include "util.h"
#include "config.h"
#include "rboot-interface.h"
#include "display.h"
#include "sdk.h"

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

static app_action_t flash_all_finish(string_t *dst,
		SpiFlashOpResult result, unsigned int sector,
		const char *tag, const char *action)
{
	const char *cause = (const char *)0;

	if(result == SPI_FLASH_RESULT_OK)
		cause = (const char *)0;
	else
		if(result == SPI_FLASH_RESULT_ERR)
			cause = "error";
		else
			if(result == SPI_FLASH_RESULT_TIMEOUT)
				cause = "timeout";
			else
				cause = "unknown";

	if(cause)
	{
		string_clear(dst);
		string_format(dst, "ERROR: %s: %s flash %s at 0x%x\n",
				tag, action, cause, sector);

		return(app_action_error);
	}

	return(app_action_normal);
}

static app_action_t flash_read(unsigned int sector, void *dst, const char *tag, string_t *str_dst)
{
	SpiFlashOpResult result;

	result = spi_flash_read(sector * SPI_FLASH_SEC_SIZE, dst, SPI_FLASH_SEC_SIZE);

	return(flash_all_finish(str_dst, result, sector, tag, "read"));
}

static app_action_t flash_write(unsigned int sector, const void *src, const char *tag, string_t *str_dst)
{
	SpiFlashOpResult result;

	result = spi_flash_write(sector * SPI_FLASH_SEC_SIZE, src, SPI_FLASH_SEC_SIZE);

	return(flash_all_finish(str_dst, result, sector, tag, "write"));
}

static app_action_t flash_erase(unsigned int sector, const char *tag, string_t *str_dst)
{
	SpiFlashOpResult result;

	result = spi_flash_erase_sector(sector);

	return(flash_all_finish(str_dst, result, sector, tag, "erase"));
}

app_action_t application_function_flash_info(app_params_t *parameters)
{
	unsigned int ota_slots = 0;
	unsigned int ota_slot = 0;
	unsigned int ota_address_0 = 0;
	unsigned int ota_address_1 = 0;
	rboot_if_config_t config;
	rboot_if_rtc_config_t rtc;
	unsigned int x, y;
	display_pixel_mode_t pixel_mode;

	display_info_t info;

	if(display_get_info(&info))
	{
		x = info.width;
		y = info.height;
		pixel_mode = info.pixel_mode;
	}
	else
	{
		x = 0;
		y = 0;
		pixel_mode = display_pixel_mode_none;
	}

	if(!rboot_if_read_config(&config))
	{
		string_append(parameters->dst, "ERROR rboot config invalid\n");
		return(app_action_error);
	}

	ota_slots =		config.slot_count;
	ota_slot =		config.slot_current;
	ota_address_0 = config.slots[0];
	ota_address_1 = config.slots[1];

	if(rboot_if_read_rtc_ram(&rtc))
		ota_slot = rtc.last_slot;

	string_format(parameters->dst, "OK flash function available, "
				"slots: %u, "
				"current: %u, "
				"sectors: [ %u, %u ], "
				"display: %ux%upx@%u\n",
			ota_slots,
			ota_slot,
			ota_address_0 / SPI_FLASH_SEC_SIZE, ota_address_1 / SPI_FLASH_SEC_SIZE,
			x, y, pixel_mode);

	return(app_action_normal);
}

app_action_t application_function_flash_read(app_params_t *parameters)
{
	unsigned int sector, pad_offset, oob_offset;
	uint8_t *sector_dst;
	app_action_t rv;

	if(string_size(parameters->dst) < (int)(SPI_FLASH_SEC_SIZE + 64))
	{
		string_format(parameters->dst, "ERROR flash-read: flash sector buffer too small: %d\n", string_size(parameters->dst));
		return(app_action_error);
	}

	if(parse_uint(1, parameters->src, &sector, 0, ' ') != parse_ok)
	{
		string_append(parameters->dst, "ERROR flash-read: requesting sector required\n");
		return(app_action_error);
	}

	string_format(parameters->dst, "OK flash-read: read sector %u\n", sector);
	oob_offset = pad_offset = string_length(parameters->dst);
	sector_dst = (uint8_t *)string_buffer_nonconst(parameters->dst) + pad_offset;

	while((oob_offset % 4 != 0))
	{
		*sector_dst++ = '\0';
		oob_offset++;
	}

	if((rv = flash_read(sector, sector_dst, "flash read", parameters->dst)) != app_action_normal)
		return(rv);

	string_setlength(parameters->dst, oob_offset + SPI_FLASH_SEC_SIZE);

	parameters->dst_data_pad_offset = pad_offset;
	parameters->dst_data_oob_offset = oob_offset;

	return(app_action_normal);
}

app_action_t application_function_flash_write(app_params_t *parameters)
{
	unsigned int mode;
	unsigned int sector;
	unsigned int word;
	const unsigned int *wordptr;
	int same;
	int erase;
	uint8_t *old;
	const uint8_t *new;
	app_action_t rv;

	if(string_size(parameters->dst) < SPI_FLASH_SEC_SIZE)
	{
		string_format(parameters->dst, "ERROR flash-write: dst buffer too small: %d\n", string_size(parameters->dst));
		return(app_action_error);
	}

	if(parse_uint(1, parameters->src, &mode, 0, ' ') != parse_ok)
	{
		string_append(parameters->dst, "ERROR flash-write: mode required (0 = simulate, 1 = write)\n");
		return(app_action_error);
	}

	if(parse_uint(2, parameters->src, &sector, 0, ' ') != parse_ok)
	{
		string_append(parameters->dst, "ERROR flash-write: sector required\n");
		return(app_action_error);
	}

	if(string_length(parameters->src_oob) != SPI_FLASH_SEC_SIZE)
	{
		string_format(parameters->dst, "ERROR flash-write: flash sector data length mismatch: %d != %d\n", string_length(parameters->src_oob), SPI_FLASH_SEC_SIZE);
		return(app_action_error);
	}

	old = (uint8_t *)string_buffer_nonconst(parameters->dst);
	new = (const uint8_t *)string_buffer(parameters->src_oob);

	if((rv = flash_read(sector, old, "flash_write", parameters->dst)) != app_action_normal)
		return(rv);

	same = !memory_compare(SPI_FLASH_SEC_SIZE, old, new);
	erase = 0;

	if(!same)
	{
		for(word = 0, wordptr = (const unsigned int *)(const void *)old; word < (SPI_FLASH_SEC_SIZE / 4); word++, wordptr++)
		{
			if(*wordptr != 0xffffffffUL)
			{
				erase = 1;
				break;
			}
		}

		if(mode == 1)
		{
			if(erase && ((rv = flash_erase(sector, "flash_write", parameters->dst)) != app_action_normal))
				return(rv);

			if((rv = flash_write(sector, new, "flash_write", parameters->dst)) != app_action_normal)
				return(rv);
		}
	}

	string_clear(parameters->dst);
	string_format(parameters->dst, "OK flash-write: written mode %u, sector %u, same %d, erased %d\n", mode, sector, same, erase);

	return(app_action_normal);
}

app_action_t application_function_flash_checksum(app_params_t *parameters)
{
	app_action_t result;
	unsigned int sector, sectors, current, done;
	char *buffer;
	SHA_CTX sha_context;
	unsigned char sha_result[SHA_DIGEST_LENGTH];
	string_new(, sha_string, SHA_DIGEST_LENGTH * 2 + 2);

	if(string_size(parameters->dst) < SPI_FLASH_SEC_SIZE)
	{
		string_format(parameters->dst, "ERROR flash-verify: dst buffer too small: %d\n", string_size(parameters->dst));
		return(app_action_error);
	}

	if(parse_uint(1, parameters->src, &sector, 0, ' ') != parse_ok)
	{
		string_append(parameters->dst, "ERROR flash-checksum: start sector required\n");
		return(app_action_error);
	}

	if(parse_uint(2, parameters->src, &sectors, 0, ' ') != parse_ok)
	{
		string_append(parameters->dst, "ERROR flash-checksum: length (sectors) required\n");
		return(app_action_error);
	}

	SHA1Init(&sha_context);

	buffer = string_buffer_nonconst(parameters->dst);

	for(current = sector, done = 0; done < sectors; current++, done++)
	{
		if((result = flash_read(current, buffer, "flash_checksum", parameters->dst)) != app_action_normal)
			return(result);

		SHA1Update(&sha_context, buffer, SPI_FLASH_SEC_SIZE);
	}

	SHA1Final(sha_result, &sha_context);
	string_bin_to_hex(&sha_string, sha_result, SHA_DIGEST_LENGTH);

	string_clear(parameters->dst);
	string_format(parameters->dst, "OK flash-checksum: checksummed %u sectors from sector %u, checksum: ", done, sector);
	string_append_string(parameters->dst, &sha_string);
	string_append(parameters->dst, "\n");

	return(app_action_normal);
}

app_action_t application_function_flash_bench(app_params_t *parameters)
{
	unsigned int bytes, pad_offset, oob_offset;
	uint8_t *sector_dst;

	if(string_size(parameters->dst) < SPI_FLASH_SEC_SIZE)
	{
		string_format(parameters->dst, "ERROR flash-bench: dst buffer too small: %d\n", string_size(parameters->dst));
		return(app_action_error);
	}

	if(parse_uint(1, parameters->src, &bytes, 0, ' ') != parse_ok)
	{
		string_append(parameters->dst, "ERROR flash-bench: length (bytes) required\n");
		return(app_action_error);
	}

	if(bytes > SPI_FLASH_SEC_SIZE)
	{
		string_append(parameters->dst, "ERROR flash-bench: length should be <= 4096\n");
		return(app_action_error);
	}

	string_format(parameters->dst, "OK flash-bench: sending %u bytes\n", bytes);
	oob_offset = pad_offset = string_length(parameters->dst);
	sector_dst = (uint8_t *)string_buffer_nonconst(parameters->dst) + pad_offset;

	while((oob_offset % 4) != 0)
	{
		*sector_dst++ = '\0';
		oob_offset++;
	}

	memset(sector_dst, 0xff, bytes);
	string_setlength(parameters->dst, oob_offset + bytes);

	parameters->dst_data_pad_offset = pad_offset;
	parameters->dst_data_oob_offset = oob_offset;

	return(app_action_normal);
}

app_action_t application_function_flash_select(app_params_t *parameters)
{
	unsigned int permanent, slot;

	rboot_if_config_t config;
	rboot_if_rtc_config_t rtc;

	if(!rboot_if_read_config(&config))
	{
		string_append(parameters->dst, "ERROR flash-select: rboot config invalid\n");
		return(app_action_error);
	}

	if(parse_uint(1, parameters->src, &slot, 0, ' ') != parse_ok)
	{
		string_append(parameters->dst, "ERROR flash-select: slot required\n");
		return(app_action_error);
	}

	if(slot >= config.slot_count)
	{
		string_format(parameters->dst, "ERROR flash-select: invalid slot, valid range = 0 - %d\n", config.slot_count - 1);
		return(app_action_error);
	}

	if(((parse_uint(2, parameters->src, &permanent, 0, ' ') != parse_ok)) || ((permanent != 0) && (permanent != 1)))
	{
		string_format(parameters->dst, "ERROR flash-select: mode temporary (=0) or permanent (=1) required, supplied: %u\n", permanent);
		return(app_action_error);
	}

	if(!rboot_if_read_rtc_ram(&rtc))
	{
		rtc.magic = rboot_if_rtc_magic;
		rtc.next_mode = rboot_if_conf_mode_standard;
		rtc.last_mode = rboot_if_conf_mode_standard;
		rtc.last_slot = config.slot_current;
		rtc.temporary_slot=	slot;

		if(!rboot_if_write_rtc_ram(&rtc))
		{
			string_append(parameters->dst, "ERROR: flash-select: RTC RAM config signature absent and can't create a new one\n");
			return(app_action_error);
		}
	}

	if(!rboot_if_read_rtc_ram(&rtc))
	{
		string_append(parameters->dst, "ERROR: flash-select: write initial data to RTC RAM failed\n");
		return(app_action_error);
	}

	if(rboot_if_mapped_slot() != rtc.last_slot)
	{
		string_format(parameters->dst, "ERROR flash-select: current slot according to rboot RTC RAM info does not match flash memory map: %u vs. %u\n",
				config.slot_current, rboot_if_mapped_slot());
		return(app_action_error);
	}

	rtc.next_mode = permanent ? rboot_if_conf_mode_standard : rboot_if_conf_mode_temp_rom;
	rtc.temporary_slot = slot;

	if(!rboot_if_write_rtc_ram(&rtc))
	{
		string_append(parameters->dst, "ERROR: flash-select: write data to RTC RAM failed\n");
		return(app_action_error);
	}

	if(!rboot_if_read_rtc_ram(&rtc))
	{
		string_append(parameters->dst, "ERROR: flash-select: verify RTC data failed\n");
		return(app_action_error);
	}

	if(rtc.next_mode != (permanent ? rboot_if_conf_mode_standard : rboot_if_conf_mode_temp_rom))
	{
		string_format(parameters->dst, "ERROR: flash-select: RTC data invalid, next boot mode: %s\n", rboot_if_boot_mode(rtc.next_mode));
		return(app_action_error);
	}

	if(rtc.temporary_slot != slot)
	{
		string_format(parameters->dst, "ERROR: flash-select: RTC data invalid, next boot slot: %x\n", rtc.temporary_slot);
		return(app_action_error);
	}

	slot = rtc.temporary_slot;

	if(permanent)
	{
		bool success;

		if(!rboot_if_read_config(&config))
		{
			string_append(parameters->dst, "ERROR flash-select: rboot config invalid\n");
			return(app_action_error);
		}

		config.slot_current = slot;

		string_clear(parameters->dst);
		success = rboot_if_write_config(&config);
		string_clear(parameters->dst);

		if(!success)
		{
			string_append(parameters->dst, "ERROR flash-select: update rboot config failed\n");
			return(app_action_error);
		}

		if(!rboot_if_read_config(&config))
		{
			string_append(parameters->dst, "ERROR flash-select: rboot config invalid after update\n");
			return(app_action_error);
		}

		if(config.slot_current != slot)
		{
			string_append(parameters->dst, "ERROR flash-select: slot not selected\n");
			return(app_action_error);
		}

		slot = config.slot_current;
	}

	string_format(parameters->dst, "OK flash-select: slot %u selected, sector %u, permanent %u\n",
			slot, config.slots[slot] / SPI_FLASH_SEC_SIZE, permanent);

	return(app_action_normal);
}
