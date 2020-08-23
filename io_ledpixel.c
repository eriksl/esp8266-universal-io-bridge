#include "io_ledpixel.h"
#include "util.h"

roflash static const unsigned int lut_5_8[32] =
{
	0,		//	0	0x00
	1,		//	1	0x01
	2,		//	2	0x02
	3,		//	3	0x03
	4,		//	4	0x04
	5,		//	5	0x05
	6,		//	6	0x06
	7,		//	7	0x07
	8,		//	8	0x08
	10,		//	9	0x0a
	12,		//	10	0x0c
	15,		//	11	0x0f
	18,		//	12	0x12
	21,		//	13	0x15
	25,		//	14	0x19
	28,		//	15	0x1c
	32,		//	16	0x20
	40,		//	17	0x28
	48,		//	18	0x30
	56,		//	19	0x38
	64,		//	20	0x40
	72,		//	21	0x48
	80,		//	22	0x50
	88,		//	23	0x58
	96,		//	24	0x60
	112,	//	25	0x70
	128,	//	26	0x80
	144,	//	27	0x90
	160,	//	28	0xa0
	192,	//	29	0xc0
	224,	//	30	0xe0
	255,	//	31	0xff
};

static unsigned int lookup_5_to_8(unsigned int entry)
{
	if(entry >= sizeof(lut_5_8))
		return(0xff);

	return(lut_5_8[entry]);
}

typedef union attr_packed
{
	struct
	{
		unsigned int b:5;
		unsigned int g:5;
		unsigned int r:5;
		unsigned int flag:1;
		unsigned int pinmask:16;
	};
	unsigned int value;
} ledpixel_mask_value_t;

assert_size(ledpixel_mask_value_t, 4);

void io_ledpixel_value_mask_to_rgb(unsigned int value, unsigned int *pinmask, unsigned int *flag, unsigned int *rgb)
{
	ledpixel_mask_value_t	mask_value;
	unsigned int			r, g, b;

	mask_value.value = value;

	*pinmask = mask_value.pinmask;
	*flag = mask_value.flag;

	r = lookup_5_to_8(mask_value.r);
	g = lookup_5_to_8(mask_value.g);
	b = lookup_5_to_8(mask_value.b);

	*rgb = (r << 16) | (g << 8) | (b << 0);
}
