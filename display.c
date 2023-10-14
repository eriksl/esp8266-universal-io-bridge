#include "display.h"
#include "display_lcd.h"
#include "display_orbital.h"
#include "display_cfa634.h"
#include "display_eastrising.h"
#include "display_ssd1306.h"
#include "display_spitft.h"

#include "util.h"
#include "stats.h"
#include "config.h"
#include "sys_time.h"
#include "ota.h"
#include "dispatch.h"
#include "wlan.h"

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

enum
{
	display_slot_amount = 8,
	display_slot_content_size = 12 * 36,
};

static uint8_t display_current_slot;

assert_size(display_current_slot, 1);

typedef struct
{
	int		timeout;
	char	content[display_slot_content_size];
} display_slot_t;

typedef enum attr_packed
{
	pls_idle,
	pls_run,
} picture_load_state_t;

static picture_load_state_t picture_load_state = pls_idle;
assert_size(picture_load_state, 1);

static uint8_t picture_load_slot;
assert_size(picture_load_slot, 1);

static uint8_t picture_load_sector;
assert_size(picture_load_sector, 1);

static unsigned int freeze_timer;
assert_size(freeze_timer, 4);

roflash static display_hooks_t const *const display_hooks[] =
{
	&display_hooks_lcd,
	&display_hooks_orbital,
	&display_hooks_cfa,
	&display_hooks_eastrising,
	&display_hooks_ssd1306,
	&display_hooks_spitft,
	(const display_hooks_t *)0
};

static const display_hooks_t *display_hooks_active = (const display_hooks_t *)0;
attr_align_int uint8_t display_buffer[display_buffer_size]; // maybe used as array of ints
static unsigned int flip_timeout;
static display_slot_t display_slot[display_slot_amount];

attr_pure bool display_detected(void)
{
	return(!!display_hooks_active);
}

bool display_get_info(display_info_t *info)
{
	if(!display_hooks_active || !display_hooks_active->info_fn)
		return(false);

	info->name[0] = '\0';
	info->columns = 0;
	info->rows = 0;
	info->cell_width = 0;
	info->cell_height = 0;
	info->width = 0;
	info->height = 0;
	info->pixel_mode = display_pixel_mode_none;

	return(display_hooks_active->info_fn(info));
}

static bool freeze(unsigned int timeout_ms)
{
	if(!display_hooks_active)
		return(false);

	freeze_timer = timeout_ms;

	if(display_hooks_active->freeze_fn)
		return(display_hooks_active->freeze_fn(freeze_timer ? true : false));

	return(true);
}

static bool display_plot(unsigned int pixels_amount, int x, int y, string_t *pixels)
{
	if(!display_hooks_active || !display_hooks_active->plot_fn)
		return(false);

	return(display_hooks_active->plot_fn(pixels_amount, x, y, pixels));
}

bool display_load_picture_slot(unsigned int slot)
{
	display_info_t info;

	if(picture_load_state != pls_idle)
		return(false);

	if(!display_hooks_active || !display_hooks_active->plot_fn || !display_hooks_active->info_fn)
		return(false);

	if(!display_hooks_active->info_fn(&info))
		return(false);

	if((info.width < 1) ||
			(info.height < 1) ||
			(info.pixel_mode == display_pixel_mode_none))
		return(false);

	if(slot > 1)
		return(false);

	picture_load_state = pls_run;
	picture_load_sector = 0;
	picture_load_slot = slot;
	dispatch_post_task(task_prio_low, task_display_load_picture_worker, 0, 0, 0);
	freeze(10000);

	return(true);
}

