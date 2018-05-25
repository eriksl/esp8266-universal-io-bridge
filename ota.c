#include "ota.h"
#include "util.h"
#include "config.h"

#if IMAGE_OTA == 1
#include <rboot-api.h>
#endif
#include <spi_flash.h>
#include <user_interface.h>
#include <stdint.h>
#include <stdlib.h>

typedef enum
{
	ota_inactive,
	ota_reading,
	ota_writing,
	ota_dummy,
	ota_successful
} ota_state_t;

string_new(, ota_receive_buffer, 0x1000);

static ota_state_t ota_state = ota_inactive;
static unsigned int remote_file_length, chunk_size, data_transferred;
static unsigned int flash_sector, flash_sectors_written, flash_sectors_skipped;
static int flash_start_address, flash_slot;
static MD5_CTX md5;

irom app_action_t application_function_ota_read(const string_t *src, string_t *dst)
{
	if(string_size(&ota_receive_buffer) < 0x1000) // FIXME
	{
		string_format(dst, "ota-read: string read buffer too small: %d\n", string_size(&ota_receive_buffer));
		return(app_action_error);
	}

	if(config_uses_logbuffer())
	{
		string_append(dst, "ota-read: string read buffer in use\n");
		return(app_action_error);
	}

	ota_state = ota_reading;
	data_transferred = 0;

	string_crc32_init();
	MD5Init(&md5);

	string_append(dst, "READ\n");
	return(app_action_normal);
}

irom app_action_t application_function_ota_receive(const string_t *src, string_t *dst)
{
	unsigned int address;
	uint32_t crc;

	if(ota_state != ota_reading)
	{
		string_append(dst, "ota-receive: flash read not active\n");
		ota_state = ota_inactive;
		return(app_action_error);
	}

	if(parse_int(1, src, &address, 0, ' ') != parse_ok)
	{
		string_append(dst, "ota-read: address required\n");
		return(app_action_error);
	}

	if(parse_int(2, src, &chunk_size, 0, ' ') != parse_ok)
	{
		string_append(dst, "ota-read: chunk size required\n");
		return(app_action_error);
	}

	if(chunk_size > 1024)
	{
		string_append(dst, "ota-read: chunk size invalid (> 1024)\n");
		return(app_action_error);
	}

	spi_flash_read(address, string_buffer_nonconst(&ota_receive_buffer), chunk_size);

	string_clear(dst);
	string_setlength(&ota_receive_buffer, chunk_size);
	crc = string_crc32(&ota_receive_buffer, 0, chunk_size);
	MD5Update(&md5, string_buffer(&ota_receive_buffer), chunk_size);
	data_transferred += chunk_size;
	string_format(dst, "DATA %u %u %u @", chunk_size, data_transferred, crc);
	string_splice(dst, -1, &ota_receive_buffer, 0, chunk_size);

	return(app_action_normal);
}

irom static app_action_t application_function_ota_write_or_dummy(const string_t *src, string_t *dst, bool_t real_write)
{
	if(string_size(&ota_receive_buffer) < 0x1000) // FIXME
	{
		string_format(dst, "ota-write: string write buffer too small: %d\n", string_size(&ota_receive_buffer));
		return(app_action_error);
	}

	if(config_uses_logbuffer())
	{
		string_append(dst, "ota-write: string write buffer in use\n");
		return(app_action_error);
	}

	if(parse_int(1, src, &remote_file_length, 0, ' ') != parse_ok)
	{
		string_append(dst, "ota-write: invalid/missing file length\n");
		return(app_action_error);
	}

	if(parse_int(2, src, &flash_start_address, 0, ' ') != parse_ok)
		flash_start_address = -1;

	if(flash_start_address == -1)
	{
#if IMAGE_OTA == 0
		string_append(dst, "ota-write: start address required on non-OTA image\n");
		return(app_action_error);
#else
		rboot_config rcfg = rboot_get_config();

		if((rcfg.magic != BOOT_CONFIG_MAGIC) || (rcfg.count != 2) || (rcfg.current_rom > 1))
		{
			string_append(dst, "ota-write: rboot config invalid\n");
			return(app_action_error);
		}

		flash_slot = rcfg.current_rom == 0 ? 1 : 0;
		flash_start_address = rcfg.roms[flash_slot];
#endif
	}
	else
		flash_slot = -1;

	if((flash_start_address & 0xfff) != 0)
	{
		string_format(dst, "ota-write: start address not aligned: %x\n", flash_start_address);
		return(app_action_error);
	}

	ota_state = real_write ? ota_writing : ota_dummy;
	data_transferred = 0;
	flash_sectors_written = 0;
	flash_sectors_skipped = 0;
	flash_sector = flash_start_address / 0x1000;

	string_clear(&ota_receive_buffer);
	string_crc32_init();
	MD5Init(&md5);

	string_format(dst, "WRITE %d %u\n", flash_slot, flash_sector);
	return(app_action_normal);
}

