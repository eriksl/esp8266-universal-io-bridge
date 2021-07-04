#include "ota.h"
#include "util.h"
#include "sys_string.h"
#include "config.h"
#include "dispatch.h"
#include "rboot-interface.h"
#include "sdk.h"
#include "lwip-interface.h"

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

app_action_t application_function_flash_info(string_t *src, string_t *dst)
{
	int ota_available = 0;
	int ota_slots = 0;
	int ota_slot = 0;
	int ota_address_0 = 0;
	int ota_address_1 = 0;
	int ota_address_2 = 0;
	int ota_address_3 = 0;

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

	if(flash_sector_buffer_use == fsb_ota)
		flash_sector_buffer_use = fsb_free;

	string_format(dst, "OK flash function available, "
				"sector size: %d bytes, "
				"OTA update available: %d, "
				"slots: %d, slot: %d, "
				"address: %d, address: %d, address: %d, address: %d, "
				"preferred chunk size: %d"
				"\n",
			SPI_FLASH_SEC_SIZE,
			ota_available,
			ota_slots, ota_slot,
			ota_address_0, ota_address_1, ota_address_2, ota_address_3,
			4096);

	return(app_action_normal);
}

app_action_t application_function_flash_erase(string_t *src, string_t *dst)
{
	unsigned int address, length;
	int sector_offset, sector_count, erased;
	uint32_t time_start, time_finish;
	SpiFlashOpResult flash_result;

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

		flash_result = spi_flash_erase_sector(sector_offset + erased);

		if(flash_result == SPI_FLASH_RESULT_ERR)
		{
			string_append(dst, "ERROR flash-erase: erase error\n");
			return(app_action_error);
		}

		if(flash_result == SPI_FLASH_RESULT_TIMEOUT)
		{
			string_append(dst, "ERROR flash-erase: erase timeout\n");
			return(app_action_error);
		}
	}

	time_finish = system_get_time();

	string_format(dst, "OK flash-erase: erased %d sectors from sector %d, in %lu milliseconds\n", erased - 1, sector_offset, (time_finish - time_start) / 1000);

	return(app_action_normal);
}

app_action_t application_function_flash_send(string_t *src, string_t *dst)
{
	int chunk_offset;
	unsigned int offset, length, chunk_length;

	if(parse_uint(1, src, &offset, 0, ' ') != parse_ok)
	{
		string_append(dst, "ERROR flash-send: offset required\n");
		return(app_action_error);
	}

	if(parse_uint(2, src, &length, 0, ' ') != parse_ok)
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
		string_format(dst, "ERROR flash-send: length(%u) + offset(%u) > sector size(%d)\n", offset, length, SPI_FLASH_SEC_SIZE);
		return(app_action_error);
	}

	if((chunk_offset = string_sep(src, 0, 3, ' ')) < 0)
	{
		string_append(dst, "ERROR flash-send: missing data\n");
		return(app_action_error);
	}

	if((chunk_length = string_length(src) - chunk_offset) != length)
	{
		string_format(dst, "ERROR flash-send: data length mismatch: %u != %u\n", length, chunk_length);
		return(app_action_error);
	}

	if(string_size(&flash_sector_buffer) < SPI_FLASH_SEC_SIZE)
	{
		string_format(dst, "ERROR flash-send: flash sector buffer too small: %d\n", string_size(&flash_sector_buffer));
		return(app_action_error);
	}

	if((flash_sector_buffer_use != fsb_free) && (flash_sector_buffer_use != fsb_config_cache) &&
			(flash_sector_buffer_use != fsb_ota) && (flash_sector_buffer_use != fsb_display_picture))
	{
		string_format(dst, "ERROR flash_send: sector buffer in use: %u\n", flash_sector_buffer_use);
		return(app_action_error);
	}

	flash_sector_buffer_use = fsb_ota;

	string_splice(&flash_sector_buffer, offset, src, chunk_offset, chunk_length);

	string_format(dst, "OK flash-send: received bytes: %u, at offset: %u\n", length, offset);

	return(app_action_normal);
}

