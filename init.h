#ifndef init_h
#define init_h

#include "util.h"

#include <stdint.h>
#include <stdbool.h>

enum
{
	stack_paint_magic = 0xabcdefaa,
	stack_top = 0x3fffeb30,
	stack_bottom = 0x40000000 - sizeof(void *)
};

extern volatile uint32_t *stack_stack_sp_initial;
extern int stack_stack_painted;

void stack_paint_stack(void);
bool wlan_init_from_config(void);
void wlan_init_start_recovery(void);
bool wlan_ap_switch(const mac_addr_t);
void multicast_init_groups(void);

#endif