irom app_action_t application_function_ota_write_dummy(const string_t *src, string_t *dst)
{
	return(application_function_ota_write_or_dummy(src, dst, false));
}

irom app_action_t application_function_ota_write(const string_t *src, string_t *dst)
{
	return(application_function_ota_write_or_dummy(src, dst, true));
}

irom static app_action_t flash_write_verify(const string_t *src, string_t *dst)
{
	int write_buffer_length = string_length(&ota_receive_buffer);

	if(string_size(dst) < 0x1000)
	{
		string_format(dst, "ota-write: string verify buffer too small: %d\n", string_size(dst));
		return(app_action_error);
	}

	if(ota_state != ota_dummy)
	{
		spi_flash_read(flash_sector * 0x1000, string_buffer_nonconst(dst), write_buffer_length);

		if(memcmp(string_buffer(&ota_receive_buffer), string_buffer(dst), write_buffer_length))
		{
			spi_flash_erase_sector(flash_sector);
			spi_flash_write(flash_sector * 0x1000, string_buffer(&ota_receive_buffer), write_buffer_length);
			flash_sectors_written++;
		}
		else
			flash_sectors_skipped++;

		spi_flash_read(flash_sector * 0x1000, string_buffer_nonconst(dst), write_buffer_length);

		MD5Update(&md5, string_buffer(dst), write_buffer_length);

		if(memcmp(string_buffer(&ota_receive_buffer), string_buffer(dst), write_buffer_length))
		{
			string_clear(dst);
			string_append(dst, "ota-write: verify mismatch\n");
			return(app_action_error);
		}
	}
	else
		MD5Update(&md5, string_buffer(&ota_receive_buffer), write_buffer_length);

	flash_sector++;
	data_transferred += write_buffer_length;

	string_clear(&ota_receive_buffer);
	string_clear(dst);

	return(app_action_normal);
}

irom app_action_t application_function_ota_send(const string_t *raw_src, string_t *dst)
{
	int chunk_offset, chunk_length, remote_chunk_length;
	uint32_t crc, remote_crc;
	app_action_t action;
	string_t trimmed_src = *raw_src;

	if((ota_state != ota_writing) && (ota_state != ota_dummy))
	{
		string_append(dst, "ota-send: not active\n");
		ota_state = ota_inactive;
		return(app_action_error);
	}

	string_trim_nl(&trimmed_src);

	if(parse_int(1, &trimmed_src, &remote_chunk_length, 0, ' ') != parse_ok)
	{
		string_append(dst, "ota-send: missing chunk length\n");
		ota_state = ota_inactive;
		return(app_action_error);
	}

	if(parse_int(2, &trimmed_src, &remote_crc, 0, ' ') != parse_ok)
	{
		string_append(dst, "ota-send: missing crc\n");
		ota_state = ota_inactive;
		return(app_action_error);
	}

	if((chunk_offset = string_sep(&trimmed_src, 0, 3, ' ')) < 0)
	{
		string_append(dst, "ota-send: missing data chunk\n");
		ota_state = ota_inactive;
		return(app_action_error);
	}

	if((chunk_length = string_length(&trimmed_src) - chunk_offset) != remote_chunk_length)
	{
		string_format(dst, "ota-send: chunk length mismatch: %d != %d\n", remote_chunk_length, chunk_length);
		ota_state = ota_inactive;
		return(app_action_error);
	}

	if((crc = string_crc32(&trimmed_src, chunk_offset, chunk_length)) != remote_crc)
	{
		string_format(dst, "ota-send: CRC mismatch %08x != %08x\n", remote_crc, crc);
		ota_state = ota_inactive;
		return(app_action_error);
	}

	string_splice(&ota_receive_buffer, -1, &trimmed_src, chunk_offset, chunk_length);

	if(string_length(&ota_receive_buffer) > 0x1000)
	{
		string_format(dst, "ota-send: unaligned %u\n", string_length(&ota_receive_buffer));
		ota_state = ota_inactive;
		return(app_action_error);
	}

	if((string_length(&ota_receive_buffer) == 0x1000) &&
			((action = flash_write_verify(&trimmed_src, dst)) != app_action_normal))
	{
		ota_state = ota_inactive;
		return(action);
	}

	string_format(dst, "ACK %d\n", data_transferred);

	return(app_action_normal);
}

