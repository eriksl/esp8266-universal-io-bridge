#include "display.h"
#include "display_saa.h"
#include "display_lcd.h"
#include "display_orbital.h"
#include "display_cfa634.h"
#include "display_seeed.h"
#include "display_eastrising.h"
#include "display_ssd1306.h"

#include "util.h"
#include "sys_string.h"
#include "stats.h"
#include "config.h"
#include "sys_time.h"
#include "sys_string.h"
#include "mailbox.h"

#include <stdint.h>
#include <stdbool.h>

enum
{
	display_slot_amount = 8,
	display_slot_tag_size = 16,
	display_slot_content_lines = 3,
	display_slot_content_line_length = 27,
	display_slot_content_size = (display_slot_content_lines * (display_slot_content_line_length + 1)),
};

static unsigned int display_current_slot;

typedef struct
{
	int		timeout;
	char	tag[display_slot_tag_size];
	char	content[display_slot_content_size];
} display_slot_t;

assert_size(display_slot_t, 104);

roflash static display_info_t const *const display_info[] =
{
	&display_info_saa1064,
	&display_info_lcd,
	&display_info_orbital,
	&display_info_cfa,
	&display_info_seeed,
	&display_info_eastrising,
	&display_info_ssd1306,
	(const display_info_t *)0
};

static const display_info_t *display_info_active = (const display_info_t *)0;

attr_align_int uint8_t display_buffer[display_buffer_size]; // maybe used as array of ints
static unsigned int flip_timeout;
static display_slot_t display_slot[display_slot_amount];
static unsigned int display_layer = 0;

assert_size(display_slot, 832);

attr_pure bool display_detected(void)
{
	return(!!display_info_active);
}