app_action_t application_function_flash_receive(string_t *src, string_t *dst)
{
	unsigned int chunk_offset, chunk_length;

	if(string_size(&flash_sector_buffer) < SPI_FLASH_SEC_SIZE)
	{
		string_format(dst, "ERROR flash-receive: flash sector buffer too small: %d\n", string_size(&flash_sector_buffer));
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
		string_format(dst, "ERROR flash-receive: chunk_length(%u) + chunk_offset(%u) > sector size(%d)\n", chunk_length, chunk_offset, SPI_FLASH_SEC_SIZE);
		return(app_action_error);
	}

	if((flash_sector_buffer_use != fsb_free) && (flash_sector_buffer_use != fsb_config_cache) &&
			(flash_sector_buffer_use != fsb_ota) && (flash_sector_buffer_use != fsb_display_picture))
	{
		string_format(dst, "ERROR: flash_send: sector buffer in use: %u\n", flash_sector_buffer_use);
		return(app_action_error);
	}

	flash_sector_buffer_use = fsb_ota;

	string_format(dst, "OK flash-receive: sending bytes: %u, from offset: %u, data: @", chunk_length, chunk_offset);
	string_splice(dst, -1, &flash_sector_buffer, chunk_offset, chunk_length);
	string_append(dst, "\n");

	if((chunk_offset + chunk_length) >= SPI_FLASH_SEC_SIZE)
		flash_sector_buffer_use = fsb_free;

	return(app_action_normal);
}

app_action_t application_function_flash_read(string_t *src, string_t *dst)
{
	unsigned int address, sector;
	SHA_CTX sha_context;
	unsigned char sha_result[SHA_DIGEST_LENGTH];
	string_new(, sha_string, SHA_DIGEST_LENGTH * 2 + 2);
	SpiFlashOpResult flash_result;

	if(string_size(&flash_sector_buffer) < SPI_FLASH_SEC_SIZE)
	{
		string_format(dst, "ERROR flash-read: flash sector buffer too small: %d\n", string_size(&flash_sector_buffer));
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

	if((flash_sector_buffer_use != fsb_free) && (flash_sector_buffer_use != fsb_config_cache) &&
			(flash_sector_buffer_use != fsb_display_picture))
	{
		string_format(dst, "ERROR: flash-read: sector buffer in use: %u\n", flash_sector_buffer_use);
		return(app_action_error);
	}

	flash_sector_buffer_use = fsb_ota;

	sector = address / SPI_FLASH_SEC_SIZE;
	flash_result = spi_flash_read(sector * SPI_FLASH_SEC_SIZE, string_buffer_nonconst(&flash_sector_buffer), SPI_FLASH_SEC_SIZE);
	string_setlength(&flash_sector_buffer, SPI_FLASH_SEC_SIZE);

	if(flash_result == SPI_FLASH_RESULT_ERR)
	{
		string_append(dst, "ERROR: flash-read: read error\n");
		return(app_action_error);
	}

	if(flash_result == SPI_FLASH_RESULT_TIMEOUT)
	{
		string_append(dst, "ERROR: flash-read: read timeout\n");
		return(app_action_error);
	}

	SHA1Init(&sha_context);
	SHA1Update(&sha_context, string_buffer(&flash_sector_buffer), SPI_FLASH_SEC_SIZE);
	SHA1Final(sha_result, &sha_context);
	string_bin_to_hex(&sha_string, sha_result, SHA_DIGEST_LENGTH);

	string_format(dst, "OK flash-read: read bytes: %d, from address: %u (%u), checksum: ", SPI_FLASH_SEC_SIZE, address, sector);
	string_append_string(dst, &sha_string);
	string_append(dst, "\n");

	return(app_action_normal);
}

static app_action_t flash_write_verify_(string_t *src, string_t *dst, bool verify)
{
	unsigned int address, sector;
	int byte;
	int same = 0;
	int erase = 1;
	const char *ptr;
	const char *caller = verify ? "verify" : "write";
	SHA_CTX sha_context;
	unsigned char sha_result[SHA_DIGEST_LENGTH];
	string_new(, sha_string, SHA_DIGEST_LENGTH * 2 + 2);
	SpiFlashOpResult flash_result;

	if(string_size(&flash_sector_buffer) < SPI_FLASH_SEC_SIZE)
	{
		string_format(dst, "ERROR flash-%s: flash sector buffer too small: %d\n", caller, string_size(&flash_sector_buffer));
		return(app_action_error);
	}

	if(string_size(dst) < SPI_FLASH_SEC_SIZE)
	{
		string_format(dst, "ERROR flash-%s: dst buffer too small: %d\n", caller, string_size(dst));
		return(app_action_error);
	}

	if(parse_uint(1, src, &address, 0, ' ') != parse_ok)
	{
		string_format(dst, "ERROR flash-%s: address required\n", caller);
		return(app_action_error);
	}

	if((address % SPI_FLASH_SEC_SIZE) != 0)
	{
		string_format(dst, "ERROR flash-%s: address should be divisible by flash sector size", caller);
		return(app_action_error);
	}

	if(flash_sector_buffer_use != fsb_ota)
	{
		string_format(dst, "ERROR: flash-%s: sector buffer in use: %u\n", caller, flash_sector_buffer_use);
		return(app_action_error);
	}

	sector = address / SPI_FLASH_SEC_SIZE;
	flash_result = spi_flash_read(sector * SPI_FLASH_SEC_SIZE, string_buffer_nonconst(dst), SPI_FLASH_SEC_SIZE);

	if(flash_result == SPI_FLASH_RESULT_ERR)
	{
		string_format(dst, "ERROR: flash-%s: read error\n", caller);
		return(app_action_error);
	}

	if(flash_result == SPI_FLASH_RESULT_TIMEOUT)
	{
		string_format(dst, "ERROR: flash-%s: read timeout\n", caller);
		return(app_action_error);
	}

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
				if(*(const uint8_t *)ptr != 0xff)
				{
					erase = 1;
					break;
				}
			}

			if(erase)
			{
				flash_result = spi_flash_erase_sector(sector);

				if(flash_result == SPI_FLASH_RESULT_ERR)
				{
					string_format(dst, "ERROR: flash-%s: erase error\n", caller);
					return(app_action_error);
				}

				if(flash_result == SPI_FLASH_RESULT_TIMEOUT)
				{
					string_format(dst, "ERROR: flash-%s: erase timeout\n", caller);
					return(app_action_error);
				}
			}

			flash_result = spi_flash_write(sector * SPI_FLASH_SEC_SIZE, string_buffer(&flash_sector_buffer), SPI_FLASH_SEC_SIZE);

			if(flash_result == SPI_FLASH_RESULT_ERR)
			{
				string_format(dst, "ERROR: flash-%s: read error\n", caller);
				return(app_action_error);
			}

			if(flash_result == SPI_FLASH_RESULT_TIMEOUT)
			{
				string_format(dst, "ERROR: flash-%s: read timeout\n", caller);
				return(app_action_error);
			}
		}
		else
			same = 1;

		SHA1Update(&sha_context, string_buffer(&flash_sector_buffer), SPI_FLASH_SEC_SIZE);
	}

	SHA1Final(sha_result, &sha_context);
	string_bin_to_hex(&sha_string, sha_result, SHA_DIGEST_LENGTH);

	flash_sector_buffer_use = fsb_free;

	if(verify)
		string_format(dst, "OK flash-verify: verified bytes: %d, at address: %u (%u), same: %d, checksum: ", SPI_FLASH_SEC_SIZE, address, sector, same);
	else
		string_format(dst, "OK flash-write: written bytes: %d, to address: %u (%u), same: %d, erased: %d, checksum: ", SPI_FLASH_SEC_SIZE, address, sector, same, erase);

	string_append_string(dst, &sha_string);
	string_append(dst, "\n");

	return(app_action_normal);
}