irom app_action_t application_function_ota_finish(const string_t *src, string_t *dst)
{
	static uint8_t md5_result[16];
	string_new(stack, local_md5_string, 34);
	string_new(stack, remote_md5_string, 34);
	app_action_t action;

	if(ota_state == ota_reading)
	{
		MD5Final(md5_result, &md5);
		string_bin_to_hex(&local_md5_string, md5_result, 16);
		string_clear(dst);

		string_format(dst, "READ_OK %s %u\n", string_to_cstr(&local_md5_string), data_transferred);
		ota_state = ota_inactive;
		return(app_action_normal);
	}
	else
		if((ota_state == ota_writing) || (ota_state == ota_dummy))
		{
			if((parse_string(1, src, &remote_md5_string, ' ')) != parse_ok)
			{
				string_clear(dst);
				string_append(dst, "ota-finish: missing md5sum string\n");
				ota_state = ota_inactive;
				return(app_action_error);
			}

			if((string_length(&ota_receive_buffer) > 0) &&
					((action = flash_write_verify(src, dst)) != app_action_normal))
			{
				ota_state = ota_inactive;
				return(action);
			}

			MD5Final(md5_result, &md5);
			string_bin_to_hex(&local_md5_string, md5_result, 16);
			string_clear(dst);

			if(remote_file_length != data_transferred)
			{
				string_format(dst, "ota-finish: file size differs: %u != %u\n", remote_file_length, data_transferred);
				ota_state = ota_inactive;
				return(app_action_error);
			}
		}
		else
		{
			string_append(dst, "ota-finish: not active\n");
			ota_state = ota_inactive;
			return(app_action_error);
		}

	if(!string_match_string(&local_md5_string, &remote_md5_string))
	{
		string_format(dst, "ota-finish: invalid md5sum: \"%s\" != \"%s\"\n",
				string_to_cstr(&local_md5_string), string_to_cstr(&remote_md5_string));
		ota_state = ota_inactive;
		return(app_action_error);
	}

	string_format(dst, "%s%sWRITE_OK %s %s %d %d\n",
				flash_slot == -1 ? "PARTIAL_" : "",
				ota_state == ota_dummy ? "DUMMY_" : "",
				string_to_cstr(&local_md5_string),
				string_to_cstr(&remote_md5_string),
				flash_sectors_written, flash_sectors_skipped);

	if((ota_state == ota_dummy) || (flash_slot == -1))
		ota_state = ota_inactive;
	else
		ota_state = ota_successful;

	return(app_action_normal);
}

irom app_action_t application_function_ota_commit(const string_t *src, string_t *dst)
{
#if IMAGE_OTA == 0
	string_append(dst, "ota-commit: unavailable on non-OTA image\n");
	return(app_action_error);
#else
	if(ota_state != ota_successful)
	{
		string_append(dst, "ota-commit: no candidate for commit\n");
		ota_state = ota_inactive;
		return(app_action_error);
	}

	if(!rboot_set_current_rom(flash_slot))
	{
		string_format(dst, "ota-commit: set current slot to %d failed\n", flash_slot);
		ota_state = ota_inactive;
		return(app_action_error);
	}

	return(app_action_ota_commit);
#endif
}

