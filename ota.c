#include "ota.h"
#include "util.h"
#include "config.h"
#include "dispatch.h"
#include "rboot-interface.h"

#include <spi_flash.h>
#include <user_interface.h>
#include <stdint.h>
#include <stdlib.h>

irom app_action_t application_function_flash_info(string_t *src, string_t *dst)
{
	int ota_available = 0;
	int ota_slots = 0;
	int ota_slot = 0;
	int ota_address_0 = 0;
	int ota_address_1 = 0;
	int ota_address_2 = 0;
	int ota_address_3 = 0;

#if IMAGE_OTA == 1
	rboot_if_config_t		config;
	rboot_if_rtc_config_t	rtc;

	if(!rboot_if_read_config(&config))
	{
		string_append(dst, "ERROR rboot config invalid\n");
		return(app_action_error);
	}

	ota_available =	1;
	ota_slots =		config.slot_count;
	ota_slot =		config.slot_current;
	ota_address_0 = config.slots[0];
	ota_address_1 = config.slots[1];
	ota_address_2 = config.slots[2];
	ota_address_3 = config.slots[3];

	if(rboot_if_read_rtc_ram(&rtc))
		ota_slot = rtc.last_slot;
#endif

	string_format(dst, "OK flash function available, "
				"sector size: %u bytes, "
				"OTA update available: %d, "
				"slots: %d, slot: %d, "
				"address: %u, address: %u, address: %u, address: %u, "
				"preferred chunk size: %u"
				"\n",
			SPI_FLASH_SEC_SIZE,
			ota_available,
			ota_slots, ota_slot,
			ota_address_0, ota_address_1, ota_address_2, ota_address_3,
			4096);

	return(app_action_normal);
}

irom app_action_t application_function_flash_erase(string_t *src, string_t *dst)
{
	unsigned int address, length;
	int sector_offset, sector_count, erased;
	uint32_t time_start, time_finish;

	if(string_size(&flash_sector_buffer) < SPI_FLASH_SEC_SIZE)
	{
		string_format(dst, "ERROR flash-erase: flash sector buffer too small: %u\n", string_size(&flash_sector_buffer));
		return(app_action_error);
	}

	if(parse_uint(1, src, &address, 0, ' ') != parse_ok)
	{
		string_append(dst, "ERROR flash-erase: offset required\n");
		return(app_action_error);
	}

	if(parse_uint(2, src, &length, 0, ' ') != parse_ok)
	{
		string_append(dst, "ERROR flash-erase: length required\n");
		return(app_action_error);
	}

	sector_offset = address / SPI_FLASH_SEC_SIZE;
	sector_count = length / SPI_FLASH_SEC_SIZE;

	if((address % SPI_FLASH_SEC_SIZE) != 0)
	{
		sector_offset--;
		sector_count++;
	}

	if((length % SPI_FLASH_SEC_SIZE) != 0)
		sector_count++;

	time_start = system_get_time();

	for(erased = 0; erased < sector_count; erased++)
	{
		system_soft_wdt_feed();
		if(spi_flash_erase_sector(sector_offset + erased) != SPI_FLASH_RESULT_OK)
			break;
	}

	time_finish = system_get_time();

	string_format(dst, "OK flash-erase: erased %d sectors from sector %d, in %d milliseconds\n", erased - 1, sector_offset, (time_finish - time_start) / 1000);

	return(app_action_normal);
}

irom app_action_t application_function_flash_send(string_t *raw_src, string_t *dst)
{
	string_t src = *raw_src;
	int chunk_offset;
	unsigned int offset, length, chunk_length;

	if(string_size(&flash_sector_buffer) < SPI_FLASH_SEC_SIZE)
	{
		string_format(dst, "ERROR flash-send: flash sector buffer too small: %u\n", string_size(&flash_sector_buffer));
		return(app_action_error);
	}

	if(parse_uint(1, &src, &offset, 0, ' ') != parse_ok)
	{
		string_append(dst, "ERROR flash-send: offset required\n");
		return(app_action_error);
	}

	if(parse_uint(2, &src, &length, 0, ' ') != parse_ok)
	{
		string_append(dst, "ERROR flash-send: length required\n");
		return(app_action_error);
	}

	if((length != 0) && ((offset % length) != 0))
	{
		string_append(dst, "ERROR: flash-send: chunk offset should be divisible by chunk size");
		return(app_action_error);
	}

	if((length != 0) && ((SPI_FLASH_SEC_SIZE % length) != 0))
	{
		string_append(dst, "ERROR: flash-send: chunk length should be divisible by flash sector size");
		return(app_action_error);
	}

	if((offset + length) > SPI_FLASH_SEC_SIZE)
	{
		string_format(dst, "ERROR flash-send: length(%d) + offset(%d) > sector size(%d)\n", offset, length, SPI_FLASH_SEC_SIZE);
		return(app_action_error);
	}

	if((chunk_offset = string_sep(&src, 0, 3, ' ')) < 0)
	{
		string_append(dst, "ERROR flash-send: missing data\n");
		return(app_action_error);
	}

	if((chunk_length = string_length(&src) - chunk_offset) != length)
	{
		string_format(dst, "ERROR flash-send: data length mismatch: %d != %d\n", length, chunk_length);
		return(app_action_error);
	}

	string_splice(&flash_sector_buffer, offset, &src, chunk_offset, chunk_length);

	string_format(dst, "OK flash-send: received bytes: %d, at offset: %d\n", length, offset);

	return(app_action_normal);
}