app_action_t application_function_flash_write(string_t *src, string_t *dst)
{
	return(flash_write_verify_(src, dst, false));
}

app_action_t application_function_flash_verify(string_t *src, string_t *dst)
{
	return(flash_write_verify_(src, dst, true));
}

app_action_t application_function_flash_checksum(string_t *src, string_t *dst)
{
	unsigned int address, current, length, done;
	SpiFlashOpResult flash_result;

	SHA_CTX sha_context;
	unsigned char sha_result[SHA_DIGEST_LENGTH];
	string_new(, sha_string, SHA_DIGEST_LENGTH * 2 + 2);

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
		flash_result = spi_flash_read(current, string_buffer_nonconst(dst), SPI_FLASH_SEC_SIZE);

		if(flash_result == SPI_FLASH_RESULT_ERR)
		{
			string_append(dst, "ERROR: flash-checksum: read error\n");
			return(app_action_error);
		}

		if(flash_result == SPI_FLASH_RESULT_TIMEOUT)
		{
			string_append(dst, "ERROR: flash-checksum: read timeout\n");
			return(app_action_error);
		}

		SHA1Update(&sha_context, string_buffer(dst), SPI_FLASH_SEC_SIZE);
	}

	SHA1Final(sha_result, &sha_context);
	string_bin_to_hex(&sha_string, sha_result, SHA_DIGEST_LENGTH);

	string_clear(dst);
	string_format(dst, "OK flash-checksum: checksummed bytes: %u, from address: %u, checksum: ", done, address);
	string_append_string(dst, &sha_string);
	string_append(dst, "\n");

	return(app_action_normal);
}

