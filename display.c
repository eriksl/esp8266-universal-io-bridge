#include "display.h"

#include "util.h"
#include "i2c.h"
#include "stats.h"

struct display_data_struct
{
	uint8_t			const size;
	const char *	const name;
	const char *	const type;
	bool_t			(* const init_fn)(void);
	bool_t			(* const set_fn)(struct display_data_struct *, const char *text);

	unsigned int	detected:1;
	uint8_t			current_slot;
	uint8_t			brightness;
	display_slot_t	slot[display_slot_amount];
};

typedef struct display_data_struct display_data_t;

/*
	+--1--+
	|     |
	2     0
	|     |
	+--3--+
	|     |
	7     5
	|     |
	+--6--+ +4
			++

	0	0x01
	1	0x02
	2	0x04
	3	0x08
	4	0x10
	5	0x20
	6	0x40
	7	0x80

*/

static const uint8_t led_charrom[] =
{
	0x00,		/*	32	SPC	*/
	0x31,		/*	33	!	*/
	0x05,		/*	34	"	*/
	0x00,		/*	35	#	*/
	0x6e,		/*	36	$	*/
	0x24,		/*	37	%	*/
	0x29,		/*	38	&	*/
	0x0f,		/*	39	´	*/
	0xc6,		/*	40	(	*/
	0x63,		/*	41	)	*/
	0x8c,		/*	42	*	*/
	0x29,		/*	43	+	*/
	0x10,		/*	44	,	*/
	0x08,		/*	45	-	*/
	0x10,		/*	46	.	*/
	0x89,		/*	47	/	*/
	0xe7,		/*	48	0	*/
	0x21,		/*	49	1	*/
	0xcb,		/*	50	2	*/
	0x6b,		/*	51	3	*/
	0x2d,		/*	52	4	*/
	0x6e,		/*	53	5	*/
	0xee,		/*	54	6	*/
	0x23,		/*	55	7	*/
	0xef,		/*	56	8	*/
	0x6f,		/*	57	9	*/
	0x21,		/*	58	:	*/
	0x61,		/*	59	;	*/
	0xc6,		/*	60	<	*/
	0x48,		/*	61	=	*/
	0x63,		/*	62	>	*/
	0x33,		/*	63	?	*/
	0xff,		/*	64	@	*/
	0xeb,		/*	65	A	*/
	0xec,		/*	66	B	*/
	0xc8,		/*	67	C	*/
	0xe9,		/*	68	D	*/
	0xcf,		/*	69	E	*/
	0x8e,		/*	70	F	*/
	0x6f,		/*	71	G	*/
	0xac,		/*	72	H	*/
	0x21,		/*	73	I	*/
	0x61,		/*	74	J	*/
	0x8c,		/*	75	K	*/
	0xc4,		/*	76	L	*/
	0xaa,		/*	77	M	*/
	0xa8,		/*	78	N	*/
	0xe8,		/*	79	O	*/
	0x8f,		/*	80	P	*/
	0x2f,		/*	81	Q	*/
	0x88,		/*	82	R	*/
	0x6e,		/*	83	S	*/
	0xcc,		/*	84	T	*/
	0xe5,		/*	85	U	*/
	0xe5,		/*	86	V	*/
	0xed,		/*	87	W	*/
	0xad,		/*	88	X	*/
	0x6d,		/*	89	Y	*/
	0xcb,		/*	90	Z	*/
	0xc6,		/*	91	[	*/
	0x2c,		/*	92	\	*/
	0x63,		/*	93	]	*/
	0x02,		/*	94	^	*/
	0x40,		/*	95	_	*/
};

static display_data_t device_data[display_size];
static char default_message[display_slot_size] = "";