irom app_action_t application_function_flash_receive(string_t *src, string_t *dst)
{
	unsigned int chunk_offset, chunk_length;

	if(string_size(&flash_sector_buffer) < SPI_FLASH_SEC_SIZE)
	{
		string_format(dst, "ERROR flash-receive: flash sector buffer too small: %u\n", string_size(&flash_sector_buffer));
		return(app_action_error);
	}

	if(parse_uint(1, src, &chunk_offset, 0, ' ') != parse_ok)
	{
		string_append(dst, "ERROR flash-receive: chunk offset required\n");
		return(app_action_error);
	}

	if(parse_uint(2, src, &chunk_length, 0, ' ') != parse_ok)
	{
		string_append(dst, "ERROR flash-receive: chunk chunk_length required\n");
		return(app_action_error);
	}

	if((chunk_length == 0) || ((chunk_offset % chunk_length) != 0))
	{
		string_append(dst, "ERROR: flash-receive: chunk offset should be divisible by chunk size");
		return(app_action_error);
	}

	if((chunk_length == 0) || ((SPI_FLASH_SEC_SIZE % chunk_length) != 0))
	{
		string_append(dst, "ERROR: flash-receive: chunk length should be divisible by flash sector size");
		return(app_action_error);
	}

	if((chunk_offset + chunk_length) > SPI_FLASH_SEC_SIZE)
	{
		string_format(dst, "ERROR flash-receive: chunk_length(%d) + chunk_offset(%d) > sector size(%d)\n", chunk_offset, chunk_length, SPI_FLASH_SEC_SIZE);
		return(app_action_error);
	}

	string_setlength(&flash_sector_buffer, SPI_FLASH_SEC_SIZE);

	string_format(dst, "OK flash-receive: sending bytes: %d, from offset: %d, data: @", chunk_length, chunk_offset);
	string_splice(dst, -1, &flash_sector_buffer, chunk_offset, chunk_length);
	string_append(dst, "\n");

	return(app_action_normal);
}

irom app_action_t application_function_flash_read(string_t *src, string_t *dst)
{
	unsigned int address, sector;

	SHA_CTX sha_context;
	uint8_t sha_result[SHA_DIGEST_LENGTH];
	string_new(, sha_string, SHA_DIGEST_LENGTH * 2 + 2);

	if(string_size(&flash_sector_buffer) < SPI_FLASH_SEC_SIZE)
	{
		string_format(dst, "ERROR flash-read: flash sector buffer too small: %u\n", string_size(&flash_sector_buffer));
		return(app_action_error);
	}

	if(parse_uint(1, src, &address, 0, ' ') != parse_ok)
	{
		string_append(dst, "ERROR flash-read: address required\n");
		return(app_action_error);
	}

	if((address % SPI_FLASH_SEC_SIZE) != 0)
	{
		string_append(dst, "ERROR flash-read: address should be divisible by flash sector size");
		return(app_action_error);
	}

	sector = address / SPI_FLASH_SEC_SIZE;
	spi_flash_read(sector * SPI_FLASH_SEC_SIZE, string_buffer_nonconst(&flash_sector_buffer), SPI_FLASH_SEC_SIZE);

	SHA1Init(&sha_context);
	SHA1Update(&sha_context, string_buffer(&flash_sector_buffer), SPI_FLASH_SEC_SIZE);
	SHA1Final(sha_result, &sha_context);
	string_bin_to_hex(&sha_string, sha_result, SHA_DIGEST_LENGTH);

	string_format(dst, "OK flash-read: read bytes: %d, from address: %d (%d), checksum: ", SPI_FLASH_SEC_SIZE, address, sector);

	string_append_string(dst, &sha_string);
	string_append(dst, "\n");

	return(app_action_normal);
}

