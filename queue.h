#ifndef queue_h
#define queue_h

#include <stdint.h>

typedef struct
{
	char *data;
	unsigned int size;
	unsigned int in;
	unsigned int out;
	unsigned int lf;
} queue_t;

queue_t * queue_new(unsigned int size);
char queue_empty(const queue_t *queue);
char queue_full(const queue_t *queue);
unsigned int queue_lf(const queue_t *queue);
void queue_flush(queue_t *queue);
void queue_push(queue_t *queue, char data);
char queue_pop(queue_t *queue);

#endif
