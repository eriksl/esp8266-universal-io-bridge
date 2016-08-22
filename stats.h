#ifndef stats_h
#define stats_h

#include <stdint.h>
#include "c_types.h"
#include "util.h"

extern int stat_uart_rx_interrupts;
extern int stat_uart_tx_interrupts;
extern int stat_fast_timer;
extern int stat_slow_timer;
extern int stat_timer_interrupts;
extern int stat_pwm_interrupts;
extern int stat_i2c_init_time_us;
extern int stat_display_init_time_us;

extern int stat_update_uart;
extern int stat_update_longop;
extern int stat_update_command;
extern int stat_update_display;
extern int stat_update_ntp;
extern int stat_update_idle;

extern int debug_a;
extern int debug_b;
extern int debug_c;
extern int debug_d;
extern int debug_e;
extern int debug_f;

void stats_generate(string_t *);

#endif
