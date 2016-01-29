#include "ota.h"
#include "util.h"

#include <rboot-api.h>
#include <spi_flash.h>
#include <stdint.h>
#include <stdlib.h>

typedef enum
{
	state_inactive,
	state_write,
	state_verify,
	state_successful
} state_t;

static state_t ota_state = state_inactive;
static int remote_file_length, received, flash_slot,flash_sector;
static MD5_CTX md5;

irom attr_pure bool ota_active(void)
{
	return(ota_state != state_inactive);
}

irom static app_action_t flash_write_verify(string_t *src, string_t *dst)
{
	char *verify_buffer = string_to_ptr(src);

	if(string_size(&buffer_4k) < 0x1000)
	{
		string_cat(dst, "OTA: string write buffer too small\n");
		return(app_action_error);
	}

	if(string_size(src) < 0x1000)
	{
		string_cat(dst, "OTA: string verify buffer too small\n");
		return(app_action_error);
	}

	if(ota_state == state_write)
	{
		spi_flash_read(flash_sector * 0x1000, (void *)verify_buffer, string_length(&buffer_4k));

		if(ets_memcmp(&buffer_4k, verify_buffer, string_length(&buffer_4k)))
		{
			spi_flash_erase_sector(flash_sector);
			spi_flash_write(flash_sector * 0x1000, (void *)&buffer_4k, string_length(&buffer_4k));
		}
	}

	spi_flash_read(flash_sector * 0x1000, (void *)verify_buffer, string_length(&buffer_4k));
	MD5Update(&md5, verify_buffer, string_length(&buffer_4k));

	if(ets_memcmp(&buffer_4k, verify_buffer, string_length(&buffer_4k)))
	{
		string_cat(dst, "OTA: verify mismatch\n");
		return(app_action_error);
	}

	flash_sector++;
	string_clear(&buffer_4k);

	return(app_action_normal);
}

irom static app_action_t ota_start(string_t *src, string_t *dst, bool verify)
{
	rboot_config rcfg;

	if(wlan_scan_active())
	{
		string_cat(dst, "OTA: wlan scan active\n");
		return(app_action_error);
	}

	rcfg = rboot_get_config();

	if(parse_int(1, src, &remote_file_length, 0) != parse_ok)
	{
		string_cat(dst, "OTA: invalid/missing file length\n");
		return(app_action_error);
	}

	if((rcfg.magic != BOOT_CONFIG_MAGIC) || (rcfg.count != 2) || (rcfg.current_rom > 1))
	{
		string_cat(dst, "OTA: rboot config invalid\n");
		return(app_action_error);
	}

	flash_slot = rcfg.current_rom == 0 ? 1 : 0;
	flash_sector = rcfg.roms[flash_slot] / 0x1000;

	received = 0;
	string_clear(&buffer_4k);
	MD5Init(&md5);

	ota_state = verify ? state_verify : state_write;

	string_format(dst, "%s %d %d\n", verify ? "VERIFY" : "WRITE", flash_slot, flash_sector);
	return(app_action_normal);
}

irom app_action_t application_function_ota_write(string_t *src, string_t *dst)
{
	return(ota_start(src, dst, false));
}

irom app_action_t application_function_ota_verify(string_t *src, string_t *dst)
{
	return(ota_start(src, dst, true));
}

irom app_action_t application_function_ota_send(string_t *src, string_t *dst)
{
	int remote_chunk_length, bin_chunk_length, hex_chunk_offset;
	app_action_t action;

	if((ota_state != state_write) && (ota_state != state_verify))
	{
		string_cat(dst, "OTA: not active\n");
		ota_state = state_inactive;
		return(app_action_error);
	}

	if(parse_int(1, src, &remote_chunk_length, 0) != parse_ok)
	{
		string_cat(dst, "OTA: missing chunk length\n");
		ota_state = state_inactive;
		return(app_action_error);
	}

	if((hex_chunk_offset = string_sep(src, 0, 2, ' ')) < 0)
	{
		string_copy(dst, "OTA: missing hex chunk\n");
		ota_state = state_inactive;
		return(app_action_error);
	}

	bin_chunk_length = string_hex_to_bin(&buffer_4k, src, hex_chunk_offset);
	received += bin_chunk_length;

	if(remote_chunk_length != bin_chunk_length)
	{
		string_format(dst, "OTA: chunk size mismatch %u != %u\n", remote_chunk_length, bin_chunk_length);
		ota_state = state_inactive;
		return(app_action_error);
	}

	if(string_length(&buffer_4k) > 0x1000)
	{
		string_format(dst, "OTA: unaligned %u\n", string_length(&buffer_4k));
		ota_state = state_inactive;
		return(app_action_error);
	}

	if((string_length(&buffer_4k) == 0x1000) &&
			((action = flash_write_verify(src, dst)) != app_action_normal))
	{
		ota_state = state_inactive;
		return(action);
	}

	string_format(dst, "ACK %d\n", received);

	return(app_action_normal);
}

irom app_action_t application_function_ota_finish(string_t *src, string_t *dst)
{
	string_new(static, remote_md5_string, 34);
	app_action_t action;

	if((parse_string(1, src, &remote_md5_string)) != parse_ok)
	{
		string_copy(dst, "OTA: missing md5sum string\n");
		ota_state = state_inactive;
		return(app_action_error);
	}

	if((string_length(&buffer_4k) > 0) &&
			((action = flash_write_verify(src, dst)) != app_action_normal))
	{
		ota_state = state_inactive;
		return(action);
	}

	if(remote_file_length != received)
	{
		string_format(dst, "OTA: file size differs: %u != %u\n", remote_file_length, received);
		ota_state = state_inactive;
		return(app_action_error);
	}

	MD5Final(string_to_ptr(dst), &md5);
	string_setlength(dst, 16);
	string_bin_to_hex(src, dst, 0);
	string_clear(dst);

	if(!string_match_string(src, &remote_md5_string))
	{
		string_format(dst, "OTA: invalid md5sum: \"%s\" != \"%s\"\n",
				string_to_ptr(src), string_to_ptr(&remote_md5_string));
		ota_state = state_inactive;
		return(app_action_error);
	}

	string_format(dst, "%s %s\n", ota_state == state_verify ? "VERIFY_OK" : "WRITE_OK", string_to_ptr(src));

	ota_state = state_successful;

	return(app_action_normal);
}

irom app_action_t application_function_ota_commit(string_t *src, string_t *dst)
{
	if(ota_state != state_successful)
	{
		string_cat(dst, "OTA: no candidate for commit\n");
		ota_state = state_inactive;
		return(app_action_error);
	}

	if(!rboot_set_current_rom(flash_slot))
	{
		string_format(dst, "OTA: set current slot to %d failed\n", flash_slot);
		ota_state = state_inactive;
		return(app_action_error);
	}

	return(app_action_reset);
}
