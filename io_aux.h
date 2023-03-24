#ifndef io_aux_h
#define io_aux_h

#include "io.h"
#include "util.h"

#include <stdint.h>

typedef enum
{
	io_aux_pin_gpio = 0,
	io_aux_pin_adc,
	io_aux_pin_error,
	io_aux_pin_size = io_aux_pin_error
} io_aux_pin_t;

assert_size(io_aux_pin_t, 4);

extern const io_info_entry_t io_info_entry_aux;

void io_aux_pins_changed(uint32_t pin_status_mask, uint16_t pin_value_mask);

#endif