void display_picture_load_worker(void)
{
	display_info_t info;
	string_t *buffer_string;
	char *buffer_cstr;
	unsigned int flash_buffer_size;
	unsigned int width, height;
	display_pixel_mode_t pixel_mode;
	unsigned int current_pixel;
	int current_x, current_y;
	unsigned int sector_base;
	bool finish;
	int length;
	unsigned int total_pixels;

	stat_display_picture_load_worker_called++;

	if(picture_load_state != pls_run)
		goto error;

	if(!display_hooks_active || !display_hooks_active->info_fn)
		goto error;

	if(!display_hooks_active->info_fn(&info))
		goto error;

	width = info.width;
	height = info.height;
	pixel_mode = info.pixel_mode;

	flash_buffer_request(fsb_display_picture, false, "display load picture worker", &buffer_string, &buffer_cstr, &flash_buffer_size);

	if(!buffer_string)
		goto retry; // buffer currently in use, try again later

	sector_base = (picture_load_slot ? PICTURE_FLASH_OFFSET_1 : PICTURE_FLASH_OFFSET_0) / SPI_FLASH_SEC_SIZE;

	if(spi_flash_read((sector_base + picture_load_sector) * flash_buffer_size, buffer_cstr, flash_buffer_size) != SPI_FLASH_RESULT_OK)
	{
		log("display load picture: failed to read sector: 0x%x (0x%x)\n", picture_load_sector, sector_base + picture_load_sector);
		goto error;
	}

	string_setlength(buffer_string, flash_buffer_size);

	total_pixels = (width * height);

	switch(pixel_mode)
	{
		case(display_pixel_mode_1): // monochrome, 1 bit per pixel
		{
			current_pixel = picture_load_sector * flash_buffer_size * 8;
			current_y = current_pixel / width;
			current_x = current_pixel % width;

			finish = false;

			if((current_pixel + (string_length(buffer_string) * 8)) >= total_pixels)
			{
				length = (total_pixels - current_pixel) / 8;

				if(length < 0)
					length = 0;

				string_setlength(buffer_string, length);

				finish = true;
			}

			if(!display_plot(string_length(buffer_string) * 8, current_x, current_y, buffer_string))
				goto error;

			if(finish)
				goto error;

			break;
		}

		case(display_pixel_mode_16_rgb): // 16 bit RGB 5-6-5 per pixel
		{
			current_pixel = picture_load_sector * flash_buffer_size / 2;
			current_y = current_pixel / width;
			current_x = current_pixel % width;

			finish = false;

			if((current_pixel + (string_length(buffer_string) / 2)) >= total_pixels)
			{
				length = (total_pixels - current_pixel) * 2;

				if(length < 0)
					length = 0;

				string_setlength(buffer_string, length);

				finish = true;
			}

			if(!display_plot(string_length(buffer_string) / 2, current_x, current_y, buffer_string))
				goto error;

			if(finish)
				goto error;

			break;
		}

		case(display_pixel_mode_24_rgb): // 24 bit RGB 8-8-8 per pixel
		{
			current_pixel = picture_load_sector * flash_buffer_size / 3;
			current_y = current_pixel / width;
			current_x = current_pixel % width;

			finish = false;

			if((current_pixel + (string_length(buffer_string) / 3)) >= total_pixels)
			{
				length = (total_pixels - current_pixel) * 3;

				if(length < 0)
					length = 0;

				string_setlength(buffer_string, length);

				finish = true;
			}

			if(!display_plot(string_length(buffer_string) / 3, current_x, current_y, buffer_string))
				goto error;

			if(finish)
				goto error;

			break;
		}

		default: // unknown
		{
			log("picture load: pixel mode of %u not implemented\n", pixel_mode);
			goto error;
		}
	}

	picture_load_sector++;

retry:
	dispatch_post_task(task_prio_low, task_display_load_picture_worker, 0, 0, 0);
	goto no_error;
error:
	picture_load_state = pls_idle;
	freeze(0);
no_error:
	if(flash_buffer_using_1(fsb_display_picture))
		flash_buffer_release(fsb_display_picture, "display load picture worker");
	return;
}

