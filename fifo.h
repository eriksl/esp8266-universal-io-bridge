#ifndef fifo_h
#define fifo_h

#include <stdint.h>

typedef struct
{
	char *data;
	uint16_t size;
	uint16_t in;
	uint16_t out;
} fifo_t;

fifo_t * fifo_new(uint16_t size);
char fifo_empty(const fifo_t *fifo);
char fifo_full(const fifo_t *fifo);
void fifo_flush(fifo_t *fifo);
void fifo_push(fifo_t *fifo, char data);
char fifo_pop(fifo_t *fifo);

#endif
