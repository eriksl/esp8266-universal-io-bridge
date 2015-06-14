#ifndef stats_h
#define stats_h

#include <stdint.h>
#include <sys/types.h>
#include "c_types.h"

typedef uint32_t stats_t;

extern stats_t stat_uart_rx_interrupts;
extern stats_t stat_uart_tx_interrupts;
extern stats_t stat_application_periodic;

void stats_generate(size_t size, char *dst);

#endif
