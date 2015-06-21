#ifndef uart_h
#define uart_h

#include <stdint.h>

typedef enum
{
	parity_none,
	parity_even,
	parity_odd,
	parity_error
} uart_parity_t;

typedef struct
{
	uint32_t		baud_rate;
	uint8_t			data_bits;
	uart_parity_t	parity;
	uint8_t			stop_bits;
} uart_parameters_t;

void debug(char);
void debug_int(int);

const char	*uart_parity_to_string(uint8_t);
char		uart_parity_to_char(uint8_t);
uint8_t		uart_string_to_parity(const char *);
uint16_t	uart_parameters_to_string(const uart_parameters_t *, uint16_t, char *);
void		uart_init(const uart_parameters_t *);
void		uart_start_transmit(char);

#endif
