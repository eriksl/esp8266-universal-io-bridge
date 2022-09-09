#ifndef _init_h_
#define _init_h_

#include "util.h"

#include <stdint.h>

enum
{
	stack_paint_magic = 0xabcdefaa,
	stack_top = 0x3fffeb30,
	stack_bottom = 0x40000000 - sizeof(void *)
};

extern volatile uint32_t *stack_stack_sp_initial;
extern int stack_stack_painted;

void stack_paint_stack(void);

#endif
