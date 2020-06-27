#include "display.h"
#include "display_saa.h"
#include "i2c.h"

#include <stdint.h>
#include <stdbool.h>

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

roflash static const uint32_t led_charrom[] =
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

static int brightness = 1;
static int i2c_bus = -1;

static int led_render_char(int character)
{
	int add_dot = 0;

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

	if(character >= (int)(sizeof(led_charrom) / sizeof(*led_charrom)))
		return(0xff);		// this should never happen

	return(led_charrom[character] | add_dot);
}

bool display_saa1064_init(void)
{
	uint8_t i2cdata;
	int bus;

	for(bus = 0; bus < i2c_busses; bus++)
	{
		if(i2c_select_bus(bus) != i2c_error_ok)
			continue;

		if(i2c_receive(0x38, 1, &i2cdata) != i2c_error_ok)
			continue;

		if(i2c_receive(0x39, 1, &i2cdata) == i2c_error_ok)	// try to detect veml6070
		{													// which uses both 0x38 and 0x39 addresses
			i2c_receive(0x38, 1, &i2cdata);
			continue;
		}

		if((i2cdata & 0x7f) != 0x00)
			continue;

		if(i2c_send2(0x38, 0x00, 0x7f) != i2c_error_ok)
			continue;

		i2c_bus = bus;
		return(true);
	}

	i2c_select_bus(0);
	return(false);
}

bool display_saa1064_bright(int bright_in)
{
	if(i2c_bus < 0)
		return(false);

	if((bright_in < 0) || (bright_in > 4))
		return(false);

	brightness = bright_in;

	return(true);
}

static unsigned int display_buffer_index;

_Static_assert(sizeof(display_buffer_size) >= 4, "display buffer too small");

bool display_saa1064_begin(unsigned int slot, bool logmode)
{
	if(i2c_bus < 0)
		return(false);

	strecpy((char *)display_buffer,  "    ", 4);

	display_buffer_index = 0;

	return(true);
}

bool display_saa1064_output(unsigned int unicode)
{
	if((unicode == '.') && (display_buffer_index > 0))
		display_buffer[display_buffer_index - 1] |= 0x80;
	else
		if(display_buffer_index < 4)
			display_buffer[display_buffer_index++] = unicode & 0x7f;

	return(true);
}

bool display_saa1064_end(void)
{
	static const uint8_t bright_to_saa[5] =
	{
		0x00, 0x10, 0x30, 0x50, 0x70
	};

	uint8_t i2cdata[6];

	i2cdata[0] = 0x00;	// start at control register (0x00),
						// followed by four digits segments registers (0x01-0x04)
	i2cdata[1] = 0x07;	// multiplex mode, enable all digits, no test mode
	i2cdata[1] |= bright_to_saa[brightness];

	for(display_buffer_index = 0; display_buffer_index < 4; display_buffer_index++)
		i2cdata[5 - display_buffer_index] = led_render_char(display_buffer[display_buffer_index]); // reverse digit's position

	i2c_send(0x38, 6, i2cdata);

	return(true);
}
