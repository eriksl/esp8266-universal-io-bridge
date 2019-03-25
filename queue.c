#include "queue.h"

void queue_new(queue_t *queue, int size, char *buffer)
{
	queue->data = buffer;
	queue->size = size;
	queue->in = 0;
	queue->out = 0;
}
