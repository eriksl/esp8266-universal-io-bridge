#include "display.h"
#include "display_saa.h"
#include "display_lcd.h"

#include "util.h"
#include "stats.h"
#include "config.h"

typedef enum
{
	display_slot_amount = 8,
	display_slot_tag_size = 32,
	display_slot_content_size = 64
} display_slot_enum_t;

assert_size(display_slot_enum_t, 4);

typedef enum
{
	display_saa1064,
	display_lcd,
	display_error,
	display_size = display_error
} display_id_t;

assert_size(display_id_t, 4);

typedef const struct
{
	int				const size;
	const char *	const name;
	const char *	const type;
	bool_t			(* const init_fn)(void);
	bool_t			(* const set_fn)(int brightness, const char *tag, const char *text);
	bool_t			(* const show_fn)(void);
} display_info_t;

typedef struct
{
	int	detected;
	int	current_slot;
	int	brightness;
} display_data_t;

typedef struct
{
	int		timeout;
	char	tag[display_slot_tag_size];
	char	content[display_slot_content_size];
} display_slot_t;

static roflash display_info_t display_info[display_size] =
{
	{
		4, "saa1064", "4 digit led display",
		display_saa1064_init,
		display_saa1064_set,
		(void *)0,
	},
	{
		80, "hd44780", "4x20 character LCD display",
		display_lcd_init,
		display_lcd_set,
		display_lcd_show
	}
};

static int page_delay = 0;
static display_data_t display_data;
static display_slot_t display_slot[display_slot_amount];

irom static void display_update(bool_t advance)
{
	const char *display_text;
	int slot;
	display_info_t *display_info_entry;
	string_new(static, tag_text, 32);
	string_new(static, info_text, 64);

	if(display_data.detected < 0)
		return;

	display_info_entry = &display_info[display_data.detected];

	for(slot = display_data.current_slot + (advance ? 1 : 0); slot < display_slot_amount; slot++)
		if(display_slot[slot].content[0])
			break;

	if(slot >= display_slot_amount)
		for(slot = 0; slot < display_slot_amount; slot++)
			if(display_slot[slot].content[0])
				break;

	if(slot >= display_slot_amount)
		slot = 0;

	display_data.current_slot = slot;
	display_text = display_slot[slot].content;

	if(!ets_strcmp(display_text, "%%%%"))
	{
		string_clear(&info_text);
		string_format(&info_text, "%02u.%02u %s %s",
				rt_hours, rt_mins, display_info_entry->name, display_info_entry->type);
		display_text = string_to_ptr(&info_text);
	}

	if(ets_strcmp(display_slot[slot].tag, "-"))
	{
		string_clear(&tag_text);
		string_format(&tag_text, "%02u:%02u ", rt_hours, rt_mins);
		string_cat_ptr(&tag_text, display_slot[slot].tag);
		string_format(&tag_text, " [%u]", slot);
		display_info_entry->set_fn(display_data.brightness, string_to_ptr(&tag_text), display_text);
	}
	else
		display_info_entry->set_fn(display_data.brightness, (char *)0, display_text);
}

irom static void display_expire(void) // call one time per second
{
	int active_slots, slot;

	if(display_data.detected < 0)
		return;

	active_slots = 0;

	for(slot = 0; slot < display_slot_amount; slot++)
	{
		if(display_slot[slot].timeout > 0)
		{
			if(--display_slot[slot].timeout <= 0)
			{
				display_slot[slot].tag[0] = '\0';
				display_slot[slot].content[0] = '\0';
			}
		}

		if(display_slot[slot].content[0])
			active_slots++;
	}

	if(active_slots == 0)
	{
		display_slot[0].timeout = 1;
		strlcpy(display_slot[0].tag, "boot", display_slot_tag_size - 1);
		strlcpy(display_slot[0].content, config.display_default_msg, display_slot_content_size - 1);
	}
}

irom bool display_periodic(void) // gets called 10 times per second
{
	static int expire_counter = 0;
	display_info_t *display_info_entry;

	if(display_data.detected < 0)
		return(false);

	display_info_entry = &display_info[display_data.detected];

	if(++expire_counter > 10) // expire once a second
	{
		display_expire();
		expire_counter = 0;
	}

	if(++page_delay > 40) // 4 seconds for each slot
	{
		page_delay = 0;
		display_update(true);
	}

	if(display_info_entry->show_fn)
		return(display_info_entry->show_fn());

	return(false);
}

