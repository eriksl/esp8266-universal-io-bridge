#ifndef stats_h
#define stats_h

#include <stdint.h>
#include "c_types.h"

extern uint32_t stat_uart_rx_interrupts;
extern uint32_t stat_uart_tx_interrupts;
extern uint32_t stat_timer_fast;
extern uint32_t stat_timer_slow;
extern uint32_t stat_background_task;

extern uint8_t ut_days;
extern uint8_t ut_hours;
extern uint8_t ut_mins;
extern uint8_t ut_secs;
extern uint8_t ut_tens;

extern uint8_t rt_days;
extern uint8_t rt_hours;
extern uint8_t rt_mins;
extern uint8_t rt_secs;
extern uint8_t rt_tens;

void stats_generate(uint16_t size, char *dst);

#endif
