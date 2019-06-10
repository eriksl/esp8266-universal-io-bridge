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
	unsigned int user_pre_init_called:1;
	unsigned int user_pre_init_success:1;
} stat_flags_t;

extern stat_flags_t stat_flags;

extern unsigned int stat_uart0_rx_interrupts;
extern unsigned int stat_uart0_tx_interrupts;
extern unsigned int stat_uart1_tx_interrupts;
extern unsigned int stat_fast_timer;
extern unsigned int stat_slow_timer;
extern unsigned int stat_pwm_cycles;;
extern unsigned int stat_pwm_timer_interrupts;
extern unsigned int stat_pwm_timer_interrupts_while_nmi_masked;
extern unsigned int stat_pc_counts;
extern unsigned int stat_cmd_receive_buffer_overflow;
extern unsigned int stat_cmd_send_buffer_overflow;
extern unsigned int stat_uart_receive_buffer_overflow;
extern unsigned int stat_uart_send_buffer_overflow;
extern unsigned int stat_config_read_requests;
extern unsigned int stat_config_read_loads;
extern unsigned int stat_config_write_requests;
extern unsigned int stat_config_write_saved;
extern unsigned int stat_config_write_aborted;
extern unsigned int stat_update_uart;
extern unsigned int stat_update_longop;
extern unsigned int stat_update_command_udp;
extern unsigned int stat_update_command_tcp;
extern unsigned int stat_update_display;
extern unsigned int stat_task_posted[3];
extern unsigned int stat_task_executed[3];
extern unsigned int stat_task_post_failed[3];
extern unsigned int stat_task_current_queue[3];
extern unsigned int stat_task_max_queue[3];
extern unsigned int stat_lwip_tcp_send_segmentation;
extern unsigned int stat_lwip_tcp_send_error;
extern unsigned int stat_lwip_udp_send_error;
extern unsigned int stat_lwip_tcp_received_packets;
extern unsigned int stat_lwip_tcp_received_bytes;
extern unsigned int stat_lwip_tcp_sent_packets;
extern unsigned int stat_lwip_tcp_sent_bytes;
extern unsigned int stat_lwip_udp_received_packets;
extern unsigned int stat_lwip_udp_received_bytes;
extern unsigned int stat_lwip_udp_sent_packets;
extern unsigned int stat_lwip_udp_sent_bytes;
extern unsigned int stat_init_display_time_us;
extern unsigned int stat_init_io_time_us;
extern unsigned int stat_init_associate_time_us;
extern unsigned int stat_init_ip_time_us;

extern unsigned int stat_sntp_received;
extern unsigned int stat_sntp_poll;

extern int stat_debug_1;
extern int stat_debug_2;
extern int stat_debug_3;

extern unsigned int stat_i2c_init_time_us;
extern unsigned int stat_i2c_sda_stucks;
extern unsigned int stat_i2c_sda_stuck_max_period;
extern unsigned int stat_i2c_bus_locks;
extern unsigned int stat_i2c_bus_lock_max_period;
extern unsigned int stat_i2c_soft_resets;
extern unsigned int stat_i2c_hard_resets;

extern volatile uint32_t *stat_stack_sp_initial;
extern int stat_stack_painted;
extern unsigned int stat_heap_min, stat_heap_max;

void stats_firmware(string_t *dst);
void stats_time(string_t *dst);
void stats_counters(string_t *dst);
void stats_i2c(string_t *dst);
void stats_wlan(string_t *dst);
#endif