static app_action_t flash_select(string_t *src, string_t *dst, bool once)
{
	const char *cmdname = once ? "flash-select-once" : "flash-select";
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
		bool success;

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

	string_format(dst, "OK %s: slot %u selected, address %lu\n", cmdname, slot, config.slots[slot]);

	return(app_action_normal);
}

app_action_t application_function_flash_select(string_t *src, string_t *dst)
{
	return(flash_select(src, dst, false));
}

app_action_t application_function_flash_select_once(string_t *src, string_t *dst)
{
	return(flash_select(src, dst, true));
}

string_new(static, mailbox_socket_receive_buffer, 4096);
string_new(static, mailbox_socket_send_buffer, 4096);
static lwip_if_socket_t mailbox_socket;

static void socket_mailbox_callback_data_received(lwip_if_socket_t *socket, unsigned int length)
{
	lwip_if_receive_buffer_unlock(socket);

	if((string_length(&mailbox_socket_receive_buffer) == SPI_FLASH_SEC_SIZE) && (length != 0))
	{
		string_clear(&mailbox_socket_send_buffer);
		string_append(&mailbox_socket_send_buffer, "ACK");
		lwip_if_send(&mailbox_socket);
	}
}

void ota_init(unsigned int mailbox_port)
{
	lwip_if_socket_create(&mailbox_socket, &mailbox_socket_receive_buffer, &mailbox_socket_send_buffer, mailbox_port,
			true, false, socket_mailbox_callback_data_received);
}

app_action_t application_function_mailbox_info(string_t *src, string_t *dst)
{
	rboot_if_config_t		config;
	rboot_if_rtc_config_t	rtc;
	unsigned int ota_slot;

	if(!rboot_if_read_config(&config))
	{
		string_append(dst, "ERROR rboot config invalid\n");
		return(app_action_error);
	}

	if(rboot_if_read_rtc_ram(&rtc))
		ota_slot = rtc.last_slot;
	else
		ota_slot = config.slot_current;

	string_format(dst, "OK mailbox function available, "
				"slots: %u, current: %u, "
				"sectors: [ %lu, %lu ]\n",
			config.slot_count, ota_slot,
			config.slots[0] / SPI_FLASH_SEC_SIZE, config.slots[1] / SPI_FLASH_SEC_SIZE);

	return(app_action_normal);
}

app_action_t application_function_mailbox_reset(string_t *src, string_t *dst)
{
	string_clear(&mailbox_socket_send_buffer);
	string_clear(&mailbox_socket_receive_buffer);
	lwip_if_receive_buffer_unlock(&mailbox_socket);

	string_format(dst, "OK mailbox-reset\n");

	return(app_action_normal);
}

app_action_t application_function_mailbox_read(string_t *src, string_t *dst)
{
	unsigned int sector;
	SHA_CTX sha_context;
	unsigned char sha_result[SHA_DIGEST_LENGTH];
	string_new(, sha_string, SHA_DIGEST_LENGTH * 2 + 2);
	SpiFlashOpResult flash_result;

	if(parse_uint(1, src, &sector, 0, ' ') != parse_ok)
	{
		string_append(dst, "ERROR: mailbox-read: invalid sector\n");
		return(app_action_error);
	}

	string_clear(&mailbox_socket_send_buffer);

	flash_result = spi_flash_read(sector * SPI_FLASH_SEC_SIZE, string_buffer_nonconst(&mailbox_socket_send_buffer), SPI_FLASH_SEC_SIZE);

	if(flash_result == SPI_FLASH_RESULT_ERR)
	{
		string_append(dst, "ERROR: mailbox-read: read error\n");
		return(app_action_error);
	}

	if(flash_result == SPI_FLASH_RESULT_TIMEOUT)
	{
		string_append(dst, "ERROR: mailbox-read: read timeout\n");
		return(app_action_error);
	}

	string_setlength(&mailbox_socket_send_buffer, SPI_FLASH_SEC_SIZE);

	SHA1Init(&sha_context);
	SHA1Update(&sha_context, string_buffer(&mailbox_socket_send_buffer), SPI_FLASH_SEC_SIZE);
	SHA1Final(sha_result, &sha_context);
	string_bin_to_hex(&sha_string, sha_result, SHA_DIGEST_LENGTH);

	string_format(dst, "OK mailbox-read: sending sector %u, checksum: %s\n", sector, string_to_cstr(&sha_string));

	if(!lwip_if_send(&mailbox_socket))
		log("mailbox read failed\n");

	return(app_action_normal);
}

