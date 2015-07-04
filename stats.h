#ifndef stats_h
#define stats_h

#include <stdint.h>
#include "c_types.h"

typedef uint32_t stats_t;

extern stats_t stat_uart_rx_interrupts;
extern stats_t stat_uart_tx_interrupts;
extern stats_t stat_timer;
extern stats_t stat_background_task;
extern stats_t stat_application_periodic;
extern stats_t stat_application_periodic_wrapped;

void stats_generate(uint16_t size, char *dst);

#endif
