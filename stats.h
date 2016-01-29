#ifndef stats_h
#define stats_h

#include <stdint.h>
#include "c_types.h"
#include "util.h"

extern int stat_uart_rx_interrupts;
extern int stat_uart_tx_interrupts;
extern int stat_timer_fast;
extern int stat_timer_slow;
extern int stat_timer_second;
extern int stat_timer_minute;
extern int stat_background_task;
extern int stat_i2c_init_time_us;
extern int stat_display_init_time_us;

extern int ut_days;
extern int ut_hours;
extern int ut_mins;
extern int ut_secs;
extern int ut_tens;

extern int rt_days;
extern int rt_hours;
extern int rt_mins;
extern int rt_secs;
extern int rt_tens;

void stats_generate(string_t *);

#endif