irom static app_action_t flash_write_verify_(const string_t *src, string_t *dst, _Bool verify)
{
	unsigned int address, sector;
	int byte;
	int same = 0;
	int erase = 1;
	const uint8_t *ptr;

	SHA_CTX sha_context;
	uint8_t sha_result[SHA_DIGEST_LENGTH];
	string_new(, sha_string, SHA_DIGEST_LENGTH * 2 + 2);

	if(string_size(&flash_sector_buffer) < SPI_FLASH_SEC_SIZE)
	{
		string_format(dst, "ERROR flash-verify: flash sector buffer too small: %u\n", string_size(&flash_sector_buffer));
		return(app_action_error);
	}

	if(parse_uint(1, src, &address, 0, ' ') != parse_ok)
	{
		if(verify)
			string_append(dst, "ERROR flash-verify");
		else
			string_append(dst, "ERROR flash-write");

		string_append(dst, ": address required\n");

		return(app_action_error);
	}

	if((address % SPI_FLASH_SEC_SIZE) != 0)
	{
		if(verify)
			string_append(dst, "ERROR flash-verify");
		else
			string_append(dst, "ERROR flash-write");

		string_append(dst, ": address should be divisible by flash sector size");

		return(app_action_error);
	}

	sector = address / SPI_FLASH_SEC_SIZE;
	spi_flash_read(sector * SPI_FLASH_SEC_SIZE, string_buffer_nonconst(dst), SPI_FLASH_SEC_SIZE);

	erase = 0;
	same = 0;
	SHA1Init(&sha_context);

	if(verify)
	{
		if(!memcmp(string_buffer(&flash_sector_buffer), string_buffer(dst), SPI_FLASH_SEC_SIZE))
			same = 1;

		SHA1Update(&sha_context, string_buffer(dst), SPI_FLASH_SEC_SIZE);
	}
	else
	{
		if(memcmp(string_buffer(&flash_sector_buffer), string_buffer(dst), SPI_FLASH_SEC_SIZE))
		{
			for(byte = 0, ptr = string_buffer(dst); byte < SPI_FLASH_SEC_SIZE; byte++, ptr++)
			{
				if(*ptr != 0xff)
				{
					erase = 1;
					break;
				}
			}

			if(erase)
				spi_flash_erase_sector(sector);

			spi_flash_write(sector * SPI_FLASH_SEC_SIZE, string_buffer(&flash_sector_buffer), SPI_FLASH_SEC_SIZE);
		}
		else
			same = 1;

		SHA1Update(&sha_context, string_buffer(&flash_sector_buffer), SPI_FLASH_SEC_SIZE);
	}

	SHA1Final(sha_result, &sha_context);
	string_bin_to_hex(&sha_string, sha_result, SHA_DIGEST_LENGTH);

	if(verify)
		string_format(dst, "OK flash-verify: verified bytes: %d, at address: %d (%d), same: %d, checksum: ", SPI_FLASH_SEC_SIZE, address, sector, same);
	else
		string_format(dst, "OK flash-write: written bytes: %d, to address: %d (%d), same: %d, erased: %d, checksum: ", SPI_FLASH_SEC_SIZE, address, sector, same, erase);

	string_append_string(dst, &sha_string);
	string_append(dst, "\n");

	return(app_action_normal);
}

irom app_action_t application_function_flash_write(string_t *src, string_t *dst)
{
	return(flash_write_verify_(src, dst, false));
}

irom app_action_t application_function_flash_verify(string_t *src, string_t *dst)
{
	return(flash_write_verify_(src, dst, true));
}

irom app_action_t application_function_flash_checksum(string_t *src, string_t *dst)
{
	unsigned int address, current, length, done;

	SHA_CTX sha_context;
	uint8_t sha_result[SHA_DIGEST_LENGTH];
	string_new(, sha_string, SHA_DIGEST_LENGTH * 2 + 2);

	if(string_size(&flash_sector_buffer) < SPI_FLASH_SEC_SIZE)
	{
		string_format(dst, "ERROR flash-checksum: flash sector buffer too small: %u\n", string_size(&flash_sector_buffer));
		return(app_action_error);
	}

	if(parse_uint(1, src, &address, 0, ' ') != parse_ok)
	{
		string_append(dst, "ERROR flash-checksum: address required\n");
		return(app_action_error);
	}

	if(parse_uint(2, src, &length, 0, ' ') != parse_ok)
	{
		string_append(dst, "ERROR flash-checksum: length required\n");
		return(app_action_error);
	}

	if((address % SPI_FLASH_SEC_SIZE) != 0)
	{
		string_append(dst, "ERROR: flash_checksum: address should be divisible by flash sector size");
		return(app_action_error);
	}

	if((length % SPI_FLASH_SEC_SIZE) != 0)
	{
		string_append(dst, "ERROR: flash_checksum: length should be divisible by flash sector size");
		return(app_action_error);
	}

	SHA1Init(&sha_context);

	for(current = address, done = 0; done < length; current += SPI_FLASH_SEC_SIZE, done += SPI_FLASH_SEC_SIZE)
	{
		spi_flash_read(current, string_buffer_nonconst(dst), SPI_FLASH_SEC_SIZE);
		SHA1Update(&sha_context, string_buffer(dst), SPI_FLASH_SEC_SIZE);
	}

	SHA1Final(sha_result, &sha_context);
	string_bin_to_hex(&sha_string, sha_result, SHA_DIGEST_LENGTH);

	string_clear(dst);
	string_format(dst, "OK flash-checksum: checksummed bytes: %d, from address: %d, checksum: ", done, address);
	string_append_string(dst, &sha_string);
	string_append(dst, "\n");

	return(app_action_normal);
}

