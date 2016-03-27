#include "display.h"
#include "display_saa.h"
#include "display_lcd.h"

#include "util.h"
#include "stats.h"
#include "config.h"

typedef const struct
{
	int				const size;
	const char *	const name;
	const char *	const type;
	bool_t			(* const init_fn)(void);
	bool_t			(* const set_fn)(int brightness, const char *text);
} display_info_t;

typedef enum
{
	display_saa1064,
	display_lcd,
	display_error,
	display_size = display_error
} display_id_t;

assert_size(display_id_t, 4);

static roflash display_info_t display_info[display_size] =
{
	{
		4, "saa1064", "4 digit led display",
		display_saa1064_init,
		display_saa1064_set,
	},
	{
		80, "hd44780", "4x20 character LCD display",
		display_lcd_init,
		display_lcd_set,
	}
};

static display_data_t display_data[display_size];

static char default_message[display_slot_size + 1] = "";

irom static void display_update(bool_t advance)
{
	const char *display_text;
	int display, slot;
	display_info_t *display_info_entry;
	display_data_t *display_data_entry;
	string_new(static, info_text, 80);

	for(display = 0; display < display_size; display++)
	{
		display_info_entry = &display_info[display];
		display_data_entry = &display_data[display];

		if(display_data_entry->detected)
		{
			for(slot = display_data_entry->current_slot + (advance ? 1 : 0); slot < display_slot_amount; slot++)
				if(display_data_entry->slot[slot].content[0])
					break;

			if(slot >= display_slot_amount)
				for(slot = 0; slot < display_slot_amount; slot++)
					if(display_data_entry->slot[slot].content[0])
						break;

			if(slot < display_slot_amount)
			{
				display_data_entry->current_slot = slot;
				display_text = display_data_entry->slot[slot].content;

				if(!ets_strcmp(display_text, "%%%%"))
				{
					string_clear(&info_text);
					string_format(&info_text, "%02u.%02u %s %s",
							rt_hours, rt_mins, display_info_entry->name, display_info_entry->type);
					display_text = string_to_ptr(&info_text);
				}

				display_info_entry->set_fn(display_data_entry->brightness, display_text);
			}
			else
				display_data_entry->current_slot = 0;
		}
	}
}

irom void display_periodic(void) // call once per second
{
	static int current_scroll = 0;
	int display, slot, active_slots;
	display_data_t *display_data_entry;

	// expiration

	for(display = 0; display < display_size; display++)
	{
		display_data_entry = &display_data[display];

		if(!display_data_entry->detected)
			continue;

		active_slots = 0;

		for(slot = 0; slot < display_slot_amount; slot++)
		{
			if(display_data_entry->slot[slot].timeout > 0)
			{
				if(--display_data_entry->slot[slot].timeout == 0)
					display_data_entry->slot[slot].content[0] = '\0';
			}

			if(display_data_entry->slot[slot].content[0])
				active_slots++;
		}

		if(active_slots == 0)
		{
			strlcpy(display_data_entry->slot[0].content, default_message, sizeof(display_data_entry->slot[0].content));
			current_scroll = 0;
		}
	}

	if(++current_scroll > 10)
	{
		current_scroll = 0;
		display_update(true);
	}
	else
		display_update(false);
}

irom void display_init(void)
{
	display_info_t *display_info_entry;
	display_data_t *display_data_entry;
	int current, slot;

	strlcpy(default_message, config.display_default_msg, sizeof(default_message));

	for(current = 0; current < display_size; current++)
	{
		display_info_entry = &display_info[current];
		display_data_entry = &display_data[current];

		if(display_info_entry->init_fn && (display_info_entry->init_fn()))
			display_data_entry->detected = 1;
		else
			display_data_entry->detected = 0;

		for(slot = 0; slot < display_slot_amount; slot++)
		{
			display_data_entry->current_slot = 0;
			display_data_entry->brightness = 1;
			display_data_entry->slot[slot].timeout = 0;
			display_data_entry->slot[slot].content[0] = '\0';
		}
	}

	display_update(false);
}