static void display_update(bool dont_advance)
{
	char *slot_content;
	unsigned int attempt;
	uint64_t start, spent;
	unsigned int unicode[display_slot_content_size];
	unsigned int length;
	unsigned int run;
	display_info_t info;

	if(!display_hooks_active || !display_hooks_active->info_fn || !display_hooks_active->begin_fn || !display_hooks_active->end_fn)
		return;

	if(!display_hooks_active->info_fn(&info))
		return;

	start = time_get_us();

	if(config_flags_match(flag_display_clock) && display_hooks_active->show_time_start_fn && display_hooks_active->show_time_stop_fn)
	{
		static bool time_shown = false;

		if(!time_shown)
		{
			unsigned int h, m;

			time_get(&h, &m, (unsigned int *)0, (unsigned int *)0, (unsigned int *)0, (unsigned int *)0);

			if(!display_hooks_active->show_time_start_fn(h, m))
			{
				log("display update: show time start failed\n");
				goto error;
			}

			time_shown = true;
			goto done;
		}
		else
		{
			if(!display_hooks_active->show_time_stop_fn())
			{
				log("display update: time show stop failed\n");
				goto error;
			}

			time_shown = false;
		}
	}

	for(attempt = display_slot_amount; attempt > 0; attempt--)
	{
		if(!dont_advance)
		{
			display_current_slot++;
			dont_advance = false;
		}

		for(run = 0; run < 2; run++)
		{
			for(; display_current_slot < display_slot_amount; display_current_slot++)
				if(display_slot[display_current_slot].content[0])
					goto active_slot_found;

			display_current_slot = 0;
		}

		display_current_slot = 0;

active_slot_found:
		slot_content = display_slot[display_current_slot].content;

		if(!strncmp(slot_content, "picture-", 8))
		{
			unsigned int picture_slot = slot_content[8] == '0' ? 0 : 1;

			if(display_load_picture_slot(picture_slot))
				goto done;
		}

		if(!strcmp(slot_content, "boot\n%%%%"))
		{
			string_new(, id, 32);
			config_get_string("identification", &id, -1, -1);

			snprintf(slot_content, display_slot_content_size, "boot\n%s\n%ux%u, %ux%u@%u",
					info.name,
					info.columns,
					info.rows,
					info.width,
					info.height,
					info.pixel_mode);
		}

		if(!display_hooks_active->begin_fn(display_current_slot, false))
		{
			log("display update: display begin failed\n");
			goto error;
		}

		length = utf8_to_unicode(slot_content, sizeof(unicode) / sizeof(*unicode), unicode);

		if(!display_hooks_active->output_fn(length, unicode))
		{
			log("display update: display output (2) failed\n");
			goto error;
		}

		if(!display_hooks_active->end_fn())
		{
			log("display update: display end failed\n");
			goto error;
		}

		goto done;
	}

	display_current_slot = 0;
	log("display update: no more attempts left\n");

done:
	spent = time_get_us() - start;

	stat_display_update_max_us = umax(stat_display_update_max_us, spent);
	stat_display_update_min_us = umin(stat_display_update_min_us, spent);

	return;

error:
	display_hooks_active = (const display_hooks_t *)0;
}

void display_periodic(void) // gets called 10 times per second
{
	static unsigned int last_update = 0;
	static unsigned int expire_counter = 0;
	unsigned int now, active_slots, slot;
	bool log_to_display;
	unsigned int unicode;

	if(!display_hooks_active || !display_hooks_active->output_fn)
		return;

	if(freeze_timer > 0)
		if(freeze_timer <= 100)
			freeze(0);
		else
			freeze_timer -= 100;
	else
		(void)0;

	if(picture_load_state != pls_idle)
	{
		display_picture_load_worker(); // fallback for missed task posts
		return;
	}

	if(config_flags_match(flag_log_to_display))
		log_to_display = true;
	else
		log_to_display = !wlan_associated();

	if(log_to_display)
	{
		static unsigned int skip = 0;
		static bool newline_seen = true;
		uint8_t current;

		if(newline_seen)
		{
			skip = (config_flags_match(flag_log_date) ? 6 : 0) + (config_flags_match(flag_log_time) ? 6 : 0);
			newline_seen = false;
		}

		while(logbuffer_display_current < (unsigned int)string_length(&logbuffer))
		{
			current = string_at(&logbuffer, logbuffer_display_current++);

			if(skip == 0)
			{
				switch(current)
				{
					case '\n':
					case ' ' ... '}':
					{
						unicode = current;
						break;
					}

					default:
					{
						unicode = '*';
						break;
					}
				}

				if(!display_hooks_active->output_fn(1, &unicode))
				{
					log("display update: display output (3) failed\n");
					goto error;
				}
			}
			else
				skip--;

			if(current == '\n')
			{
				newline_seen = true;
				break;
			}
		}

		return;
	}

	now = time_get_us() / 1000000;

	if(++expire_counter > 10) // expire and update once a second
	{
		expire_counter = 0;
		active_slots = 0;

		for(slot = 0; slot < display_slot_amount; slot++)
		{
			if(display_slot[slot].timeout == 1)
				display_slot[slot].content[0] = '\0';

			if(display_slot[slot].timeout > 0)
				display_slot[slot].timeout--;

			if(display_slot[slot].content[0])
				active_slots++;
		}

		if(active_slots == 0)
		{
			display_slot[0].timeout = 1;
			strecpy(display_slot[0].content, "boot\n%%%%", display_slot_content_size);
		}

		if((last_update > now) || ((last_update + flip_timeout) < now))
		{
			last_update = now;
			if(freeze_timer == 0)
				display_update(false);
		}
	}

	return;

error:
	display_hooks_active = (const display_hooks_t *)0;
	return;
}