irom static app_action_t flash_select(const string_t *src, string_t *dst, _Bool once)
{
	const char *cmdname = once ? "flash-select-once" : "flash-select";

#if IMAGE_OTA == 0
	string_format(dst, "ERROR %s: no OTA image\n", cmdname);
	return(app_action_error);
#else
	unsigned int slot;

	rboot_if_config_t config;
	rboot_if_rtc_config_t rtc;

	if(!rboot_if_read_config(&config))
	{
		string_format(dst, "ERROR %s: rboot config invalid\n", cmdname);
		return(app_action_error);
	}

	if(parse_uint(1, src, &slot, 0, ' ') != parse_ok)
	{
		string_format(dst, "ERROR %s: slot required\n", cmdname);
		return(app_action_error);
	}

	if(slot >= config.slot_count)
	{
		string_format(dst, "ERROR %s: invalid slot, valid range = 0 - %d\n", cmdname, config.slot_count - 1);
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
			string_format(dst, "ERROR: %s: RTC RAM config signature absent and can't create a new one\n", cmdname);
			return(app_action_error);
		}
	}

	if(!rboot_if_read_rtc_ram(&rtc))
	{
		string_format(dst, "ERROR: %s: write initial data to RTC RAM failed\n", cmdname);
		return(app_action_error);
	}

	if(rboot_if_mapped_slot() != rtc.last_slot)
	{
		string_format(dst, "ERROR %s: current slot according to rboot RTC RAM info does not match flash memory map: %u vs. %u\n",
				cmdname, config.slot_current, rboot_if_mapped_slot());
		return(app_action_error);
	}

	rtc.next_mode = once ? rboot_if_conf_mode_temp_rom : rboot_if_conf_mode_standard;
	rtc.temporary_slot = slot;

	if(!rboot_if_write_rtc_ram(&rtc))
	{
		string_format(dst, "ERROR: %s: write data to RTC RAM failed\n", cmdname);
		return(app_action_error);
	}

	if(!rboot_if_read_rtc_ram(&rtc))
	{
		string_format(dst, "ERROR: %s: verify RTC data failed\n", cmdname);
		return(app_action_error);
	}

	if(rtc.next_mode != (once ? rboot_if_conf_mode_temp_rom : rboot_if_conf_mode_standard))
	{
		string_format(dst, "ERROR: %s: RTC data invalid, next boot mode: %s\n", cmdname, rboot_if_boot_mode(rtc.next_mode));
		return(app_action_error);
	}

	if(rtc.temporary_slot != slot)
	{
		string_format(dst, "ERROR: %s: RTC data invalid, next boot slot: %x\n", cmdname, rtc.temporary_slot);
		return(app_action_error);
	}

	slot = rtc.temporary_slot;

	if(!once)
	{
		_Bool success;

		if(!rboot_if_read_config(&config))
		{
			string_format(dst, "ERROR %s: rboot config invalid\n", cmdname);
			return(app_action_error);
		}

		config.slot_current = slot;

		string_clear(dst);
		success = rboot_if_write_config(&config, dst);
		string_clear(dst);

		if(!success)
		{
			string_format(dst, "ERROR %s: update rboot config failed\n", cmdname);
			return(app_action_error);
		}

		if(!rboot_if_read_config(&config))
		{
			string_format(dst, "ERROR %s: rboot config invalid after update\n", cmdname);
			return(app_action_error);
		}

		if(config.slot_current != slot)
		{
			string_format(dst, "ERROR %s: slot not selected\n", cmdname);
			return(app_action_error);
		}

		slot = config.slot_current;
	}

	string_format(dst, "OK %s: slot %d selected, address %d\n", cmdname, slot, config.slots[slot]);

	return(app_action_normal);
#endif
}

irom app_action_t application_function_flash_select(string_t *src, string_t *dst)
{
	return(flash_select(src, dst, false));
}

irom app_action_t application_function_flash_select_once(string_t *src, string_t *dst)
{
	return(flash_select(src, dst, true));
}
