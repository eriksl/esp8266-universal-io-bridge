#ifndef queue_h
#define queue_h

#include <stdint.h>

typedef struct
{
	char *data;
	uint16_t size;
	uint16_t in;
	uint16_t out;
	uint16_t lf;
} queue_t;

queue_t * queue_new(uint16_t size);
char queue_empty(const queue_t *queue);
char queue_full(const queue_t *queue);
uint16_t queue_lf(const queue_t *queue);
void queue_flush(queue_t *queue);
void queue_push(queue_t *queue, char data);
char queue_pop(queue_t *queue);

#endif
