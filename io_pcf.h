#ifndef io_pcf_h
#define io_pcf_h

#include "util.h"
#include "io.h"

#include <stdint.h>

typedef enum
{
	io_pcf_instance_3a = 0,
	io_pcf_instance_26,
	io_pcf_instance_size
} io_pcf_instance_t;

enum
{
	io_pcf_pin_size = 8,
};

extern const io_info_entry_t io_info_entry_pcf_3a;
extern const io_info_entry_t io_info_entry_pcf_26;

void io_pcf_pins_changed(uint32_t pin_status_mask, uint16_t pin_value_mask, uint8_t io);

#endif
