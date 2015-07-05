#include "queue.h"

#include "util.h"

ICACHE_FLASH_ATTR queue_t * queue_new(uint16_t size)
{
	queue_t *queue;

	if((queue = malloc(sizeof(queue_t))))
	{
		if((queue->data = malloc(size)))
		{
			queue->size = size;
			queue->in = 0;
			queue->out = 0;
			queue->lf = 0;

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

uint16_t queue_lf(const queue_t *queue)
{
	return(queue->lf);
}

void queue_flush(queue_t *queue)
{
	queue->in = 0;
	queue->out = 0;
	queue->lf = 0;
}

void queue_push(queue_t *queue, char data)
{
	if(data == '\n')
		queue->lf++;

	queue->data[queue->in] = data;
	queue->in = (queue->in + 1) % queue->size;
}

char queue_pop(queue_t *queue)
{
	char data;

	data = queue->data[queue->out];
	queue->out = (queue->out + 1) % queue->size;

	if(data == '\n')
		queue->lf--;

	return(data);
}
