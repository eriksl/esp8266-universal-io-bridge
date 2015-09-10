#ifndef display_h
#define display_h

#include "util.h"

#include <stdint.h>

typedef enum __attribute__ ((__packed__))
{
	display_saa1064,
	display_error,
	display_size = display_error
} display_id_t;

_Static_assert(sizeof(display_id_t) == 1, "sizeof(display_id_t) != 1");

typedef enum __attribute__ ((__packed__))
{
	display_slot_amount = 8,
	display_slot_size = 80
} display_slot_enum_t;

_Static_assert(sizeof(display_slot_enum_t) == 1, "sizeof(display_slot_enum_t) != 1");

typedef struct
{
	uint32_t	timeout;
	char		content[display_slot_size];
} display_slot_t;

void		display_init(void);
void		display_periodic(void);
bool_t		display_detected(display_id_t);
uint16_t	display_dump(uint16_t size, char *dst, uint8_t verbose_level);
uint16_t	display_setslot(display_id_t display, uint8_t slot, uint16_t timeout,
					const char *text, uint16_t size, char *dst);
bool_t		display_set_brightness(display_id_t display, uint8_t brightness);
bool_t		display_get_brightness(display_id_t display, uint8_t *brightness);
#endif
