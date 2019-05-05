#ifndef dispatch_h
#define dispatch_h

#include "config.h"

typedef enum
{
	uart_task_invalid,
	uart_task_fetch_fifo,
	uart_task_fill0_fifo,
	uart_task_fill1_fifo,
	command_task_reset,
	command_task_uart_bridge,
	command_task_init_i2c_sensors,
	command_task_periodic_i2c_sensors,
	command_task_init_displays,
	command_task_received_command,
	command_task_display_update,
	command_task_fallback_wlan,
	command_task_update_time,
	command_task_run_sequencer,
	command_task_alert_association,
	command_task_alert_disassociation,
	command_task_alert_status,
	timer_task_io_periodic_slow,
	timer_task_io_periodic_fast,
} task_command_t;

typedef enum
{
	fsb_free,
	fsb_config_read,
	fsb_config_write,
	fsb_config_write_dirty,
	fsb_config_cache,
	fsb_ota,
	fsb_sequencer,
} flash_sector_buffer_use_t;

extern	bool uart_bridge_active;

extern	string_t					flash_sector_buffer;
extern	flash_sector_buffer_use_t	flash_sector_buffer_use;

void	dispatch_init1(void);
void	dispatch_init2(void);
void	dispatch_post_uart(task_command_t);
void	dispatch_post_command(task_command_t);
void	dispatch_post_timer(bool);
#endif
