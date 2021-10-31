#include "mailbox.h"
#include "util.h"
#include "sys_string.h"
#include "config.h"
#include "dispatch.h"
#include "rboot-interface.h"
#include "sdk.h"
#include "lwip-interface.h"
#include "display.h"

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

string_new(, mailbox_socket_receive_buffer, 4096);
string_new(, mailbox_socket_send_buffer, 4096);
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

void mailbox_init(unsigned int mailbox_port)
{
	if(string_size(&mailbox_socket_receive_buffer) != SPI_FLASH_SEC_SIZE)
		log("ALERT: mailbox socket receive buffer (%d) != SPI_FLASH_SEC_SIZE (%d)\n", string_size(&mailbox_socket_receive_buffer), SPI_FLASH_SEC_SIZE);

	if(string_size(&mailbox_socket_send_buffer) != SPI_FLASH_SEC_SIZE)
		log("ALERT: mailbox socket send buffer (%d) != SPI_FLASH_SEC_SIZE (%d)\n", string_size(&mailbox_socket_send_buffer), SPI_FLASH_SEC_SIZE);

	lwip_if_socket_create(&mailbox_socket, &mailbox_socket_receive_buffer, &mailbox_socket_send_buffer, mailbox_port,
			true, socket_mailbox_callback_data_received);
}