static irom unsigned int led_render_char(unsigned int character)
{
	unsigned int add_dot = 0;

	if(character & 0x80)
	{
		add_dot = 0x10;
		character &= ~0x80;
	}

	if(character & 0x40)
		character &= ~0x20;	// maps CAPS characters to lowercase characters

	if(character > 0x60)
		return(0x10);

	if(character < 0x20)
		return(0x10);

	character -= 0x20;		// skip control characters 0x00 - 0x20

	if(character >= sizeof(led_charrom))
		return(0xff);		// this should never happen

	return(led_charrom[character] | add_dot);
}

static irom bool_t display_saa1064_init(void)
{
	uint8_t i2cdata;

	if(i2c_receive(0x38, 1, &i2cdata) != i2c_error_ok)
	{
		i2c_reset();
		return(false);
	}

	if((i2cdata & 0x7f) != 0x00)
	{
		i2c_reset();
		return(false);
	}

	if(i2c_send_2(0x38, 0x00, 0x07) != i2c_error_ok)
	{
		i2c_reset();
		return(false);
	}

	return(true);
}

static irom bool_t display_saa1064_set(display_data_t *display, const char *from)
{
	static uint8_t bright_to_saa[5] =
	{
		0x00, 0x10, 0x30, 0x50, 0x70
	};

	uint8_t text[4];
	uint8_t i2cdata[6];
	unsigned int current;

	strcpy(text,  "    ");

	for(current = 0; *from && (current < sizeof(text)); from++)
	{
		if((*from == '.') && (current > 0))
			text[current - 1] |= 0x80;
		else
			text[current++] = *from;
	}

	i2cdata[0] = 0x00;	// start at control register (0x00),
						// followed by four digits segments registers (0x01-0x04)
	i2cdata[1] = 0x07;	// multiplex mode, enable all digits, no test mode
	i2cdata[1] |= bright_to_saa[display->brightness];

	for(current = 0; current < 4; current++)
		i2cdata[2 + current] = 0x00;

	for(current = 0; (current < 4) && text[current]; current++)
		i2cdata[5 - current] = led_render_char(text[current]); // reverse digit's position

	if(i2c_send(0x38, 6, i2cdata) != i2c_error_ok)
	{
		i2c_reset();
		return(false);
	}

	return(true);
}

static display_data_t display_data[display_size] =
{
	{
		4, "saa1064", "4 digit led display",
		display_saa1064_init,
		display_saa1064_set,
		0, 0, 0,
		{ }
	}
};

irom static void display_update(bool advance)
{
	char info_text[16];
	const char *display_text;
	unsigned int display;
	unsigned int slot;
	display_data_t *display_entry;

	for(display = 0; display < display_size; display++)
	{
		display_entry = &display_data[display];

		if(display_entry->detected)
		{
			for(slot = display_entry->current_slot + (advance ? 1 : 0); slot < display_slot_amount; slot++)
				if(display_entry->slot[slot].content[0])
					break;

			if(slot >= display_slot_amount)
				for(slot = 0; slot < display_slot_amount; slot++)
					if(display_entry->slot[slot].content[0])
						break;

			if(slot < display_slot_amount)
			{
				display_entry->current_slot = slot;
				display_text = display_entry->slot[slot].content;

				if(!strcmp(display_text, "%%%%"))
				{
					snprintf(info_text, sizeof(info_text), "%02u.%02u %s %s",
							rt_hours, rt_mins, display_entry->name, display_entry->type);
					display_text = info_text;
				}

				display_entry->set_fn(display_entry, display_text);
			}
			else
				display_entry->current_slot = 0;
		}
	}
}

