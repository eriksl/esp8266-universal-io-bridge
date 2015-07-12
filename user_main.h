#ifndef user_main_h
#define user_main_h

#include "queue.h"

#include <os_type.h>
#include <ets_sys.h>
#include <user_interface.h>

enum
{
	background_task_id				= USER_TASK_PRIO_0,
	background_task_queue_length	= 64,
	buffer_size						= 2048,
};

extern queue_t *uart_send_queue;
extern queue_t *uart_receive_queue;
extern os_event_t background_task_queue[background_task_queue_length];

#endif