app_action_t application_function_mailbox_bench(string_t *src, string_t *dst)
{
	unsigned int direction;

	if(parse_uint(1, src, &direction, 0, ' ') != parse_ok)
	{
		string_append(dst, "ERROR: mailbox-bench: invalid direction\n");
		return(app_action_error);
	}

	if(direction == 0)
	{
		string_setlength(&mailbox_socket_send_buffer, SPI_FLASH_SEC_SIZE);
		string_append(dst, "OK mailbox-bench: sending one sector\n");

		if(!lwip_if_send(&mailbox_socket))
			log("mailbox-bench send failed\n");
	}
	else
	{
		if(string_length(&mailbox_socket_receive_buffer) != SPI_FLASH_SEC_SIZE)
			string_format(dst, "ERROR mailbox-bench: received incomplete sector: %d\n", string_length(&mailbox_socket_receive_buffer));
		else
			string_append(dst, "OK mailbox-bench: received one sector\n");

		string_setlength(&mailbox_socket_receive_buffer, 0);
		lwip_if_receive_buffer_unlock(&mailbox_socket);
	}

	return(app_action_normal);
}

app_action_t application_function_mailbox_checksum(string_t *src, string_t *dst)
{
	unsigned int current, sector, sectors;
	SpiFlashOpResult flash_result;

	SHA_CTX sha_context;
	unsigned char sha_result[SHA_DIGEST_LENGTH];
	string_new(, sha_string, SHA_DIGEST_LENGTH * 2 + 2);

	if(parse_uint(1, src, &sector, 0, ' ') != parse_ok)
	{
		string_append(dst, "ERROR mailbox-checksum: start sector required\n");
		return(app_action_error);
	}

	if(parse_uint(2, src, &sectors, 0, ' ') != parse_ok)
	{
		string_append(dst, "ERROR mailbox-checksum: amount of sectors required\n");
		return(app_action_error);
	}

	SHA1Init(&sha_context);

	for(current = sector; (current - sector) < sectors; current++)
	{
		flash_result = spi_flash_read(current * SPI_FLASH_SEC_SIZE, string_buffer_nonconst(dst), SPI_FLASH_SEC_SIZE);

		if(flash_result == SPI_FLASH_RESULT_ERR)
		{
			string_append(dst, "ERROR: mailbox-checksum: read error\n");
			return(app_action_error);
		}

		if(flash_result == SPI_FLASH_RESULT_TIMEOUT)
		{
			string_append(dst, "ERROR: mailbox-checksum: read timeout\n");
			return(app_action_error);
		}

		SHA1Update(&sha_context, string_buffer(dst), SPI_FLASH_SEC_SIZE);
	}

	SHA1Final(sha_result, &sha_context);
	string_bin_to_hex(&sha_string, sha_result, SHA_DIGEST_LENGTH);

	string_clear(dst);
	string_format(dst, "OK mailbox-checksum: checksummed sectors: %u, from sector: %u, checksum: ", sectors, sector);
	string_append_string(dst, &sha_string);
	string_append(dst, "\n");

	return(app_action_normal);
}

app_action_t application_function_mailbox_simulate(string_t *src, string_t *dst)
{
	unsigned int sector;
	SHA_CTX sha_context;
	unsigned char sha_result[SHA_DIGEST_LENGTH];
	string_new(, sha_string, SHA_DIGEST_LENGTH * 2 + 2);

	if(parse_uint(1, src, &sector, 0, ' ') != parse_ok)
	{
		string_append(dst, "ERROR: mailbox-simulate: invalid sector\n");
		return(app_action_error);
	}

	if(string_length(&mailbox_socket_receive_buffer) != SPI_FLASH_SEC_SIZE)
	{
		string_format(dst, "ERROR: mailbox-simulate: mailbox incomplete, %d bytes\n", string_length(&mailbox_socket_receive_buffer));
		return(app_action_error);
	}

	SHA1Init(&sha_context);
	SHA1Update(&sha_context, string_buffer(&mailbox_socket_receive_buffer), SPI_FLASH_SEC_SIZE);
	SHA1Final(sha_result, &sha_context);
	string_bin_to_hex(&sha_string, sha_result, SHA_DIGEST_LENGTH);

	string_format(dst, "OK mailbox-simulate: received sector %u, erased: %u, skipped %u, checksum: %s\n", sector, 0U, 0U, string_to_cstr(&sha_string));

	string_clear(&mailbox_socket_receive_buffer);
	lwip_if_receive_buffer_unlock(&mailbox_socket);

	return(app_action_normal);
}

