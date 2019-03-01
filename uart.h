#ifndef uart_h
#define uart_h

#include <stdint.h>

#include "util.h"
#include "esp-uart-register.h"
#include "queue.h"

typedef enum attr_packed
{
	parity_none,
	parity_even,
	parity_odd,
	parity_error
} uart_parity_t;

assert_size(uart_parity_t, 1);

typedef struct attr_packed
{
	uint8_t			data_bits;
	uart_parity_t	parity;
	uint8_t			stop_bits;
	uint32_t		baud_rate;
} uart_parameters_t;

assert_size(uart_parameters_t, 7);

void			uart_task(os_event_t *event);
void			uart_parity_to_string(string_t *dst, uart_parity_t);
char			uart_parity_to_char(uart_parity_t);
uart_parity_t	uart_string_to_parity(const string_t *src);
void			uart_parameters_to_string(string_t *dst, const uart_parameters_t *);
void			uart_init(void);
void			uart_baudrate(unsigned int uart, unsigned int baudrate);
void 			uart_data_bits(unsigned int uart, unsigned int data_bits);
void			uart_stop_bits(unsigned int uart, unsigned int stop_bits);
void			uart_parity(unsigned int uart, uart_parity_t parity);
void			uart_autofill(unsigned int uart, _Bool enable, unsigned int character);
void			uart_is_autofill(unsigned int uart, _Bool *enable, unsigned int *character);
_Bool			uart_full(unsigned int uart);
void			uart_send(unsigned int, unsigned int);
void			uart_flush(unsigned int);
void			uart_clear_send_queue(unsigned int);
_Bool			uart_empty(unsigned int);
unsigned int	uart_receive(unsigned int);
void			uart_clear_receive_queue(unsigned int);
void			uart_set_initial(unsigned int uart);

#endif
