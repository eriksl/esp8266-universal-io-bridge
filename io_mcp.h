#ifndef io_mcp_h
#define io_mcp_h

#include "io.h"
#include "util.h"

#include <stdint.h>

typedef enum
{
	io_mcp_instance_20 = 0,
	io_mcp_instance_first = io_mcp_instance_20,
	io_mcp_instance_21,
	io_mcp_instance_22,
	io_mcp_instance_last = io_mcp_instance_22,
	io_mcp_instance_size
} io_mcp_instance_t;

void			io_mcp_periodic_slow(int io, const struct io_info_entry_T *, io_data_entry_t *, io_flags_t *);
io_error_t		io_mcp_init(const struct io_info_entry_T *);
io_error_t		io_mcp_init_pin_mode(string_t *, const struct io_info_entry_T *, io_data_pin_entry_t *, const io_config_pin_entry_t *, int);
io_error_t		io_mcp_get_pin_info(string_t *, const struct io_info_entry_T *, io_data_pin_entry_t *, const io_config_pin_entry_t *, int);
io_error_t		io_mcp_read_pin(string_t *, const struct io_info_entry_T *, io_data_pin_entry_t *, const io_config_pin_entry_t *, int, uint32_t *);
io_error_t		io_mcp_write_pin(string_t *, const struct io_info_entry_T *, io_data_pin_entry_t *, const io_config_pin_entry_t *, int, uint32_t);
io_error_t		io_mcp_set_mask(string_t *, const struct io_info_entry_T *, unsigned int, unsigned int);

#endif