app_action_t application_function_mailbox_write(string_t *src, string_t *dst)
{
	unsigned int sector;
	SHA_CTX sha_context;
	unsigned char sha_result[SHA_DIGEST_LENGTH];
	string_new(, sha_string, SHA_DIGEST_LENGTH * 2 + 2);
	unsigned int skip, erase;
	SpiFlashOpResult flash_result;
	const uint8_t *received_buffer;
	const uint8_t *flash_buffer;

	if(parse_uint(1, src, &sector, 0, ' ') != parse_ok)
	{
		string_append(dst, "ERROR: mailbox-write: invalid sector\n");
		return(app_action_error);
	}

	if(string_length(&mailbox_socket_receive_buffer) != SPI_FLASH_SEC_SIZE)
	{
		string_format(dst, "ERROR: mailbox-write: mailbox incomplete, %d bytes\n", string_length(&mailbox_socket_receive_buffer));
		return(app_action_error);
	}

	flash_result = spi_flash_read(sector * SPI_FLASH_SEC_SIZE, string_buffer_nonconst(dst), SPI_FLASH_SEC_SIZE);

	if(flash_result == SPI_FLASH_RESULT_ERR)
	{
		string_clear(dst);
		string_append(dst, "ERROR: mailbox-write: read error (check)\n");
		return(app_action_error);
	}

	if(flash_result == SPI_FLASH_RESULT_TIMEOUT)
	{
		string_clear(dst);
		string_append(dst, "ERROR: mailbox-write: read timeout (check)\n");
		return(app_action_error);
	}

	received_buffer = (const uint8_t *)string_buffer(&mailbox_socket_receive_buffer);
	flash_buffer = (const uint8_t *)string_buffer(dst);

	erase = 0;
	skip = 1;

	for(unsigned int current = 0; current < SPI_FLASH_SEC_SIZE; current++)
	{
		if(received_buffer[current] != 0xff)
			erase = 1;

		if(received_buffer[current] != flash_buffer[current])
			skip = 0;

		if((erase == 1) && (skip == 0))
			break;
	}

	if(skip == 1)
		erase = 0;

	if(erase)
	{
		flash_result = spi_flash_erase_sector(sector);

		if(flash_result == SPI_FLASH_RESULT_ERR)
		{
			string_clear(dst);
			string_append(dst, "ERROR: mailbox-write: erase error\n");
			return(app_action_error);
		}

		if(flash_result == SPI_FLASH_RESULT_TIMEOUT)
		{
			string_clear(dst);
			string_append(dst, "ERROR: mailbox-write: erase timeout\n");
			return(app_action_error);
		}
	}

	if(!skip)
	{
		flash_result = spi_flash_write(sector * SPI_FLASH_SEC_SIZE, received_buffer, SPI_FLASH_SEC_SIZE);

		if(flash_result == SPI_FLASH_RESULT_ERR)
		{
			string_clear(dst);
			string_append(dst, "ERROR: mailbox-write: read error\n");
			return(app_action_error);
		}

		if(flash_result == SPI_FLASH_RESULT_TIMEOUT)
		{
			string_clear(dst);
			string_append(dst, "ERROR: mailbox-write: read timeout\n");
			return(app_action_error);
		}
	}

	flash_result = spi_flash_read(sector * SPI_FLASH_SEC_SIZE, string_buffer_nonconst(dst), SPI_FLASH_SEC_SIZE);

	if(flash_result == SPI_FLASH_RESULT_ERR)
	{
		string_clear(dst);
		string_append(dst, "ERROR: mailbox-write: read error (verify)\n");
		return(app_action_error);
	}

	if(flash_result == SPI_FLASH_RESULT_TIMEOUT)
	{
		string_clear(dst);
		string_append(dst, "ERROR: mailbox-write: read timeout (verify)\n");
		return(app_action_error);
	}

	SHA1Init(&sha_context);
	SHA1Update(&sha_context, string_buffer(dst), SPI_FLASH_SEC_SIZE);
	SHA1Final(sha_result, &sha_context);
	string_bin_to_hex(&sha_string, sha_result, SHA_DIGEST_LENGTH);

	string_clear(dst);
	string_format(dst, "OK mailbox-write: written sector %u, erased: %u, skipped %u, checksum: %s\n", sector, skip, erase, string_to_cstr(&sha_string));

	string_clear(&mailbox_socket_receive_buffer);
	lwip_if_receive_buffer_unlock(&mailbox_socket);

	return(app_action_normal);
}

