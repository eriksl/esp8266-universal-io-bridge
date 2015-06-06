#ifndef user_main_h
#define user_main_h

#include "fifo.h"

#include <os_type.h>
#include <ets_sys.h>

enum
{
	background_task_id				= 0,
	background_task_queue_length	= 64,
	buffer_size						= 1024,
};

extern fifo_t *uart_send_fifo;
extern fifo_t *uart_receive_fifo;
extern os_event_t background_task_queue[background_task_queue_length];

#endif
