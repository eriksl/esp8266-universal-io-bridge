#ifndef stats_h
#define stats_h

#include <stdint.h>
#include "dispatch.h"
#include "util.h"

enum
{
	uarts = 2
};

typedef struct
{
	unsigned int user_pre_init_called:1;
	unsigned int user_pre_init_success:1;
} stat_flags_t;

extern stat_flags_t stat_flags;

typedef struct
{
	unsigned int rx_interrupts;
	unsigned int rx_posted;
	unsigned int rx_posted_max;
	unsigned int rx_posted_skipped;
	unsigned int tx_interrupts;
	unsigned int tx_posted;
	unsigned int tx_posted_max;
	unsigned int tx_posted_skipped;
} stat_uart_instance_t;

typedef struct
{
	unsigned int spurious;

	stat_uart_instance_t instance[uarts];
} stat_uart_t;

extern stat_uart_t stat_uart;

extern unsigned int stat_fast_timer;
extern unsigned int stat_slow_timer;
extern unsigned int stat_pwm_cycles;;
extern unsigned int stat_pwm_timer_interrupts;
extern unsigned int stat_pwm_timer_interrupts_while_nmi_masked;
extern unsigned int stat_pc_counts;
extern unsigned int stat_cmd_udp;
extern unsigned int stat_cmd_tcp;
extern unsigned int stat_cmd_uart;
extern unsigned int stat_cmd_send_buffer_overflow;
extern unsigned int stat_cmd_receive_buffer_overflow;
extern unsigned int stat_cmd_udp_packet_incomplete;
extern unsigned int stat_cmd_tcp_too_many_segments;
extern unsigned int stat_cmd_invalid_packet_length;
extern unsigned int stat_cmd_timeout;
extern unsigned int stat_cmd_checksum_error;
extern unsigned int stat_cmd_duplicate;
extern unsigned int stat_uart_receive_buffer_overflow;
extern unsigned int stat_uart_send_buffer_overflow;
extern unsigned int stat_config_read_requests;
extern unsigned int stat_config_read_loads;
extern unsigned int stat_config_write_requests;
extern unsigned int stat_config_write_saved;
extern unsigned int stat_config_write_aborted;
extern unsigned int stat_update_uart;
extern unsigned int stat_update_longop;
extern unsigned int stat_update_display;
extern unsigned int stat_display_picture_load_worker_called;
extern unsigned int stat_task_posted[task_prio_size];
extern unsigned int stat_task_executed[task_prio_size];
extern unsigned int stat_task_post_failed[task_prio_size];
extern unsigned int stat_task_current_queue[task_prio_size];
extern unsigned int stat_task_max_queue[task_prio_size];
extern unsigned int stat_lwip_tcp_send_error;
extern unsigned int stat_lwip_udp_send_error;
extern unsigned int stat_lwip_tcp_received_packets;
extern unsigned int stat_lwip_udp_received_packets;
extern unsigned int stat_lwip_tcp_received_bytes;
extern unsigned int stat_lwip_udp_received_bytes;
extern unsigned int stat_lwip_tcp_sent_packets;
extern unsigned int stat_lwip_udp_sent_packets;
extern unsigned int stat_lwip_tcp_sent_bytes;
extern unsigned int stat_lwip_udp_sent_bytes;
extern unsigned int stat_lwip_tcp_locked;
extern unsigned int stat_lwip_udp_locked;
extern unsigned int stat_lwip_unicast_received;
extern unsigned int stat_lwip_broadcast_received;
extern unsigned int stat_lwip_multicast_received;
extern unsigned int stat_lwip_unicast_dropped;
extern unsigned int stat_lwip_broadcast_dropped;
extern unsigned int stat_lwip_multicast_dropped;
extern unsigned int stat_broadcast_group_received;
extern unsigned int stat_init_display_time_us;
extern unsigned int stat_init_io_time_us;
extern unsigned int stat_init_associate_time_us;
extern unsigned int stat_init_ip_time_us;
extern unsigned int stat_display_update_min_us;
extern unsigned int stat_display_update_max_us;
extern unsigned int stat_spi_slave_interrupts;
extern unsigned int stat_spi_slave_i2s_interrupts;
extern unsigned int stat_spi_largest_chunk;
extern unsigned int stat_spi_wait_cycles;
extern unsigned int stat_spi_8;
extern unsigned int stat_spi_16;

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

extern unsigned int stat_font_render_time;

extern unsigned int stat_heap_min, stat_heap_max;

void stats_firmware(string_t *dst);
void stats_flash(string_t *dst);
void stats_time(string_t *dst);
void stats_counters(string_t *dst);
void stats_lwip(string_t *dst);
void stats_i2c(string_t *dst);
void stats_uart(string_t *dst);
#endif
