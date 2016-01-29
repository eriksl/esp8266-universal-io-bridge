#ifndef queue_h
#define queue_h

#include <stdint.h>

typedef struct
{
	char *data;
	int size;
	int in;
	int out;
	int lf;
} queue_t;

void queue_new(queue_t *queue, int size, char *buffer);
char queue_empty(const queue_t *queue);
char queue_full(const queue_t *queue);
int queue_lf(const queue_t *queue);
void queue_flush(queue_t *queue);
void queue_push(queue_t *queue, char data);
char queue_pop(queue_t *queue);

#endif