void display_init(void)
{
	unsigned int current;
	unsigned int slot;

	for(current = 0; display_hooks[current]; current++)
	{
		display_hooks_active = display_hooks[current];

		if(display_hooks_active->init_fn && display_hooks_active->init_fn())
			break;
	}

	if(!display_hooks[current])
	{
		display_hooks_active = (const display_hooks_t *)0;
		goto error;
	}

	display_current_slot = 0;

	for(slot = 0; slot < display_slot_amount; slot++)
	{
		display_slot[slot].timeout = 0;
		display_slot[slot].content[0] = '\0';
	}

	if(!config_get_uint("display.fliptimeout", &flip_timeout, -1, -1))
		flip_timeout = 4;

	// for log to display

	if(!display_hooks_active->begin_fn(0, true))
	{
		log("display init: display begin failed\n");
		goto error;
	}

	return;

error:
	display_hooks_active = (const display_hooks_t *)0;
	return;
}

static void display_dump(string_t *dst)
{
	unsigned int slot;
	int ix;
	char current;
	unsigned int newlines_pending;
	display_info_t info;

	if(!display_hooks_active || !display_hooks_active->info_fn || !display_hooks_active->info_fn(&info))
	{
		string_append(dst, "> no displays detected\n");
		return;
	}

	string_format(dst, "> display type: %s\n", info.name);

	string_format(dst, "> capabilities: graphical dimensions: %u x %u, text dimensions: %u x %u, colour depth: %u\n",
			info.width, info.height,
			info.columns, info.rows,
			info.pixel_mode);

	if(config_flags_match(flag_log_to_display))
	{
		string_format(dst, "> display is set up to receive logging\n");
		return;
	}

	string_format(dst, "> display update time, min: %u us, max: %u us\n",
		stat_display_update_min_us,
		stat_display_update_max_us);

	for(slot = 0; slot < display_slot_amount; slot++)
	{
		string_format(dst, "\n> %c slot %u: timeout %d, length: %u",
				slot == display_current_slot ? '+' : ' ', slot, display_slot[slot].timeout, strlen(display_slot[slot].content));

		for(ix = 0, newlines_pending = 1; ix < display_slot_content_size; ix++)
		{
			if(!(current = display_slot[slot].content[ix]))
				break;

			if(current == '\n')
			{
				newlines_pending++;
				continue;
			}

			if(newlines_pending > 0)
			{
				string_append_chars(dst, '\n', newlines_pending);
				string_format(dst, "    ");
				newlines_pending = 0;
			}

			if((current < ' ') || (current > '}'))
				current = '_';

			string_append_char(dst, current);
		}

		string_append(dst, "\n");
	}
}

app_action_t application_function_display_dump(app_params_t *parameters)
{
	display_dump(parameters->dst);

	return(app_action_normal);
}

app_action_t application_function_display_flip_timeout(app_params_t *parameters)
{
	unsigned int timeout;

	if(parse_uint(1, parameters->src, &timeout, 0, ' ') == parse_ok)
	{
		if((timeout < 1) || (timeout > 60))
		{
			string_format(parameters->dst, "> invalid timeout: %u\n", timeout);
			return(app_action_error);
		}

		if(!config_open_write())
		{
			string_append(parameters->dst, "> cannot set config (open)\n");
			return(app_action_error);
		}

		if(timeout == 4)
		{
			if(!config_delete("display.fliptimeout", false, -1, -1))
			{
				config_abort_write();
				string_append(parameters->dst, "> cannot delete config (default values)\n");
				return(app_action_error);
			}
		}
		else
			if(!config_set_int("display.fliptimeout", timeout, -1, -1))
			{
				config_abort_write();
				string_append(parameters->dst, "> cannot set config\n");
				return(app_action_error);
			}

		if(!config_close_write())
		{
			string_append(parameters->dst, "> cannot set config (close)\n");
			return(app_action_error);
		}
	}

	if(!config_get_uint("display.fliptimeout", &timeout, -1, -1))
		timeout = 4;

	flip_timeout = timeout;

	string_format(parameters->dst, "> timeout: %u s\n", flip_timeout);

	return(app_action_normal);
}

