#ifndef dispatch_h
#define dispatch_h

#include "config.h"

typedef enum
{
	app_action_normal,
	app_action_error,
	app_action_empty,
	app_action_disconnect,
	app_action_http_ok,
	app_action_reset,
} app_action_t;

assert_size(app_action_t, 4);

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
	task_periodic_i2c_sensors,
	task_init_displays,
	task_received_command,
	task_display_update,
	task_fallback_wlan,
	task_update_time,
	task_remote_trigger,
} task_id_t;

typedef struct
{
	string_t *src;
	string_t *dst;
} app_params_t;

extern bool uart_bridge_active;

void dispatch_init1(void);
void dispatch_init2(void);
void dispatch_post_task(unsigned int prio, task_id_t, unsigned int argument);
#endif
