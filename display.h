#ifndef display_h
#define display_h

#include "util.h"

#include <stdint.h>

typedef enum
{
	display_saa1064,
	display_error,
	display_size = display_error
} display_id_t;

_Static_assert(sizeof(display_id_t) == 4, "sizeof(display_id_t) != 4");

typedef enum
{
	display_slot_amount = 8,
	display_slot_size = 80
} display_slot_enum_t;

_Static_assert(sizeof(display_slot_enum_t) == 4, "sizeof(display_slot_enum_t) != 4");

typedef struct
{
	unsigned int	timeout;
	char			content[display_slot_size];
} display_slot_t;

void			display_init(const char *default_message);
void			display_periodic(void);
bool_t			display_detected(display_id_t);
unsigned int	display_dump(unsigned int size, char *dst, unsigned int verbose_level);
unsigned int	display_setslot(display_id_t display, unsigned int slot, unsigned int timeout,
					const char *text, unsigned int size, char *dst);
bool_t			display_set_brightness(display_id_t display, unsigned int brightness);
bool_t			display_get_brightness(display_id_t display, unsigned int *brightness);
#endif
