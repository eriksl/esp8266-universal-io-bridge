#ifndef dispatch_h
#define dispatch_h

#include "config.h"

#include <os_type.h>
#include <ets_sys.h>
#include <user_interface.h>

typedef enum
{
	uart_task_invalid,
	uart_task_fetch_fifo,
	uart_task_fill0_fifo,
	uart_task_fill1_fifo,
	command_task_command_reset,
	command_task_command_reset_finish,
	command_task_command_uart_bridge,
	command_task_command_disconnect,
	command_task_command_init_i2c_sensors,
	command_task_command_init_displays,
	command_task_command_received_command,
	command_task_command_display_update,
	command_task_command_fallback_wlan,
	command_task_command_update_time,
	command_task_command_run_sequencer,
	timer_task_io_periodic_slow,
	timer_task_io_periodic_fast,
} task_command_t;

extern string_t flash_sector_buffer;

void	dispatch_init1(void);
void	dispatch_init2(void);
void	dispatch_post_uart(task_command_t);
void	dispatch_post_command(task_command_t);
void	dispatch_post_timer(task_command_t);
#endif