irom void display_periodic(void) // call once per second
{
	static unsigned int current_scroll = 0;
	unsigned int display;
	unsigned int slot;
	unsigned int active_slots;
	display_data_t *display_entry;

	// expiration

	for(display = 0; display < display_size; display++)
	{
		display_entry = &display_data[display];

		if(!display_entry->detected)
			continue;

		active_slots = 0;

		for(slot = 0; slot < display_slot_amount; slot++)
		{
			if(display_entry->slot[slot].timeout > 0)
			{
				if(--display_entry->slot[slot].timeout == 0)
					display_entry->slot[slot].content[0] = '\0';
			}

			if(display_entry->slot[slot].content[0])
				active_slots++;
		}

		if(active_slots == 0)
		{
			snprintf(display_entry->slot[0].content, sizeof(display_entry->slot[0].content), "%s",
					default_message);
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

irom void display_init(const char *default_message_in)
{
	display_data_t *entry;
	unsigned int current;
	unsigned int slot;

	snprintf(default_message, sizeof(default_message), "%s", default_message_in);

	for(current = 0; current < display_size; current++)
	{
		entry = &display_data[current];

		if(entry->init_fn && (entry->init_fn()))
			entry->detected = true;
		else
			entry->detected = false;

		for(slot = 0; slot < display_slot_amount; slot++)
		{
			entry->current_slot = 0;
			entry->brightness = 1;
			entry->slot[slot].timeout = 0;
			entry->slot[slot].content[0] = '\0';
		}
	}

	display_update(false);
}

irom unsigned int display_setslot(display_id_t display, unsigned int slot, unsigned int timeout,
		const char *text, unsigned int size, char *dst)
{
	display_data_t *display_entry;

	if(display >= display_size)
		return(snprintf(dst, size, "display setslot: display #%d unknown\n", (int)display));

	display_entry = &display_data[display];

	if(!display_entry->detected)
		return(snprintf(dst, size, "display setslot: display #%d not found\n", (int)display));

	if(slot > display_slot_amount)
		return(snprintf(dst, size, "display setslot: slot #%d out of limits\n", (int)slot));

	strlcpy(display_entry->slot[slot].content, text, display_slot_size);
	display_entry->slot[slot].timeout = timeout;

	display_update(false);

	return(snprintf(dst, size, "display setslot: set slot %d on %s to \"%s\"\n",
				slot, display_entry->name, display_entry->slot[slot].content));
}

irom bool_t display_set_brightness(display_id_t display, unsigned int brightness)
{
	display_data_t *display_entry;

	if(brightness > 4)
		return(false);

	if(display >= display_size)
		return(false);

	display_entry = &display_data[display];

	if(!display_entry->detected)
		return(false);

	display_entry->brightness = brightness;
	display_update(false);

	return(true);
}

irom bool_t display_get_brightness(display_id_t display, unsigned int *brightness)
{
	display_data_t *display_entry;

	if(display >= display_size)
		return(false);

	display_entry = &display_data[display];

	if(!display_entry->detected)
		return(false);

	*brightness = display_entry->brightness;

	return(true);
}

irom attr_const attr_pure bool_t display_detected(display_id_t display)
{
	if(display > display_size)
		return(false);

	return(device_data[display].detected);
}

irom unsigned int display_dump(unsigned int size, char *dst, unsigned int verbose_level)
{
	unsigned int display;
	unsigned int slot;
	unsigned int length;
	display_data_t *entry;
	display_slot_t *slot_entry;
	char *orig_dst = dst;

	for(display = 0; display < display_size; display++)
	{
		entry = &display_data[display];

		if((verbose_level > 0) || entry->detected)
		{
			length = snprintf(dst, size, "> %c display #%u (%s: %s)\n",
					entry->detected ? '+' : ' ', display, entry->name, entry->type);

			size -= length;
			dst += length;

			if(verbose_level > 1)
			{
				for(slot = 0; slot < display_slot_amount; slot++)
				{
					slot_entry = &entry->slot[slot];

					length = snprintf(dst, size, ">> slot %u: timeout %u, text: \"%s\"\n",
							slot, slot_entry->timeout, slot_entry->content);

					size -= length;
					dst += length;
				}
			}
		}
	}

	if(dst == orig_dst)
	{
		length = snprintf(dst, size, "> no displays found\n");
		dst += length;
		size -= length;
	}

	return(dst - orig_dst);
}
