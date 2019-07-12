#include "display.h"
#include "display_saa.h"
#include "display_lcd.h"
#include "display_orbital.h"
#include "display_cfa634.h"

#include "util.h"
#include "sys_string.h"
#include "stats.h"
#include "config.h"
#include "sys_time.h"
#include "sys_string.h"

#include <stdint.h>
#include <stdbool.h>

enum
{
	display_slot_amount = 8,
	display_slot_tag_size = 12,
	display_slot_content_size = 64
};

enum
{
	display_saa1064 = 0,
	display_lcd = 1,
	display_orbital = 2,
	display_cfa634 = 3,
	display_error,
	display_size = display_error
};

typedef enum
{
	display_generic,
	display_text_4x20,
} display_type_t;

typedef enum
{
	u8p_state_base,
	u8p_state_utf8_byte_3,
	u8p_state_utf8_byte_2,
	u8p_state_utf8_byte_1,
	u8p_state_output,
} utf8_parser_state_t;

typedef const struct
{
	display_type_t	const type;
	const char *	const name;
	const char *	const description;
	bool			(* const init_fn)(void);
	bool			(* const bright_fn)(int brightness);
	void			(* const begin_fn)(void);
	void			(* const output_fn)(unsigned int);
	void			(* const end_fn)(void);
} display_info_t;

assert_size(display_info_t, 32);

typedef struct
{
	int	detected;
	int	current_slot;
} display_data_t;

assert_size(display_data_t, 8);

typedef struct
{
	int		timeout;
	char	tag[display_slot_tag_size];
	char	content[display_slot_content_size];
} display_slot_t;

assert_size(display_slot_t, 80);

static unsigned int flip_timeout;

roflash static display_info_t display_info[display_size] =
{
	{
		display_text_4x20,
		"saa1064", "4 digit led display",
		display_saa1064_init,
		display_saa1064_bright,
		display_saa1064_begin,
		display_saa1064_output,
		display_saa1064_end,
	},
	{
		display_text_4x20,
		"hd44780", "4x20 character LCD display",
		display_lcd_init,
		display_lcd_bright,
		display_lcd_begin,
		display_lcd_output,
		display_lcd_end,
	},
	{
		display_text_4x20,
		"matrix orbital", "4x20 character VFD display",
		display_orbital_init,
		display_orbital_bright,
		display_orbital_begin,
		display_orbital_output,
		display_orbital_end,
	},
	{
		display_text_4x20,
		"cfa634", "4x20 character serial LCD display",
		display_cfa634_init,
		display_cfa634_bright,
		display_cfa634_begin,
		display_cfa634_output,
		display_cfa634_end,
	},
	/*display_generic,*/
};

static display_data_t display_data;
static display_slot_t display_slot[display_slot_amount];

assert_size(display_slot, 640);

attr_pure bool display_detected(void)
{
	return(display_data.detected >= 0);
}

static void display_update(bool advance)
{
	const char *display_text, *tag_text, *current_text;
	int slot;
	unsigned int hour, minute, month, day;
	unsigned int utf8, unicode;
	utf8_parser_state_t state;
	display_info_t *display_info_entry;
	string_new(, tag_string, 32);
	string_new(, info_text, 64);

	if(!display_detected())
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

	time_get(&hour, &minute, 0, 0, &month, &day);

	if(!strcmp(display_text, "%%%%"))
	{
		config_get_string("identification", &info_text, -1, -1);
		string_format(&info_text, "\n%s\n%s", display_info_entry->name, display_info_entry->description);
		display_text = string_to_cstr(&info_text);
	}

	if(strcmp(display_slot[slot].tag, "-"))
	{
		string_format(&tag_string, "%02u:%02u %02u/%02u ", hour, minute, day, month);
		string_append_cstr_flash(&tag_string, display_slot[slot].tag);
	}

	display_info_entry->begin_fn();

	tag_text = string_to_cstr(&tag_string);

	if(tag_text && *tag_text)
		current_text = tag_text;
	else
		current_text = display_text;

	state = u8p_state_base;
	unicode = 0;

	while(current_text)
	{
		utf8 = *current_text++;

		if(!utf8)
		{
			current_text = display_text;
			display_text = (const char *)0;
			display_info_entry->output_fn('\n');
			state = u8p_state_base;
			continue;
		}

		switch(state)
		{
			case u8p_state_base:
			{
				if((utf8 & 0xe0) == 0xc0) // first of two bytes (11 bits)
				{
					unicode = utf8 & 0x1f;
					state = u8p_state_utf8_byte_1;
				}
				else
					if((utf8 & 0xf0) == 0xe0) // first of three bytes (16 bits)
					{
						unicode = utf8 & 0x0f;
						state = u8p_state_utf8_byte_2;
					}
					else
						if((utf8 & 0xf8) == 0xf0) // first of four bytes (21 bits)
						{
							unicode = utf8 & 0x07;
							state = u8p_state_utf8_byte_3;
						}
						else
							if((utf8 & 0x80) == 0x80)
								log("utf8 parser: invalid utf8, bit 7 set: %x %c\n", utf8, (int)utf8);
							else
							{
								unicode = utf8 & 0x7f;
								state = u8p_state_output;
							}

				break;
			}

			case u8p_state_utf8_byte_3 ... u8p_state_utf8_byte_1:
			{
				if((utf8 & 0xc0) == 0x80) // following bytes
				{
					unicode = (unicode << 6) | (utf8 & 0x3f);
					state++;
				}
				else
				{
					log("utf8 parser: invalid utf8, no prefix on following byte, state: %u: %x %c\n", state, utf8, (int)utf8);
					state = u8p_state_base;
				}

				break;
			}

			case u8p_state_output:
			{
				break;
			}
		}

		if(state == u8p_state_output)
		{
			display_info_entry->output_fn(unicode);
			state = u8p_state_base;
		}
	}

	display_info_entry->end_fn();
}

	}
}