irom static void display_setslot(string_t *dst, display_id_t display, int slot, int timeout, const char *text)
{
	display_info_t *display_info_entry;
	display_data_t *display_data_entry;

	if(display >= display_size)
	{
		string_format(dst, "display setslot: display #%d unknown\n", display);
		return;
	}

	display_info_entry = &display_info[display];
	display_data_entry = &display_data[display];

	if(!display_data_entry->detected)
	{
		string_format(dst, "display setslot: display #%d not found\n", display);
		return;
	}

	if(slot > display_slot_amount)
	{
		string_format(dst, "display setslot: slot #%d out of limits\n", slot);
		return;
	}

	strlcpy(display_data_entry->slot[slot].content, text, display_slot_size);
	display_data_entry->slot[slot].timeout = timeout;

	display_update(false);

	string_format(dst, "display setslot: set slot %d on %s to \"%s\"\n",
				slot, display_info_entry->name, display_data_entry->slot[slot].content);
}

irom static bool_t display_set_brightness(display_id_t display, int brightness)
{
	display_data_t *display_data_entry;

	if(brightness > 4)
		return(false);

	if(display >= display_size)
		return(false);

	display_data_entry = &display_data[display];

	if(!display_data_entry->detected)
		return(false);

	display_data_entry->brightness = brightness;
	display_update(false);

	return(true);
}

irom static bool_t display_get_brightness(display_id_t display, int *brightness)
{
	display_data_t *display_data_entry;

	if(display >= display_size)
		return(false);

	display_data_entry = &display_data[display];

	if(!display_data_entry->detected)
		return(false);

	*brightness = display_data_entry->brightness;

	return(true);
}

irom static void display_dump(string_t *dst, int verbose_level)
{
	int display;
	int slot;
	display_info_t *display_info_entry;
	display_data_t *display_data_entry;
	display_slot_t *slot_entry;
	int original_length = string_length(dst);

	for(display = 0; display < display_size; display++)
	{
		display_info_entry = &display_info[display];
		display_data_entry = &display_data[display];

		if((verbose_level > 0) || display_data_entry->detected)
		{
			string_format(dst, "> %c display #%u (%s: %s)\n",
					display_data_entry->detected ? '+' : ' ', display, display_info_entry->name, display_info_entry->type);

			if(verbose_level > 1)
			{
				for(slot = 0; slot < display_slot_amount; slot++)
				{
					slot_entry = &display_data_entry->slot[slot];
					string_format(dst, ">> slot %u: timeout %u, text: \"%s\"\n", slot, slot_entry->timeout, slot_entry->content);
				}
			}
		}
	}

	if(original_length == string_length(dst))
		string_cat(dst, "> no displays found\n");
}

irom app_action_t application_function_display_brightness(const string_t *src, string_t *dst)
{
	int id, value;

	if((parse_int(1, src, &id, 0) != parse_ok) ||
			(parse_int(2, src, &value, 0) != parse_ok) ||
			(value < 0) || (value > 4))
	{
		string_cat(dst, "display-brightness: usage: display_id <brightess>=0,1,2,3,4\n");
		return(app_action_error);
	}

	if(!display_set_brightness(id, value) || !display_get_brightness(id, &value))
	{
		string_format(dst, "display-brightness: invalid display: %d\n", id);
		return(app_action_error);
	}

	string_format(dst, "display %u brightness: %u\n", id, value);

	return(app_action_normal);
}

irom app_action_t application_function_display_dump(const string_t *src, string_t *dst)
{
	int verbose;

	if(parse_int(1, src, &verbose, 0) != parse_ok)
		verbose = 0;

	display_dump(dst, verbose);

	return(app_action_normal);
}

irom app_action_t application_function_display_default_message(const string_t *src, string_t *dst)
{
	const char *text;
	int ws;

	text = string_to_const_ptr(src);

	for(ws = 1; ws > 0; text++)
	{
		if(*text == '\0')
			break;

		if(*text == ' ')
			ws--;
	}

	strlcpy(config.display_default_msg, text, sizeof(config.display_default_msg));
	string_format(dst, "set default display message to \"%s\"\n", config.display_default_msg);

	return(app_action_normal);
}

irom app_action_t application_function_display_set(const string_t *src, string_t *dst)
{
	int id, slot, timeout, current;
	const char *text;

	if((parse_int(1, src, &id, 0) != parse_ok) ||
		(parse_int(2, src, &slot, 0) != parse_ok) ||
		(parse_int(3, src, &timeout, 0) != parse_ok))
	{
		string_cat(dst, "display-set: usage: display_id slot timeout text\n");
		return(app_action_error);
	}

	text = src->buffer;

	for(current = 4; current > 0; text++)
	{
		if(*text == '\0')
			break;

		if(*text == ' ')
			current--;
	}

	display_setslot(dst, id, slot, timeout, text);

	return(app_action_normal);
}
