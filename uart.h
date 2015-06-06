#ifndef uart_h
#define uart_h

void debug(char);
void debug_int(int);

void uart_init(void);
void uart_start_transmit(char);

#endif
