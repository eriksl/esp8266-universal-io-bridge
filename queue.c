#include "queue.h"

#include "util.h"

irom void queue_new(queue_t *queue, int size, char *buffer)
{
	queue->data = buffer;
	queue->size = size;
	queue->in = 0;
	queue->out = 0;
	queue->lf = 0;
}

attr_speed iram attr_pure char queue_empty(const queue_t *queue)
{
	return(queue->in == queue->out);
}

attr_speed iram attr_pure char queue_full(const queue_t *queue)
{
	return(((queue->in + 1) % queue->size) == queue->out);
}

attr_speed iram attr_pure int queue_lf(const queue_t *queue)
{
	return(queue->lf);
}

attr_speed iram void queue_flush(queue_t *queue)
{
	queue->in = 0;
	queue->out = 0;
	queue->lf = 0;
}

attr_speed iram void queue_push(queue_t *queue, char data)
{
	if(data == '\n')
		queue->lf++;

	queue->data[queue->in] = data;
	queue->in = (queue->in + 1) % queue->size;
}

attr_speed iram char queue_pop(queue_t *queue)
{
	char data;

	data = queue->data[queue->out];
	queue->out = (queue->out + 1) % queue->size;

	if(data == '\n')
		queue->lf--;

	return(data);
}
