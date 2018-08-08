#ifndef queue_h
#define queue_h

#include <stdint.h>

#include "util.h"

typedef struct
{
	char *data;
	int size;
	int in;
	int out;
} queue_t;

void queue_new(queue_t *queue, int size, char *buffer);

always_inline static attr_pure char queue_empty(const queue_t *queue)
{
	return(queue->in == queue->out);
}

always_inline static attr_pure char queue_full(const queue_t *queue)
{
	return(((queue->in + 1) % queue->size) == queue->out);
}

always_inline static void queue_flush(queue_t *queue)
{
	queue->in = 0;
	queue->out = 0;
}

always_inline static void queue_push(queue_t *queue, char data)
{
	queue->data[queue->in] = data;
	queue->in = (queue->in + 1) % queue->size;
}

always_inline static char queue_pop(queue_t *queue)
{
	char data;

	data = queue->data[queue->out];
	queue->out = (queue->out + 1) % queue->size;

	return(data);
}

#endif
