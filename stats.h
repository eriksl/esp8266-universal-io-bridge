#ifndef stats_h
#define stats_h

#include <stdint.h>
#include "c_types.h"

extern uint32_t stat_uart_rx_interrupts;
extern uint32_t stat_uart_tx_interrupts;
extern uint32_t stat_timer;
extern uint32_t stat_background_task;
extern uint32_t stat_application_periodic;
extern uint32_t stat_application_periodic_wrapped;

void stats_generate(uint16_t size, char *dst);

#endif
