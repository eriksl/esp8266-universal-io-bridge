#include "queue.h"

#include "util.h"

irom queue_t * queue_new(unsigned int size)
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


iram attr_pure char queue_empty(const queue_t *queue)
{
	return(queue->in == queue->out);
}

iram attr_pure char queue_full(const queue_t *queue)
{
	return(((queue->in + 1) % queue->size) == queue->out);
}

iram attr_pure unsigned int queue_lf(const queue_t *queue)
{
	return(queue->lf);
}

iram void queue_flush(queue_t *queue)
{
	queue->in = 0;
	queue->out = 0;
	queue->lf = 0;
}

iram void queue_push(queue_t *queue, char data)
{
	if(data == '\n')
		queue->lf++;

	queue->data[queue->in] = data;
	queue->in = (queue->in + 1) % queue->size;
}

iram char queue_pop(queue_t *queue)
{
	char data;

	data = queue->data[queue->out];
	queue->out = (queue->out + 1) % queue->size;

	if(data == '\n')
		queue->lf--;

	return(data);
}
