#ifndef uart_h
#define uart_h

#include <stdint.h>

#include "util.h"

typedef enum
{
	parity_none,
	parity_even,
	parity_odd,
	parity_error
} uart_parity_t;

_Static_assert(sizeof(uart_parity_t) == 4, "sizeof(uart_parity_t) != 4");

typedef struct
{
	uint32_t		baud_rate;
	uint8_t			data_bits;
	uart_parity_t	parity;
	uint8_t			stop_bits;
} uart_parameters_t;

void			uart_parity_to_string(string_t *dst, uart_parity_t);
char			uart_parity_to_char(uart_parity_t);
uart_parity_t	uart_string_to_parity(const string_t *src);
void			uart_parameters_to_string(string_t *dst, const uart_parameters_t *);
void			uart_init(const uart_parameters_t *);
void			uart_start_transmit(char);

#endif