app_action_t application_function_display_brightness(app_params_t *parameters)
{
	unsigned int value;

	if(!display_hooks_active)
	{
		string_append(parameters->dst, "display_brightess: no display detected\n");
		return(app_action_error);
	}

	if(parse_uint(1, parameters->src, &value, 0, ' ') != parse_ok)
	{
		string_append(parameters->dst, "display-brightness: usage: <brightness>=0,1,2,3,4\n");
		return(app_action_error);
	}

	if(!display_hooks_active->bright_fn || !display_hooks_active->bright_fn(value))
	{
		string_format(parameters->dst, "display-brightness: invalid brightness value: %u\n", value);
		return(app_action_error);
	}

	string_format(parameters->dst, "display brightness: %u\n", value);

	return(app_action_normal);
}

app_action_t application_function_display_set(app_params_t *parameters)
{
	int user_slot, timeout, from, to;
	unsigned int slot;
	char current;
	bool cleared = false;

	if(!display_hooks_active)
	{
		string_append(parameters->dst, "display_set: no display detected\n");
		return(app_action_error);
	}

	if((parse_int(1, parameters->src, &user_slot, 0, ' ') != parse_ok) ||
			(parse_int(2, parameters->src, &timeout, 0, ' ') != parse_ok))
		goto usage;

	if((from = string_sep(parameters->src, 0, 3, ' ')) < 0)
		goto usage;

	if(user_slot < 0)
	{
		for(slot = 0; slot < display_slot_amount; slot++)
		{
			display_slot[slot].content[0] = '\0';
			display_slot[slot].timeout = 0;
		}

		cleared = true;
		slot = 0;
	}
	else
		slot = (unsigned int)user_slot;

	if(slot >= display_slot_amount)
	{
		string_format(parameters->dst, "display-set: slot #%d out of limits\n", user_slot);
		return(app_action_error);
	}

	for(to = 0; (to + 1) < display_slot_content_size; from++)
	{
		if(!(current = string_at(parameters->src, from)))
			break;

		if((current == '\\') && (string_length(parameters->src) > from) && (string_at(parameters->src, from + 1) == 'n'))
		{
			from++;
			current = '\n';
		}

		display_slot[slot].content[to++] = current;
	}

	display_slot[slot].content[to] = '\0';
	display_slot[slot].timeout = timeout;

	if(cleared)
		display_update(true);

	string_clear(parameters->dst);

	string_format(parameters->dst, "display-set: set slot %u to \"%s\"\n", slot, display_slot[slot].content);

	return(app_action_normal);

usage:
	string_append(parameters->dst, "display-set: usage: slot timeout text\n");
	return(app_action_error);
}

app_action_t application_function_display_picture_load(app_params_t *parameters)
{
	unsigned int entry;
	bool rv;

	if(!parse_uint(1, parameters->src, &entry, 0, ' ') == parse_ok)
		entry = 0;

	if(entry > 1)
	{
		string_append(parameters->dst, "picture load: usage: [slot (0/1)]\n");
		return(app_action_error);
	}

	rv = display_load_picture_slot(entry);

	string_format(parameters->dst, "picture load success: %s\n", yesno(rv));

	return(app_action_normal);
}

app_action_t application_function_display_plot(app_params_t *parameters)
{
	bool rv;
	unsigned int x, y, pixels;

	if(!display_hooks_active)
	{
		string_append(parameters->dst, "display plot: no display detected\n");
		return(app_action_error);
	}

	if((parse_uint(1, parameters->src, &pixels, 0, ' ') != parse_ok) || (parse_uint(2, parameters->src, &x, 0, ' ') != parse_ok) || (parse_uint(3, parameters->src, &y, 0, ' ') != parse_ok))
	{
		string_append(parameters->dst, "usage: display plot <pixels> <x> <y>\n");
		return(app_action_error);
	}

	if(string_length(parameters->src_oob) == 0)
	{
		string_append(parameters->dst, "display-plot: missing data\n");
		return(app_action_error);
	}

	rv = display_plot(pixels, x, y, parameters->src_oob);

	string_format(parameters->dst, "display plot success: %s\n", yesno(rv));

	return(app_action_normal);
}

app_action_t application_function_display_freeze(app_params_t *parameters)
{
	unsigned int timeout;

	if(!display_hooks_active)
	{
		string_append(parameters->dst, "display freeze: no display detected\n");
		return(app_action_error);
	}

	if(parse_uint(1, parameters->src, &timeout, 0, ' ') != parse_ok)
	{
		string_append(parameters->dst, "usage: display_freeze <timeout ms>");
		return(app_action_error);
	}

	string_format(parameters->dst, "display freeze success: %s", yesno(freeze(timeout)));
	return(app_action_normal);
}
