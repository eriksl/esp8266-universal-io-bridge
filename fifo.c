#include "fifo.h"

#include <mem.h>
#include <esp-missing-decls.h>

fifo_t * fifo_new(uint16_t size)
{
	fifo_t *fifo;

	if((fifo = os_malloc(sizeof(fifo_t))))
	{
		if((fifo->data = os_malloc(size)))
		{
			fifo->size = size;
			fifo->in = 0;
			fifo->out = 0;

			return(fifo);
		}
	}

	return(0);
}


char fifo_empty(const fifo_t *fifo)
{
	return(fifo->in == fifo->out);
}

char fifo_full(const fifo_t *fifo)
{
	return(((fifo->in + 1) % fifo->size) == fifo->out);
}

void fifo_flush(fifo_t *fifo)
{
	fifo->in = 0;
	fifo->out = 0;
}

void fifo_push(fifo_t *fifo, char data)
{
	fifo->data[fifo->in] = data;
	fifo->in = (fifo->in + 1) % fifo->size;
}

char fifo_pop(fifo_t *fifo)
{
	char data;

	data = fifo->data[fifo->out];
	fifo->out = (fifo->out + 1) % fifo->size;

	return(data);
}