static void display_update(bool dont_advance)
{
	const char *slot_content;
	unsigned int attempt;
	uint64_t start, spent;
	string_new(, tag_string, display_slot_content_line_length * 2);
	string_new(, info_text, display_slot_content_size * 2);
	unsigned int unicode[display_slot_content_size];
	unsigned int length;
	unsigned int run;

	if(!display_detected())
		return;

	start = time_get_us();

	if(config_flags_match(flag_display_clock) && display_info_active->show_time_start_fn && display_info_active->show_time_stop_fn)
	{
		static bool time_shown = false;

		if(!time_shown)
		{
			unsigned int h, m;

			time_get(&h, &m, (unsigned int *)0, (unsigned int *)0, (unsigned int *)0, (unsigned int *)0);

			if(!display_info_active->show_time_start_fn(h, m))
			{
				log("display update: show time start failed\n");
				goto error;
			}

			time_shown = true;
			goto done;
		}
		else
		{
			if(!display_info_active->show_time_stop_fn())
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

		if(!strcmp(slot_content, "picture") &&
			!strcmp(display_slot[display_current_slot].tag, "picture"))

		{
			if(display_info_active->layer_select_fn &&
				display_info_active->picture_valid_fn &&
				display_info_active->picture_valid_fn())
			{
				if(!display_info_active->layer_select_fn(1))
				{
					log("display update: display layer select (1) failed\n");
					goto error;
				}

				goto done;
			}

			continue;
		}

		if(display_info_active->layer_select_fn && !display_info_active->layer_select_fn(0))
		{
			log("display update: display layer select (2) failed\n");
			goto error;
		}

		if(!strcmp(slot_content, "%%%%"))
		{
			config_get_string("identification", &info_text, -1, -1);
			string_format(&info_text, "\n%s\n%s", display_info_active->name, display_info_active->description);
			slot_content = string_to_cstr(&info_text);
		}

		unsigned int hour, minute, month, day;

		string_clear(&tag_string);
		time_get(&hour, &minute, 0, 0, &month, &day);
		string_format(&tag_string, "%02u:%02u %02u/%02u ", hour, minute, day, month);
		string_append_cstr_flash(&tag_string, display_slot[display_current_slot].tag);

		if(!display_info_active->begin_fn(display_current_slot, false))
		{
			log("display update: display begin failed\n");
			goto error;
		}

		if(display_info_active->standout_fn && !display_info_active->standout_fn(1))
		{
			log("display update: display standout (1) failed\n");
			goto error;
		}

		length = utf8_to_unicode(string_to_cstr(&tag_string), sizeof(unicode) / sizeof(*unicode), unicode);

		for(unsigned int ix = 0; ix < length; ix++)
		{
			if(!display_info_active->output_fn(unicode[ix]))
			{
				log("display update: display output (0) failed\n");
				goto error;
			}
		}

		if(!display_info_active->output_fn('\n'))
		{
			log("display update: display output (1) failed\n");
			goto error;
		}

		if(display_info_active->standout_fn && !display_info_active->standout_fn(0))
		{
			log("display update: display standout (1) failed\n");
			goto error;
		}

		length = utf8_to_unicode(slot_content, sizeof(unicode) / sizeof(*unicode), unicode);

		for(unsigned int ix = 0; ix < length; ix++)
		{
			if(!display_info_active->output_fn(unicode[ix]))
			{
				log("display update: display output (2) failed\n");
				goto error;
			}
		}

		if(!display_info_active->end_fn())
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
	display_info_active = (const display_info_t *)0;
}

void display_periodic(void) // gets called 10 times per second
{
	static unsigned int last_update = 0;
	static unsigned int expire_counter = 0;
	unsigned int now, active_slots, slot;
	static bool display_initial_log_active = true;
	static unsigned int display_initial_log_counter = 0;
	bool log_to_display;

	if(!display_detected())
		return;

	if(display_info_active->periodic_fn && !display_info_active->periodic_fn())
	{
		log("display update: display periodic failed\n");
		goto error;
	}

	if(config_flags_match(flag_log_to_display))
		log_to_display = true;
	else
		if((display_initial_log_active) && (display_initial_log_counter++ < 100))
			log_to_display = true;
		else
			log_to_display = false;

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
				if(!display_info_active->output_fn(current))
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
			{
				display_slot[slot].tag[0] = '\0';
				display_slot[slot].content[0] = '\0';
			}

			if(display_slot[slot].timeout > 0)
				display_slot[slot].timeout--;

			if(display_slot[slot].content[0])
				active_slots++;
		}

		if(active_slots == 0)
		{
			display_slot[0].timeout = 1;
			strecpy(display_slot[0].tag, "boot", display_slot_tag_size);
			strecpy(display_slot[0].content, "%%%%", display_slot_content_size);
		}

		if((last_update > now) || ((last_update + flip_timeout) < now))
		{
			last_update = now;
			display_update(false);
		}
	}

	return;

error:
	display_info_active = (const display_info_t *)0;
	return;
}

void display_init(void)
{
	unsigned int current;
	unsigned int slot;
	unsigned int picture_autoload_index;

	for(current = 0; display_info[current]; current++)
	{
		display_info_active = display_info[current];

		if(display_info_active->init_fn && display_info_active->init_fn())
			break;
	}

	if(!display_info[current])
	{
		display_info_active = (const display_info_t *)0;
		goto error;
	}

	display_current_slot = 0;

	for(slot = 0; slot < display_slot_amount; slot++)
	{
		display_slot[slot].timeout = 0;
		display_slot[slot].tag[0] = '\0';
		display_slot[slot].content[0] = '\0';
	}

	if(!config_get_uint("display.fliptimeout", &flip_timeout, -1, -1))
		flip_timeout = 4;

	// for log to display

	if(!display_info_active->begin_fn(0, true))
	{
		log("display init: display begin failed\n");
		goto error;
	}

	if(display_info_active->standout_fn && !display_info_active->standout_fn(0))
	{
		log("display init: display standout failed\n");
		goto error;
	}

	if(display_info_active->picture_load_fn &&
			config_get_uint("picture.autoload", &picture_autoload_index, -1, -1) &&
			(picture_autoload_index < 2) &&
			!display_info_active->picture_load_fn(picture_autoload_index))
	{
		log("display update: display picture autoload failed\n");
		goto error;
	}

	return;

error:
	display_info_active = (const display_info_t *)0;
	return;
}