app_action_t application_function_mailbox_select(string_t *src, string_t *dst)
{
	unsigned int slot;
	unsigned int permanent;

	rboot_if_config_t config;
	rboot_if_rtc_config_t rtc;

	if(parse_uint(1, src, &slot, 0, ' ') != parse_ok)
	{
		string_append(dst, "ERROR mailbox-select: slot required\n");
		return(app_action_error);
	}

	if(parse_uint(2, src, &permanent, 0, ' ') != parse_ok)
	{
		string_append(dst, "ERROR mailbox-select: permanent status required\n");
		return(app_action_error);
	}

	if(!rboot_if_read_config(&config))
	{
		string_append(dst, "ERROR mailbox-select: rboot config invalid\n");
		return(app_action_error);
	}

	if(slot >= config.slot_count)
	{
		string_append(dst, "ERROR mailbox-select: invalid slot\n");
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
			string_format(dst, "ERROR: mailbox-select: RTC RAM config signature absent and can't create a new one\n");
			return(app_action_error);
		}
	}

	if(!rboot_if_read_rtc_ram(&rtc))
	{
		string_append(dst, "ERROR: mailbox-select: write initial data to RTC RAM failed\n");
		return(app_action_error);
	}

	if(rboot_if_mapped_slot() != rtc.last_slot)
	{
		string_format(dst, "ERROR mailbox-select: current slot according to rboot RTC RAM info does not match flash memory map: %u vs. %u\n",
				config.slot_current, rboot_if_mapped_slot());
		return(app_action_error);
	}

	rtc.next_mode = permanent ? rboot_if_conf_mode_standard : rboot_if_conf_mode_temp_rom;
	rtc.temporary_slot = slot;

	if(!rboot_if_write_rtc_ram(&rtc))
	{
		string_append(dst, "ERROR: mailbox-select: write data to RTC RAM failed\n");
		return(app_action_error);
	}

	if(!rboot_if_read_rtc_ram(&rtc))
	{
		string_append(dst, "ERROR: mailbox-select: verify RTC data failed\n");
		return(app_action_error);
	}

	if(rtc.next_mode != (permanent ? rboot_if_conf_mode_standard : rboot_if_conf_mode_temp_rom))
	{
		string_format(dst, "ERROR: mailbox-select: RTC data invalid, next boot mode: %s\n", rboot_if_boot_mode(rtc.next_mode));
		return(app_action_error);
	}

	if(rtc.temporary_slot != slot)
	{
		string_format(dst, "ERROR: mailbox-selects: RTC data invalid, next boot slot: %x\n", rtc.temporary_slot);
		return(app_action_error);
	}

	slot = rtc.temporary_slot;

	if(permanent)
	{
		bool success;

		if(!rboot_if_read_config(&config))
		{
			string_append(dst, "ERROR mailbox-select: rboot config invalid\n");
			return(app_action_error);
		}

		config.slot_current = slot;

		string_clear(dst);
		success = rboot_if_write_config(&config, dst);
		string_clear(dst);

		if(!success)
		{
			string_append(dst, "ERROR mailbox-select: update rboot config failed\n");
			return(app_action_error);
		}

		if(!rboot_if_read_config(&config))
		{
			string_append(dst, "ERROR mailbox-select: rboot config invalid after update\n");
			return(app_action_error);
		}

		if(config.slot_current != slot)
		{
			string_append(dst, "ERROR mailbox-select: slot not selected\n");
			return(app_action_error);
		}

		slot = config.slot_current;
	}

	string_format(dst, "OK mailbox-select: slot %u, permanent %u\n", slot, permanent ? 1U : 0U);

	return(app_action_normal);
}
