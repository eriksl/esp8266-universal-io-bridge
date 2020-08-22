#ifndef io_ledpixel_h
#define io_ledpixel_h

typedef struct
{
	unsigned int	enabled:1;
	unsigned int	extended:1;
	unsigned int	grb:1;
	unsigned int	fill8:1;
	uint32_t		value;
} ledpixel_data_pin_t;

#endif