bool display_periodic(void) // gets called 10 times per second
{
	static unsigned int last_update = 0;
	static unsigned int expire_counter = 0;
	unsigned int now, active_slots, slot;
	string_new(, default_message, 64);

	if(!display_detected())
		return(false);

	now = system_get_time() / 1000000;

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

			if(!config_get_string("display.defaultmsg", &default_message, -1, -1))
			{
				string_clear(&default_message);
				string_append(&default_message, "%%%%");
			}

			strecpy(display_slot[0].content, string_to_cstr(&default_message), display_slot_content_size);
		}

		if((last_update > now) || ((last_update + flip_timeout) < now))
		{
			last_update = now;
			display_update(true);
		}
	}

	return(false);
}

void display_init(void)
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

	for(slot = 0; slot < display_slot_amount; slot++)
	{
		display_slot[slot].timeout = 0;
		display_slot[slot].tag[0] = '\0';
		display_slot[slot].content[0] = '\0';
	}

	if(!config_get_uint("display.fliptimeout", &flip_timeout, -1, -1))
		flip_timeout = 4;
}

static void display_dump(string_t *dst)
{
	display_info_t *display_info_entry;
	int slot, ix;
	char current;
	unsigned int newlines_pending;

	if(!display_detected())
	{
		string_append(dst, "> no displays detected\n");
		return;
	}

	display_info_entry = &display_info[display_data.detected];

	string_format(dst, "> display type #%d (%s: %s)\n", display_data.detected,
			display_info_entry->name, display_info_entry->description);

	for(slot = 0; slot < display_slot_amount; slot++)
	{
		string_format(dst, "\n> %c slot %d: timeout %d, tag: \"%s\", length: %u",
				slot == display_data.current_slot ? '+' : ' ', slot, display_slot[slot].timeout, display_slot[slot].tag, strlen(display_slot[slot].content));

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

app_action_t application_function_display_default_message(string_t *src, string_t *dst)
{
	int ix;
	string_new(, message, 64);

	if((ix = string_sep(src, 0, 1, ' ')) > 0)
	{
		string_splice(&message, 0, src, string_length(src) - ix - 1, -1);

		if(!config_open_write() ||
				!config_set_string("display.defaultmsg", string_to_cstr(&message), -1, -1) ||
				!config_close_write())
		{
			config_abort_write();
			string_append(dst, "> cannot set config\n");
			return(app_action_error);
		}
	}

	string_clear(dst);

	if(config_get_string("display.defaultmsg", dst, -1, -1) && string_match_cstr(dst, "%%%%"))
	{
		if(!config_open_write() ||
				!config_delete("display.defaultmsg", false, -1, -1) ||
				!config_close_write())
		{
			config_abort_write();
			string_append(dst, "> cannot delete config (default values)\n");
			return(app_action_error);
		}
	}

	string_clear(dst);
	string_append(dst, "set default display message to \"");

	if(!config_get_string("display.defaultmsg", dst, -1, -1))
		string_append(dst, "%%%%");

	string_append(dst, "\"\n");

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
	display_info_t *display_info_entry;

	if(!display_detected())
	{
		string_append(dst, "display_brightess: no display detected\n");
		return(app_action_error);
	}

	display_info_entry = &display_info[display_data.detected];

	if(parse_uint(1, src, &value, 0, ' ') != parse_ok)
	{
		string_append(dst, "display-brightness: usage: <brightness>=0,1,2,3,4\n");
		return(app_action_error);
	}

	if(!display_info_entry->bright_fn || !display_info_entry->bright_fn(value))
	{
		string_format(dst, "display-brightness: invalid brightness value: %u\n", value);
		return(app_action_error);
	}

	string_format(dst, "display brightness: %u\n", value);

	return(app_action_normal);
}

app_action_t application_function_display_set(string_t *src, string_t *dst)
{
	int slot, timeout, from, to;
	char current;

	if(!display_detected())
	{
		string_append(dst, "display_set: no display detected\n");
		return(app_action_error);
	}

	if((parse_int(1, src, &slot, 0, ' ') != parse_ok) ||
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

	if(slot < 0)
	{
		for(slot = 0; slot < display_slot_amount; slot++)
		{
			display_slot[slot].tag[0] = '\0';
			display_slot[slot].content[0] = '\0';
			display_slot[slot].timeout = 0;
		}

		slot = 0;
	}

	if(slot >= display_slot_amount)
	{
		string_clear(dst);
		string_format(dst, "display-set: slot #%d out of limits\n", slot);
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

	display_update(false);
	string_clear(dst);

	string_format(dst, "display-set: set slot %d with tag %s to \"%s\"\n",
				slot, display_slot[slot].tag,
				display_slot[slot].content);

	return(app_action_normal);
}