app_action_t application_function_mailbox_info(string_t *src, string_t *dst)
{
	rboot_if_config_t config;
	rboot_if_rtc_config_t rtc;
	unsigned int mailbox_slot;
	display_properties_t *properties;
	unsigned x, y;

	if(!rboot_if_read_config(&config))
	{
		string_append(dst, "ERROR rboot config invalid\n");
		return(app_action_error);
	}

	if(rboot_if_read_rtc_ram(&rtc))
		mailbox_slot = rtc.last_slot;
	else
		mailbox_slot = config.slot_current;

	if((properties = display_get_properties()))
	{
		x = properties->graphic_dimensions.x;
		y = properties->graphic_dimensions.y;
	}
	else
	{
		x = 0;
		y = 0;
	}

	string_format(dst, "OK mailbox function available, "
				"slots: %u, current: %u, "
				"sectors: [ %u, %u ], display: %ux%upx\n",
			config.slot_count, mailbox_slot,
			config.slots[0] / SPI_FLASH_SEC_SIZE, config.slots[1] / SPI_FLASH_SEC_SIZE,
			x, y);

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

	if(lwip_if_send(&mailbox_socket))
		string_format(dst, "OK mailbox-read: sending sector %u, checksum: %s\n", sector, string_to_cstr(&sha_string));
	else
		string_append(dst, "ERROR mailbox-read: send failed\n");

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
	string_t *buffer_string;
	char *buffer_cstr;
	unsigned int size;

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

	flash_buffer_request(fsb_mailbox, true, "mailbox checksum", &buffer_string, &buffer_cstr, &size);

	if(!buffer_string)
	{
		string_format(dst, "mailbox_checksum: flash buffer busy (%u)\n", flash_buffer_using());
		return(app_action_error);
	}

	SHA1Init(&sha_context);

	for(current = sector; (current - sector) < sectors; current++)
	{
		flash_result = spi_flash_read(current * size, buffer_cstr, size);

		if(flash_result == SPI_FLASH_RESULT_ERR)
		{
			string_append(dst, "ERROR: mailbox-checksum: read error\n");
			flash_buffer_release(fsb_mailbox, "mailbox checksum");
			return(app_action_error);
		}

		if(flash_result == SPI_FLASH_RESULT_TIMEOUT)
		{
			string_append(dst, "ERROR: mailbox-checksum: read timeout\n");
			flash_buffer_release(fsb_mailbox, "mailbox checksum");
			return(app_action_error);
		}

		SHA1Update(&sha_context, buffer_cstr, size);
	}

	flash_buffer_release(fsb_mailbox, "mailbox checksum");

	SHA1Final(sha_result, &sha_context);
	string_bin_to_hex(&sha_string, sha_result, SHA_DIGEST_LENGTH);

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
	string_t *buffer_string;
	char *buffer_cstr;
	unsigned int size;

	if(parse_uint(1, src, &sector, 0, ' ') != parse_ok)
	{
		string_append(dst, "ERROR: mailbox-write: invalid sector\n");
		return(app_action_error);
	}

	flash_buffer_request(fsb_mailbox, true, "mailbox write", &buffer_string, &buffer_cstr, &size);

	if(!buffer_string)
	{
		string_format(dst, "mailbox_write: flash buffer busy (%u)\n", flash_buffer_using());
		return(app_action_error);
	}

	if(string_length(&mailbox_socket_receive_buffer) != (int)size)
	{
		flash_buffer_release(fsb_mailbox, "mailbox write");
		string_format(dst, "ERROR: mailbox-write: mailbox incomplete, %d bytes\n", string_length(&mailbox_socket_receive_buffer));
		return(app_action_error);
	}

	flash_result = spi_flash_read(sector * size, buffer_cstr, size);

	if(flash_result == SPI_FLASH_RESULT_ERR)
	{
		flash_buffer_release(fsb_mailbox, "mailbox write");
		string_append(dst, "ERROR: mailbox-write: read error (check)\n");
		return(app_action_error);
	}

	if(flash_result == SPI_FLASH_RESULT_TIMEOUT)
	{
		flash_buffer_release(fsb_mailbox, "mailbox write");
		string_append(dst, "ERROR: mailbox-write: read timeout (check)\n");
		return(app_action_error);
	}

	erase = 0;
	skip = 1;
	received_buffer = (const uint8_t *)string_buffer(&mailbox_socket_receive_buffer);

	for(unsigned int current = 0; current < SPI_FLASH_SEC_SIZE; current++)
	{
		if(received_buffer[current] != 0xff)
			erase = 1;

		if((unsigned int)received_buffer[current] != (unsigned int)buffer_cstr[current])
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
			flash_buffer_release(fsb_mailbox, "mailbox write");
			string_append(dst, "ERROR: mailbox-write: erase error\n");
			return(app_action_error);
		}

		if(flash_result == SPI_FLASH_RESULT_TIMEOUT)
		{
			flash_buffer_release(fsb_mailbox, "mailbox write");
			string_append(dst, "ERROR: mailbox-write: erase timeout\n");
			return(app_action_error);
		}
	}

	if(!skip)
	{
		flash_result = spi_flash_write(sector * size, received_buffer, size);

		if(flash_result == SPI_FLASH_RESULT_ERR)
		{
			flash_buffer_release(fsb_mailbox, "mailbox write");
			string_append(dst, "ERROR: mailbox-write: read error\n");
			return(app_action_error);
		}

		if(flash_result == SPI_FLASH_RESULT_TIMEOUT)
		{
			flash_buffer_release(fsb_mailbox, "mailbox write");
			string_append(dst, "ERROR: mailbox-write: read timeout\n");
			return(app_action_error);
		}
	}

	flash_result = spi_flash_read(sector * size, buffer_cstr, size);

	if(flash_result == SPI_FLASH_RESULT_ERR)
	{
		flash_buffer_release(fsb_mailbox, "mailbox write");
		string_append(dst, "ERROR: mailbox-write: read error (verify)\n");
		return(app_action_error);
	}

	if(flash_result == SPI_FLASH_RESULT_TIMEOUT)
	{
		flash_buffer_release(fsb_mailbox, "mailbox write");
		string_append(dst, "ERROR: mailbox-write: read timeout (verify)\n");
		return(app_action_error);
	}

	SHA1Init(&sha_context);
	SHA1Update(&sha_context, buffer_cstr, size);
	SHA1Final(sha_result, &sha_context);
	string_bin_to_hex(&sha_string, sha_result, SHA_DIGEST_LENGTH);

	flash_buffer_release(fsb_mailbox, "mailbox write");
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
		success = rboot_if_write_config(&config);

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