irom void display_init(void)
{
	display_info_t *display_info_entry;
	int current, slot;

	display_data.detected = -1;

	for(current = 0; current < display_size; current++)
	{
		display_info_entry = &display_info[current];

		if(display_info_entry->init_fn && (display_info_entry->init_fn()))
		{
			display_data.detected = current;
			break;
		}
	}

	display_data.current_slot = 0;
	display_data.brightness = 1;

	for(slot = 0; slot < display_slot_amount; slot++)
	{
		display_slot[slot].timeout = 0;
		display_slot[slot].tag[0] = '\0';
		display_slot[slot].content[0] = '\0';
	}
}

irom static bool_t display_set_brightness(int brightness)
{
	if(display_data.detected < 0)
		return(false);

	if((brightness < 0) || (brightness > 4))
		return(false);

	display_data.brightness = brightness;
	display_update(false);

	return(true);
}

irom static bool_t display_get_brightness(int *brightness)
{
	if(display_data.detected < 0)
		return(false);

	*brightness = display_data.brightness;

	return(true);
}

irom static void display_dump(string_t *dst)
{
	display_info_t *display_info_entry;
	int slot;

	if(display_data.detected < 0)
	{
		string_cat(dst, "> no displays detected\n");
		return;
	}

	display_info_entry = &display_info[display_data.detected];

	string_format(dst, "> display type #%u (%s: %s)\n", display_data.detected,
			display_info_entry->name, display_info_entry->type);

	for(slot = 0; slot < display_slot_amount; slot++)
		string_format(dst, ">> %c slot %u: timeout %u, tag: \"%s\", text: \"%s\"\n",
				slot == display_data.current_slot ? '+' : ' ',
				slot, display_slot[slot].timeout, display_slot[slot].tag, display_slot[slot].content);
}

irom app_action_t application_function_display_brightness(const string_t *src, string_t *dst)
{
	int value;

	if(parse_int(1, src, &value, 0) != parse_ok)
	{
		string_cat(dst, "display-brightness: usage: <brightess>=0,1,2,3,4\n");
		return(app_action_error);
	}

	if(!display_set_brightness(value) || !display_get_brightness(&value))
	{
		string_format(dst, "display-brightness: no display or invalid brightness value: %d\n", value);
		return(app_action_error);
	}

	string_format(dst, "display brightness: %u\n", value);

	return(app_action_normal);
}

irom app_action_t application_function_display_dump(const string_t *src, string_t *dst)
{
	display_dump(dst);

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

	strlcpy(config.display_default_msg, text, sizeof(config.display_default_msg) - 1);
	string_format(dst, "set default display message to \"%s\"\n", config.display_default_msg);

	return(app_action_normal);
}

irom app_action_t application_function_display_set(const string_t *src, string_t *dst)
{
	int slot, timeout, current;
	const char *text;

	if(display_data.detected < 0)
	{
		string_cat(dst, "display_set: no display detected\n");
		return(app_action_error);
	}

	if((parse_int(1, src, &slot, 0) != parse_ok) ||
		(parse_int(2, src, &timeout, 0) != parse_ok) ||
		(parse_string(3, src, dst) != parse_ok))
	{
		string_clear(dst);
		string_cat(dst, "display-set: usage: slot timeout tag text\n");
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

	if(current > 0)
	{
		string_clear(dst);
		string_cat(dst, "display-set: usage: slot timeout tag TEXT\n");
		return(app_action_error);
	}

	if(slot > display_slot_amount)
	{
		string_clear(dst);
		string_format(dst, "display-set: slot #%d out of limits\n", slot);
		return(app_action_error);
	}

	strlcpy(display_slot[slot].tag, string_to_ptr(dst), display_slot_tag_size - 1);
	strlcpy(display_slot[slot].content, text, display_slot_content_size - 1);
	display_slot[slot].timeout = timeout;

	display_update(false);
	string_clear(dst);

	string_format(dst, "display-set: set slot %d with tag %s to \"%s\"\n",
				slot, display_slot[slot].tag,
				display_slot[slot].content);

	return(app_action_normal);
}
