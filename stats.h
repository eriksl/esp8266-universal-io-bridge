#ifndef stats_h
#define stats_h

#include <stdint.h>
#include "util.h"

enum
{
	stack_paint_magic = 0xabcdefaa,
	stack_top = 0x3fffeb30,
	stack_bottom = 0x40000000 - sizeof(void *)
};

typedef struct
{
	unsigned int user_rf_cal_sector_set:1;
	unsigned int user_rf_pre_init:1;
} stat_called_t;

extern stat_called_t stat_called;

extern int stat_uart0_rx_interrupts;
extern int stat_uart0_tx_interrupts;
extern int stat_uart1_tx_interrupts;
extern int stat_fast_timer;
extern int stat_slow_timer;
extern int stat_pwm_timer_interrupts;
extern int stat_pwm_timer_interrupts_while_nmi_masked;
extern int stat_pc_counts;
extern int stat_i2c_init_time_us;
extern int stat_display_init_time_us;
extern int stat_cmd_receive_buffer_overflow;
extern int stat_cmd_send_buffer_overflow;
extern int stat_uart_receive_buffer_overflow;
extern int stat_uart_send_buffer_overflow;

extern int stat_update_uart;
extern int stat_update_longop;
extern int stat_update_command_udp;
extern int stat_update_command_tcp;
extern int stat_update_display;
extern int stat_update_ntp;

extern int stat_debug_1;
extern int stat_debug_2;
extern int stat_debug_3;

extern volatile uint32_t *stat_stack_sp_initial;
extern int stat_stack_painted;

void stats_firmware(string_t *dst);
void stats_time(string_t *dst);
void stats_counters(string_t *dst);
void stats_i2c(string_t *dst);
void stats_wlan(string_t *dst);
#endif
