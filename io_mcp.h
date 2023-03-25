#ifndef io_mcp_h
#define io_mcp_h

#include "io.h"
#include "util.h"

#include <stdint.h>

enum
{
	io_mcp_pin_size = 16,
};

typedef enum
{
	io_mcp_instance_20 = 0,
	io_mcp_instance_21,
	io_mcp_instance_22,
	io_mcp_instance_spi,
	io_mcp_instance_size
} io_mcp_instance_t;

extern const io_info_entry_t io_info_entry_mcp_20;
extern const io_info_entry_t io_info_entry_mcp_21;
extern const io_info_entry_t io_info_entry_mcp_22;
extern const io_info_entry_t io_info_entry_mcp_spi;

void io_mcp_pins_changed(uint32_t pin_status_mask, uint16_t pin_value_mask, uint8_t id);

extern const char help_description_io_mcp_spi[];
app_action_t application_function_io_mcp_spi(app_params_t *parameters);

#endif