irom app_action_t application_function_flash_info(const string_t *src, string_t *dst)
{
	int ota_available = 0;
	int ota_slots = 0;
	int ota_slot = 0;
	int ota_address_0 = 0;
	int ota_address_1 = 0;
	int ota_address_2 = 0;
	int ota_address_3 = 0;

#if IMAGE_OTA != 0
	rboot_config rcfg = rboot_get_config();

	if((rcfg.magic != BOOT_CONFIG_MAGIC) || (rcfg.count > 3))
	{
		string_append(dst, "ERROR rboot config invalid\n");
		return(app_action_error);
	}

	ota_available = 1;
	ota_slots = rcfg.count;
	ota_slot = rcfg.current_rom;
	ota_address_0 = rcfg.roms[0];
	ota_address_1 = rcfg.roms[1];
	ota_address_2 = rcfg.roms[2];
	ota_address_3 = rcfg.roms[3];
#endif

	string_format(dst, "OK flash function available, sector size: %u bytes, OTA update available: %d, slots: %d, slot: %d, address: %u, address: %u, address: %u, address: %u\n",
			string_size(&ota_receive_buffer),
			ota_available,
			ota_slots, ota_slot,
			ota_address_0, ota_address_1, ota_address_2, ota_address_3);

	return(app_action_normal);
}

static iram unsigned int flash_erase(unsigned int sector_offset, unsigned int sector_count)
{
	unsigned int current;

	for(current = 0; current <= sector_count; current++)
		spi_flash_erase_sector(sector_offset + current);

	return(current - 1);
}

irom app_action_t application_function_flash_erase(const string_t *src, string_t *dst)
{
	int address, length;
	int flash_sector_size = string_size(&ota_receive_buffer);
	int sector_offset, sector_count, count;
	uint32_t time_start, time_finish;

	if(parse_int(1, src, &address, 0, ' ') != parse_ok)
	{
		string_append(dst, "ERROR flash-erase: offset required\n");
		return(app_action_error);
	}

	if(parse_int(2, src, &length, 0, ' ') != parse_ok)
	{
		string_append(dst, "ERROR flash-erase: length required\n");
		return(app_action_error);
	}

	sector_offset = address / flash_sector_size;
	sector_count = length / flash_sector_size;

	if((address % flash_sector_size) != 0)
	{
		sector_offset--;
		sector_count++;
	}

	if((length % flash_sector_size) != 0)
		sector_count++;

	time_start = system_get_time();
	count = flash_erase(sector_offset, sector_count);
	time_finish = system_get_time();

	string_format(dst, "OK flash-erase: erased %d sectors from sector %d, in %d milliseconds\n", count, sector_offset, (time_finish - time_start) / 1000);

	return(app_action_normal);
}

