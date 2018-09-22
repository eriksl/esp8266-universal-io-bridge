#ifndef _sequencer_h_
#define _sequencer_h_

#include "attribute.h"
#include "util.h"

#include <stdint.h>

int			sequencer_get_current(void);
int			sequencer_get_start(void);
uint64_t	sequencer_get_current_end_time(void);
int			sequencer_get_repeats(void);
void		sequencer_get_status(_Bool *running, unsigned int *start, unsigned int *flash_size, unsigned int *flash_size_entries,
				unsigned int *flash_offset_flash0, unsigned int *flash_offset_flash1, unsigned int *flash_offset_mapped);
void		sequencer_run(void);
void		sequencer_init(void);
_Bool		sequencer_clear(void);
void		sequencer_start(unsigned int start, unsigned int repeats);
void		sequencer_stop(void);
_Bool		sequencer_set_entry(unsigned int entry, int io, int pin, uint32_t value, int duration);
_Bool		sequencer_get_entry(unsigned int entry, _Bool *active, int *io, int *pin, uint32_t *value, int *duration);
_Bool		sequencer_remove_entry(unsigned int entry);

#endif
