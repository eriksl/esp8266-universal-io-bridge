#ifndef display_h
#define display_h

#include "util.h"
#include "application.h"

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
	int				timeout;
	char			content[display_slot_size];
} display_slot_t;

void	display_init(void);
void	display_periodic(void);

app_action_t application_function_display_brightness(const string_t *src, string_t *dst);
app_action_t application_function_display_dump(const string_t *src, string_t *dst);
app_action_t application_function_display_default_message(const string_t *src, string_t *dst);
app_action_t application_function_display_set(const string_t *src, string_t *dst);
#endif