irom app_action_t application_function_flash_send(const string_t *raw_src, string_t *dst)
{
	string_t src = *raw_src;
	int offset, length, chunk_offset, chunk_length;
	int flash_sector_size = string_size(&ota_receive_buffer);

	string_trim_nl(&src);

	if(parse_int(1, &src, &offset, 0, ' ') != parse_ok)
	{
		string_append(dst, "ERROR flash-send: offset required\n");
		return(app_action_error);
	}

	if(parse_int(2, &src, &length, 0, ' ') != parse_ok)
	{
		string_append(dst, "ERROR flash-send: length required\n");
		return(app_action_error);
	}

	if((length != 0) && ((offset % length) != 0))
	{
		string_append(dst, "ERROR: flash-send: chunk offset should be divisible by chunk size");
		return(app_action_error);
	}

	if((length != 0) && ((flash_sector_size % length) != 0))
	{
		string_append(dst, "ERROR: flash-send: chunk length should be divisible by flash sector size");
		return(app_action_error);
	}

	if((offset + length) > string_size(&ota_receive_buffer))
	{
		string_format(dst, "ERROR flash-send: length(%d) + offset(%d) > sector size(%d)\n", offset, length, string_size(&ota_receive_buffer));
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

	string_splice(&ota_receive_buffer, offset, &src, chunk_offset, chunk_length);

	string_format(dst, "OK flash-send: received bytes: %d, at offset: %d\n", length, offset);

	return(app_action_normal);
}

irom app_action_t application_function_flash_receive(const string_t *src, string_t *dst)
{
	int chunk_offset, chunk_length;
	int flash_sector_size = string_size(&ota_receive_buffer);

	if(parse_int(1, src, &chunk_offset, 0, ' ') != parse_ok)
	{
		string_append(dst, "ERROR flash-receive: chunk offset required\n");
		return(app_action_error);
	}

	if(parse_int(2, src, &chunk_length, 0, ' ') != parse_ok)
	{
		string_append(dst, "ERROR flash-receive: chunk chunk_length required\n");
		return(app_action_error);
	}

	if((chunk_length == 0) || ((chunk_offset % chunk_length) != 0))
	{
		string_append(dst, "ERROR: flash-receive: chunk offset should be divisible by chunk size");
		return(app_action_error);
	}

	if((chunk_length == 0) || ((flash_sector_size % chunk_length) != 0))
	{
		string_append(dst, "ERROR: flash-receive: chunk length should be divisible by flash sector size");
		return(app_action_error);
	}

	if((chunk_offset + chunk_length) > string_size(&ota_receive_buffer))
	{
		string_format(dst, "ERROR flash-receive: chunk_length(%d) + chunk_offset(%d) > sector size(%d)\n", chunk_offset, chunk_length, string_size(&ota_receive_buffer));
		return(app_action_error);
	}

	string_setlength(&ota_receive_buffer, string_size(&ota_receive_buffer));

	string_format(dst, "OK flash-receive: sending bytes: %d, from offset: %d, data: @", chunk_length, chunk_offset);
	string_splice(dst, -1, &ota_receive_buffer, chunk_offset, chunk_length);
	string_append(dst, "\n");

	return(app_action_normal);
}

irom app_action_t application_function_flash_read(const string_t *src, string_t *dst)
{
	int address, sector;
	int sector_size = string_size(&ota_receive_buffer);

	SHA_CTX sha_context;
	uint8_t sha_result[SHA_DIGEST_LENGTH];
	string_new(stack, sha_string, SHA_DIGEST_LENGTH * 2);

	if(parse_int(1, src, &address, 0, ' ') != parse_ok)
	{
		string_append(dst, "ERROR flash-read: address required\n");
		return(app_action_error);
	}

	if((address % sector_size) != 0)
	{
		string_append(dst, "ERROR flash-read: address should be divisible by flash sector size");
		return(app_action_error);
	}

	sector = address / sector_size;
	spi_flash_read(sector * sector_size, string_buffer_nonconst(&ota_receive_buffer), sector_size);

	SHA1Init(&sha_context);
	SHA1Update(&sha_context, string_buffer(&ota_receive_buffer), sector_size);
	SHA1Final(sha_result, &sha_context);
	string_bin_to_hex(&sha_string, sha_result, SHA_DIGEST_LENGTH);

	string_format(dst, "OK flash-read: read bytes: %d, from address: %d (%d), checksum: ", sector_size, address, flash_sector);

	string_append_string(dst, &sha_string);
	string_append(dst, "\n");

	return(app_action_normal);
}

irom static app_action_t flash_write_verify_(const string_t *src, string_t *dst, bool_t verify)
{
	int address, sector;
	int sector_size = string_size(&ota_receive_buffer);
	int byte;
	int same = 0;
	int erase = 1;
	const uint8_t *ptr;

	SHA_CTX sha_context;
	uint8_t sha_result[SHA_DIGEST_LENGTH];
	string_new(stack, sha_string, SHA_DIGEST_LENGTH * 2);

	if(parse_int(1, src, &address, 0, ' ') != parse_ok)
	{
		if(verify)
			string_append(dst, "ERROR flash-verify");
		else
			string_append(dst, "ERROR flash-write");

		string_append(dst, ": address required\n");

		return(app_action_error);
	}

	if((address % sector_size) != 0)
	{
		if(verify)
			string_append(dst, "ERROR flash-verify");
		else
			string_append(dst, "ERROR flash-write");

		string_append(dst, ": address should be divisible by flash sector size");

		return(app_action_error);
	}

	sector = address / sector_size;
	spi_flash_read(sector * sector_size, string_buffer_nonconst(dst), sector_size);

	erase = 0;
	same = 0;
	SHA1Init(&sha_context);

	if(verify)
	{
		if(!memcmp(string_buffer(&ota_receive_buffer), string_buffer(dst), sector_size))
			same = 1;

		SHA1Update(&sha_context, string_buffer(dst), sector_size);
	}
	else
	{
		if(memcmp(string_buffer(&ota_receive_buffer), string_buffer(dst), sector_size))
		{
			for(byte = 0, ptr = string_buffer(dst); byte < sector_size; byte++, ptr++)
			{
				if(*ptr != 0xff)
				{
					erase = 1;
					break;
				}
			}

			if(erase)
				spi_flash_erase_sector(sector);

			spi_flash_write(sector * sector_size, string_buffer(&ota_receive_buffer), sector_size);
		}
		else
			same = 1;

		SHA1Update(&sha_context, string_buffer(&ota_receive_buffer), sector_size);
	}

	SHA1Final(sha_result, &sha_context);
	string_bin_to_hex(&sha_string, sha_result, SHA_DIGEST_LENGTH);

	if(verify)
		string_format(dst, "OK flash-verify: verified bytes: %d, at address: %d (%d), same: %d, checksum: ", sector_size, address, flash_sector, same);
	else
		string_format(dst, "OK flash-write: written bytes: %d, to address: %d (%d), same: %d, erased: %d, checksum: ", sector_size, address, flash_sector, same, erase);

	string_append_string(dst, &sha_string);
	string_append(dst, "\n");

	return(app_action_normal);
}

irom app_action_t application_function_flash_write(const string_t *src, string_t *dst)
{
	return(flash_write_verify_(src, dst, false));
}

irom app_action_t application_function_flash_verify(const string_t *src, string_t *dst)
{
	return(flash_write_verify_(src, dst, true));
}

irom app_action_t application_function_flash_checksum(const string_t *src, string_t *dst)
{
	int address, current, length, done;
	int sector_size = string_size(&ota_receive_buffer);

	SHA_CTX sha_context;
	uint8_t sha_result[SHA_DIGEST_LENGTH];
	string_new(stack, sha_string, SHA_DIGEST_LENGTH * 2);

	if(parse_int(1, src, &address, 0, ' ') != parse_ok)
	{
		string_append(dst, "ERROR flash-checksum: address required\n");
		return(app_action_error);
	}

	if(parse_int(2, src, &length, 0, ' ') != parse_ok)
	{
		string_append(dst, "ERROR flash-checksum: length required\n");
		return(app_action_error);
	}

	if((address % sector_size) != 0)
	{
		string_append(dst, "ERROR: flash_checksum: address should be divisible by flash sector size");
		return(app_action_error);
	}

	if((length % sector_size) != 0)
	{
		string_append(dst, "ERROR: flash_checksum: length should be divisible by flash sector size");
		return(app_action_error);
	}

	SHA1Init(&sha_context);

	for(current = address, done = 0; done < length; current += sector_size, done += sector_size)
	{
		spi_flash_read(current, string_buffer_nonconst(dst), sector_size);
		SHA1Update(&sha_context, string_buffer(dst), sector_size);
	}

	SHA1Final(sha_result, &sha_context);
	string_bin_to_hex(&sha_string, sha_result, SHA_DIGEST_LENGTH);

	string_clear(dst);
	string_format(dst, "OK flash-checksum: checksummed bytes: %d, from address: %d, checksum: ", done, address);
	string_append_string(dst, &sha_string);
	string_append(dst, "\n");

	return(app_action_normal);
}

irom app_action_t application_function_flash_select(const string_t *src, string_t *dst)
{
#if IMAGE_OTA == 0
	string_format(dst, "ERROR flash-select: no OTA image\n");
	return(app_action_error);
#else
	int slot;

	rboot_config rcfg = rboot_get_config();

	if(rcfg.magic != BOOT_CONFIG_MAGIC)
	{
		string_append(dst, "ERROR flash-select: rboot config invalid\n");
		return(app_action_error);
	}

	if(parse_int(1, src, &slot, 0, ' ') != parse_ok)
	{
		string_append(dst, "ERROR flash-select: slot required\n");
		return(app_action_error);
	}

	if(slot == rcfg.current_rom)
	{
		string_append(dst, "ERROR flash-select: this slot is already active\n");
		return(app_action_error);
	}

	if((slot < 0) || (slot >= rcfg.count))
	{
		string_format(dst, "ERROR flash-select: invalid slot, valid range = 0 - %d\n", rcfg.count - 1);
		return(app_action_error);
	}

	if(!rboot_set_current_rom(slot))
	{
		string_format(dst, "ERROR: flash-select: set current slot to %d failed\n", slot);
		return(app_action_error);
	}

	rcfg = rboot_get_config();

	if(rcfg.magic != BOOT_CONFIG_MAGIC)
	{
		string_append(dst, "ERROR flash-select: rboot config invalid after write\n");
		return(app_action_error);
	}

	if(rcfg.current_rom != slot)
	{
		string_append(dst, "ERROR flash-select: slot not selected\n");
		return(app_action_error);
	}

	string_format(dst, "OK flash-select: slot %d selected, address %d\n", rcfg.current_rom, rcfg.roms[rcfg.current_rom]);

	return(app_action_normal);
#endif
}
