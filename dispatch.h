#ifndef dispatch_h
#define dispatch_h

#include "util.h"

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
	task_prio_low = 0,
	task_prio_medium,
	task_prio_high,
	task_prio_size,
} task_prio_t;

typedef enum
{
	task_uart_fetch_fifo,
	task_uart_fill_fifo,
	task_uart_bridge,
	task_alert_association,
	task_alert_disassociation,
	task_reset,
	task_run_sequencer,
	task_periodic_i2c_sensors,
	task_init_displays,
	task_received_command,
	task_display_update,
	task_wlan_recovery,
	task_remote_trigger,
	task_wlan_reconnect,
	task_pins_changed_gpio,
	task_pins_changed_mcp,
	task_pins_changed_pcf,
	task_display_load_picture_worker,
	task_invalid,
	task_size = task_invalid,
} task_id_t;

enum
{
	task_received_command_text,
	task_received_command_packet,
	task_received_command_uart
};

typedef struct
{
	string_t *src;
	string_t *src_oob;
	string_t *dst;
	int dst_data_pad_offset;
	int dst_data_oob_offset;
} app_params_t;

typedef struct attr_packed
{
	int16_t	io;
	int16_t	pin;
} trigger_t;

assert_size(trigger_t, 4);

extern trigger_t trigger_alert;
extern trigger_t pcint_alert;

extern bool uart_bridge_active;

void dispatch_init1(void);
void dispatch_init2(void);
bool dispatch_post_task(task_prio_t, task_id_t, uint32_t parameter_32, uint16_t parameter_16, uint8_t parameter_8);
#endif
