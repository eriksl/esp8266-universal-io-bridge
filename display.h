#ifndef display_h
#define display_h

#include "util.h"
#include "application.h"

#include <stdint.h>

typedef enum
{
	display_slot_amount = 8,
	display_slot_tag_size = 32,
	display_slot_content_size = 64
} display_slot_enum_t;

assert_size(display_slot_enum_t, 4);

typedef struct
{
	int		timeout;
	char	tag[display_slot_tag_size + 1];
	char	content[display_slot_content_size + 1];
} display_slot_t;

typedef struct
{
	unsigned int	detected:1;
	uint8_t			current_slot;
	uint8_t			brightness;
	display_slot_t	slot[display_slot_amount];
} display_data_t;

void	display_init(void);
void	display_periodic(void);

app_action_t application_function_display_brightness(const string_t *src, string_t *dst);
app_action_t application_function_display_dump(const string_t *src, string_t *dst);
app_action_t application_function_display_default_message(const string_t *src, string_t *dst);
app_action_t application_function_display_set(const string_t *src, string_t *dst);
#endif
