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

static state_t state = state_inactive;
static unsigned int remote_file_length, received;
static MD5_CTX md5;
static char md5_sum[16];
static char md5_string[33];
static int flash_slot;
static unsigned int flash_sector;
static char flash_buffer[0x1000];
static char verify_buffer[0x1000];
static int flash_buffer_offset;

irom static app_action_t flash_write_verify(application_parameters_t *ap)
{
	if(state == state_write)
	{
		spi_flash_erase_sector(flash_sector);
		spi_flash_write(flash_sector * 0x1000, (void *)flash_buffer, flash_buffer_offset);
	}

	spi_flash_read(flash_sector * 0x1000, (void *)verify_buffer, flash_buffer_offset);
	MD5Update(&md5, verify_buffer, flash_buffer_offset);

	if(memcmp(flash_buffer, verify_buffer, flash_buffer_offset))
	{
		state = state_inactive;
		return(app_action_error);
	}

	received += flash_buffer_offset;
	flash_sector++;
	flash_buffer_offset = 0;

	return(app_action_normal);
}

irom static app_action_t ota_start(application_parameters_t *ap, bool verify)
{
	rboot_config rcfg;

	rcfg = rboot_get_config();
	remote_file_length = string_to_int((*ap->args)[1]);

	if((rcfg.magic != BOOT_CONFIG_MAGIC) || (rcfg.count != 2) || (rcfg.current_rom > 1))
	{
		snprintf(ap->dst, ap->size, "OTA: rboot config invalid\n");
		state = state_inactive;
		return(app_action_error);
	}

	flash_slot = rcfg.current_rom == 0 ? 1 : 0;
	flash_sector = rcfg.roms[flash_slot] / 0x1000;

	received = 0;
	flash_buffer_offset = 0;
	MD5Init(&md5);

	state = verify ? state_verify : state_write;

	snprintf(ap->dst, ap->size, "%s %d %d\n", verify ? "VERIFY" : "WRITE", flash_slot, flash_sector);
	return(app_action_normal);
}

irom app_action_t application_function_ota_write(application_parameters_t ap)
{
	return(ota_start(&ap, false));
}

irom app_action_t application_function_ota_verify(application_parameters_t ap)
{
	return(ota_start(&ap, true));
}

irom app_action_t application_function_ota_send(application_parameters_t ap)
{
	const char *hex_chunk;
	int remote_chunk_length, hex_chunk_length, bin_chunk_length;
	int skip_ws;
	app_action_t action;

	if((state != state_write) && (state != state_verify))
	{
		snprintf(ap.dst, ap.size, "OTA: not active\n");
		state = state_inactive;
		return(app_action_error);
	}

	remote_chunk_length = string_to_int((*ap.args)[1]);
	hex_chunk_length = strlen(ap.cmdline);
	hex_chunk = ap.cmdline;

	for(skip_ws = 2; skip_ws > 0; hex_chunk++, hex_chunk_length--)
	{
		if(*hex_chunk == '\0')
			break;

		if(*hex_chunk == ' ')
			skip_ws--;
	}

	bin_chunk_length = hex_to_bin(hex_chunk_length, hex_chunk,
			sizeof(flash_buffer) - flash_buffer_offset, flash_buffer + flash_buffer_offset);

	if(remote_chunk_length != bin_chunk_length)
	{
		snprintf(ap.dst, ap.size, "OTA: chunk size mismatch %u != %u\n", remote_chunk_length, bin_chunk_length);
		state = state_inactive;
		return(app_action_error);
	}

	flash_buffer_offset += bin_chunk_length;

	if(flash_buffer_offset > 0x1000)
	{
		snprintf(ap.dst, ap.size, "OTA: unaligned %u\n", flash_buffer_offset);
		state = state_inactive;
		return(app_action_error);
	}

	if((flash_buffer_offset == 0x1000) && ((action = flash_write_verify(&ap)) != app_action_normal))
		return(action);

	snprintf(ap.dst, ap.size, "ACK\n");

	return(app_action_normal);
}

irom app_action_t application_function_ota_finish(application_parameters_t ap)
{
	const char *md5_string_remote;
	int skip_ws;
	app_action_t action;

	md5_string_remote = ap.cmdline;

	for(skip_ws = 1; skip_ws > 0; md5_string_remote++)
	{
		if(*md5_string_remote == '\0')
			break;

		if(*md5_string_remote == ' ')
			skip_ws--;
	}

	if((flash_buffer_offset > 0) && ((action = flash_write_verify(&ap)) != app_action_normal))
		return(action);

	MD5Final(md5_sum, &md5);
	md5_hash_to_string(md5_sum, sizeof(md5_string), md5_string);

	if(remote_file_length != received)
	{
		snprintf(ap.dst, ap.size, "OTA: file size differs: %u != %u\n", remote_file_length, received);
		state = state_inactive;
		return(app_action_error);
	}

	if(strcmp(md5_string, md5_string_remote))
	{
		snprintf(ap.dst, ap.size, "OTA: invalid md5sum: \"%s\" != \"%s\"\n", md5_string, md5_string_remote);
		state = state_inactive;
		return(app_action_error);
	}

	snprintf(ap.dst, ap.size, "%s %s\n", state == state_verify ? "VERIFY_OK" : "WRITE_OK", md5_string);

	state = state_successful;

	return(app_action_normal);
}

irom app_action_t application_function_ota_commit(application_parameters_t ap)
{
	if(state != state_successful)
	{
		snprintf(ap.dst, ap.size, "OTA: no candidate for commit\n");
		state = state_inactive;
		return(app_action_error);
	}

	if(!rboot_set_current_rom(flash_slot))
	{
		snprintf(ap.dst, ap.size, "OTA: set current slot to %d failed\n", flash_slot);
		state = state_inactive;
		return(app_action_error);
	}

	return(app_action_reset);
}
