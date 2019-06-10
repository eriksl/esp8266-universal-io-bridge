#ifndef dispatch_h
#define dispatch_h

#include "config.h"

typedef enum
{
	task_invalid,
	task_uart_fetch_fifo,
	task_uart_fill_fifo,
	task_uart_bridge,
	task_alert_pin_changed,
	task_alert_association,
	task_alert_disassociation,
	task_reset,
	task_run_sequencer,
	task_init_i2c_sensors,
	task_periodic_i2c_sensors,
	task_init_displays,
	task_received_command,
	task_display_update,
	task_fallback_wlan,
	task_update_time,
} task_id_t;

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
void	dispatch_post_task(unsigned int prio, task_id_t, unsigned int argument);
#endif
