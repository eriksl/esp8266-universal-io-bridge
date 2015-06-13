#include "queue.h"

#include <mem.h>
#include <esp-missing-decls.h>

queue_t * queue_new(uint16_t size)
{
	queue_t *queue;

	if((queue = os_malloc(sizeof(queue_t))))
	{
		if((queue->data = os_malloc(size)))
		{
			queue->size = size;
			queue->in = 0;
			queue->out = 0;

			return(queue);
		}
	}

	return(0);
}


char queue_empty(const queue_t *queue)
{
	return(queue->in == queue->out);
}

char queue_full(const queue_t *queue)
{
	return(((queue->in + 1) % queue->size) == queue->out);
}

void queue_flush(queue_t *queue)
{
	queue->in = 0;
	queue->out = 0;
}

void queue_push(queue_t *queue, char data)
{
	queue->data[queue->in] = data;
	queue->in = (queue->in + 1) % queue->size;
}

char queue_pop(queue_t *queue)
{
	char data;

	data = queue->data[queue->out];
	queue->out = (queue->out + 1) % queue->size;

	return(data);
}
