#include "ota.h"
#include "util.h"
#include "config.h"

#if IMAGE_OTA == 1
#include <rboot-api.h>
#endif
#include <spi_flash.h>
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