static void display_dump(string_t *dst)
{
	unsigned int slot;
	int ix;
	char current;
	unsigned int newlines_pending;

	if(!display_detected())
	{
		string_append(dst, "> no displays detected\n");
		return;
	}

	string_format(dst, "> display type %s: %s\n",
			display_info_active->name, display_info_active->description);

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
		string_format(dst, "\n> %c slot %u: timeout %d, tag: \"%s\", length: %u",
				slot == display_current_slot ? '+' : ' ', slot, display_slot[slot].timeout, display_slot[slot].tag, strlen(display_slot[slot].content));

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

app_action_t application_function_display_dump(string_t *src, string_t *dst)
{
	display_dump(dst);

	return(app_action_normal);
}

app_action_t application_function_display_flip_timeout(string_t *src, string_t *dst)
{
	unsigned int timeout;

	if(parse_uint(1, src, &timeout, 0, ' ') == parse_ok)
	{
		if((timeout < 1) || (timeout > 60))
		{
			string_format(dst, "> invalid timeout: %u\n", timeout);
			return(app_action_error);
		}

		if(!config_open_write())
		{
			string_append(dst, "> cannot set config (open)\n");
			return(app_action_error);
		}

		if(timeout == 4)
		{
			if(!config_delete("display.fliptimeout", false, -1, -1))
			{
				config_abort_write();
				string_append(dst, "> cannot delete config (default values)\n");
				return(app_action_error);
			}
		}
		else
			if(!config_set_int("display.fliptimeout", timeout, -1, -1))
			{
				config_abort_write();
				string_append(dst, "> cannot set config\n");
				return(app_action_error);
			}

		if(!config_close_write())
		{
			string_append(dst, "> cannot set config (close)\n");
			return(app_action_error);
		}
	}

	if(!config_get_uint("display.fliptimeout", &timeout, -1, -1))
		timeout = 4;

	flip_timeout = timeout;

	string_format(dst, "> timeout: %u s\n", flip_timeout);

	return(app_action_normal);
}

app_action_t application_function_display_brightness(string_t *src, string_t *dst)
{
	unsigned int value;

	if(!display_detected())
	{
		string_append(dst, "display_brightess: no display detected\n");
		return(app_action_error);
	}

	if(parse_uint(1, src, &value, 0, ' ') != parse_ok)
	{
		string_append(dst, "display-brightness: usage: <brightness>=0,1,2,3,4\n");
		return(app_action_error);
	}

	if(!display_info_active->bright_fn || !display_info_active->bright_fn(value))
	{
		string_format(dst, "display-brightness: invalid brightness value: %u\n", value);
		return(app_action_error);
	}

	string_format(dst, "display brightness: %u\n", value);

	return(app_action_normal);
}

app_action_t application_function_display_set(string_t *src, string_t *dst)
{
	int user_slot, timeout, from, to;
	unsigned int slot;
	char current;
	bool cleared = false;

	if(!display_detected())
	{
		string_append(dst, "display_set: no display detected\n");
		return(app_action_error);
	}

	if((parse_int(1, src, &user_slot, 0, ' ') != parse_ok) ||
		(parse_int(2, src, &timeout, 0, ' ') != parse_ok) ||
		(parse_string(3, src, dst, ' ') != parse_ok))
	{
		string_clear(dst);
		string_append(dst, "display-set: usage: slot timeout tag text\n");
		return(app_action_error);
	}

	if((from = string_sep(src, 0, 4, ' ')) < 0)
	{
		string_clear(dst);
		string_append(dst, "display-set: missing text; usage: slot timeout tag text\n");
		return(app_action_error);
	}

	if(user_slot < 0)
	{
		for(slot = 0; slot < display_slot_amount; slot++)
		{
			display_slot[slot].tag[0] = '\0';
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
		string_clear(dst);
		string_format(dst, "display-set: slot #%d out of limits\n", user_slot);
		return(app_action_error);
	}

	strecpy(display_slot[slot].tag, string_to_cstr(dst), display_slot_tag_size);

	for(to = 0; (to + 1) < display_slot_content_size; from++)
	{
		if(!(current = string_at(src, from)))
			break;

		if((current == '\\') && (string_length(src) > from) && (string_at(src, from + 1) == 'n'))
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

	string_clear(dst);

	string_format(dst, "display-set: set slot %u with tag %s to \"%s\"\n",
				slot, display_slot[slot].tag,
				display_slot[slot].content);

	return(app_action_normal);
}

app_action_t application_function_display_picture_switch_layer(string_t *src, string_t *dst)
{
	unsigned int layer;

	if(!display_detected())
	{
		string_append(dst, "display layer: no display detected\n");
		return(app_action_error);
	}

	if(!display_info_active->layer_select_fn)
	{
		string_append(dst, "display layer: no layer support\n");
		return(app_action_error);
	}

	if(parse_uint(1, src, &layer, 0, ' ') != parse_ok)
		layer = display_layer ? 0 : 1;

	if(!display_info_active->layer_select_fn(layer))
	{
		string_append(dst, "display-layer: select layer failed\n");
		return(app_action_error);
	}

	display_layer = layer;

	string_format(dst, "display-layer: layer %u selected\n", layer);
	return(app_action_normal);
}

app_action_t application_function_display_picture_load(string_t *src, string_t *dst)
{
	unsigned int entry;
	bool rv;

	if(!display_detected())
	{
		string_append(dst, "picture load: no display detected\n");
		return(app_action_error);
	}

	if(!display_info_active->picture_load_fn)
	{
		string_append(dst, "picture load: not supported\n");
		return(app_action_error);
	}

	if((!parse_uint(1, src, &entry, 0, ' ') == parse_ok) && (!config_get_uint("picture.autoload", &entry, -1, -1)))
		entry = 0;

	if(entry > 1)
	{
		string_append(dst, "picture load: usage: [entry (0/1)]\n");
		config_abort_write();
		return(app_action_error);
	}

	rv = display_info_active->picture_load_fn(entry);

	string_format(dst, "picture load success: %s\n", yesno(rv));

	return(app_action_normal);
}

app_action_t application_function_display_picture_autoload(string_t *src, string_t *dst)
{
	int entry;

	if(!config_open_write())
	{
		string_append(dst, "picture set autoload: open config failed\n");
		return(app_action_error);
	}

	if(parse_int(1, src, &entry, 0, ' ') == parse_ok)
	{
		if((entry == 0) || (entry == 1))
		{
			if(!config_set_uint("picture.autoload", entry, -1, -1))
			{
				string_append(dst, "picture set autoload: config set failed\n");
				config_abort_write();
				return(app_action_error);
			}
		}
		else
		{
			if(entry < -1)
			{
				string_append(dst, "picture set autoload: usage: [entry (-1[off]/0/1)]\n");
				config_abort_write();
				return(app_action_error);
			}
			else
				config_delete("picture.autoload", false, -1, -1);
		}
	}

	if(!config_close_write())
	{
		string_append(dst, "picture set autoload: write config failed\n");
		return(app_action_error);
	}

	if(!config_get_int("picture.autoload", &entry, -1, -1))
	{
		string_append(dst, "picture set autoload: not set\n");
		return(app_action_normal);
	}

	string_format(dst, "picture set autoload: active for entry %d\n", entry);
	return(app_action_normal);
}

app_action_t application_function_display_canvas_start(string_t *src, string_t *dst)
{
	bool rv;
	unsigned int timeout;

	if(!display_detected())
	{
		string_append(dst, "display canvas start: no display detected\n");
		return(app_action_error);
	}

	if(parse_uint(1, src, &timeout, 0, ' ') != parse_ok)
	{
		string_append(dst, "usage: display canvas start <timeout_ms>");
		return(app_action_error);
	}

	if(!display_info_active->canvas_start_fn)
	{
		string_append(dst, "display canvas start: not supported\n");
		return(app_action_error);
	}

	string_clear(&mailbox_socket_receive_buffer);

	rv = display_info_active->canvas_start_fn(timeout);

	string_format(dst, "display canvas start success: %s\n", yesno(rv));

	return(app_action_normal);
}

app_action_t application_function_display_canvas_goto(string_t *src, string_t *dst)
{
	bool rv;
	unsigned int x, y;

	if(!display_detected())
	{
		string_append(dst, "display canvas start: no display detected\n");
		return(app_action_error);
	}

	if((parse_uint(1, src, &x, 0, ' ') != parse_ok) || (parse_uint(1, src, &y, 0, ' ') != parse_ok))
	{
		string_append(dst, "usage: display canvas goto <x> <y>");
		return(app_action_error);
	}

	if(!display_info_active->canvas_goto_fn)
	{
		string_append(dst, "display canvas goto: not supported\n");
		return(app_action_error);
	}

	rv = display_info_active->canvas_goto_fn(x, y);

	string_format(dst, "display canvas goto success: %s\n", yesno(rv));

	return(app_action_normal);
}

app_action_t application_function_display_canvas_plot(string_t *src, string_t *dst)
{
	bool rv;
	unsigned int x, y, pixels;

	if(!display_detected())
	{
		string_append(dst, "display canvas plot: no display detected\n");
		goto error;
	}

	if((parse_uint(1, src, &pixels, 0, ' ') != parse_ok) || (parse_uint(2, src, &x, 0, ' ') != parse_ok) || (parse_uint(3, src, &y, 0, ' ') != parse_ok))
	{
		string_append(dst, "usage: display canvas plot <pixels> <x> <y>");
		goto error;
	}

	if(!display_info_active->canvas_plot_fn || !display_info_active->canvas_goto_fn)
	{
		string_append(dst, "display canvas plot: not supported\n");
		goto error;
	}

	if((unsigned int)string_length(&mailbox_socket_receive_buffer) < (pixels * 2))
	{
		string_format(dst, "display canvas plot: incorrect pixel amount: %u/%d\n", pixels, string_length(&mailbox_socket_receive_buffer) / 2);
		goto error;
	}

	string_setlength(&mailbox_socket_receive_buffer, pixels * 2);

	if(!display_info_active->canvas_goto_fn(x, y))
	{
		string_append(dst, "display canvas plot: goto failed\n");
		goto error;
	}

	rv = display_info_active->canvas_plot_fn(&mailbox_socket_receive_buffer);

	string_clear(&mailbox_socket_receive_buffer);
	string_format(dst, "display canvas plot success: %s\n", yesno(rv));

	return(app_action_normal);

error:
	string_clear(&mailbox_socket_receive_buffer);
	return(app_action_error);
}

app_action_t application_function_display_canvas_show(string_t *src, string_t *dst)
{
	bool rv;

	if(!display_detected())
	{
		string_append(dst, "display canvas show: no display detected\n");
		return(app_action_error);
	}

	if(!display_info_active->canvas_show_fn)
	{
		string_append(dst, "display canvas show: not supported\n");
		return(app_action_error);
	}

	rv = display_info_active->canvas_show_fn();

	string_format(dst, "display canvas show success: %s\n", yesno(rv));
	return(app_action_normal);
}

app_action_t application_function_display_canvas_stop(string_t *src, string_t *dst)
{
	bool rv;

	if(!display_detected())
	{
		string_append(dst, "display canvas stop: no display detected\n");
		return(app_action_error);
	}

	if(!display_info_active->canvas_stop_fn)
	{
		string_append(dst, "display canvas stop: not supported\n");
		return(app_action_error);
	}

	rv = display_info_active->canvas_stop_fn();

	string_format(dst, "display canvas stop success: %s\n", yesno(rv));
	return(app_action_normal);
}
