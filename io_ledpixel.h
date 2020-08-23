#ifndef io_ledpixel_h
#define io_ledpixel_h

#include <attribute.h>

typedef struct attr_packed
{
	unsigned int	enabled:1;
	unsigned int	extended:1;
	unsigned int	grb:1;
	unsigned int	fill8:1;
	unsigned int	value:32;
} ledpixel_data_pin_t;

assert_size(ledpixel_data_pin_t, 5);

void io_ledpixel_value_mask_to_rgb(unsigned int value, unsigned int *pinmask, unsigned int *flag, unsigned int *rgb);

#endif
