#ifndef user_main_h
#define user_main_h

#include "config.h"

#include <os_type.h>
#include <ets_sys.h>
#include <user_interface.h>

enum
{
	uart_task_id					= USER_TASK_PRIO_0,
	uart_task_queue_length			= 16,

	command_task_id					= USER_TASK_PRIO_1,
	command_task_queue_length		= 16,

	background_task_id				= USER_TASK_PRIO_2,
	background_task_queue_length	= 64,
};

enum
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
};

extern os_event_t background_task_queue[background_task_queue_length];

bool_t	wlan_init(void);
void	uart_set_initial(unsigned int uart);
#endif
