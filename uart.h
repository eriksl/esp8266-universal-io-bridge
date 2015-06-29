#ifndef uart_h
#define uart_h

#include <stdint.h>

typedef enum __attribute__ ((__packed__))
{
	parity_none,
	parity_even,
	parity_odd,
	parity_error
} uart_parity_t;

_Static_assert(sizeof(uart_parity_t) == 1, "sizeof(uart_parity_t) != 1");

typedef struct
{
	uint32_t		baud_rate;
	uint8_t			data_bits;
	uart_parity_t	parity;
	uint8_t			stop_bits;
} uart_parameters_t;

const char		*uart_parity_to_string(uart_parity_t);
char			uart_parity_to_char(uart_parity_t);
uart_parity_t	uart_string_to_parity(const char *);
uint16_t		uart_parameters_to_string(const uart_parameters_t *, uint16_t, char *);
void			uart_init(const uart_parameters_t *);
void			uart_start_transmit(char);

#endif
