#ifndef i2c_h
#define i2c_h

#include "util.h"

#include <stdint.h>

typedef enum
{
	i2c_state_invalid = 0,
	i2c_state_idle,
	i2c_state_header_send,
	i2c_state_start_send,
	i2c_state_address_send,
	i2c_state_address_ack_receive,
	i2c_state_address_ack_received,
	i2c_state_data_send_data,
	i2c_state_data_send_ack_receive,
	i2c_state_data_send_ack_received,
	i2c_state_data_receive_data,
	i2c_state_data_receive_ack_send,
	i2c_state_stop_send,
	i2c_state_error,
	i2c_state_size = i2c_state_error
} i2c_state_t;

assert_size(i2c_state_t, 4);

typedef enum
{
	i2c_error_ok = 0,
	i2c_error_no_init,
	i2c_error_invalid_state_not_idle,
	i2c_error_invalid_state_idle,
	i2c_error_invalid_state_not_send_header,
	i2c_error_invalid_state_not_send_address_or_data,
	i2c_error_invalid_state_not_receive_ack,
	i2c_error_invalid_state_not_send_ack,
	i2c_error_bus_lock,
	i2c_error_sda_stuck,
	i2c_error_address_nak,
	i2c_error_data_nak,
	i2c_error_device_error_1,
	i2c_error_device_error_2,
	i2c_error_device_error_3,
	i2c_error_device_error_4,
	i2c_error_device_error_5,
	i2c_error_error,
	i2c_error_size = i2c_error_error
} i2c_error_t;

_Static_assert(sizeof(i2c_error_t) == 4, "sizeof(i2c_error_t) != 4");

void		i2c_init(int sda_index, int scl_index);
i2c_error_t	i2c_send(int address, int length, const uint8_t *bytes);
i2c_error_t	i2c_receive(int address, int length, uint8_t *bytes);
void		i2c_error_format_string(string_t *dst, i2c_error_t error);

i2c_error_t		i2c_send_1(int address, int byte0);
i2c_error_t		i2c_send_2(int address, int byte0, int byte1);
i2c_error_t		i2c_send_3(int address, int byte0, int byte1, int byte2);
i2c_error_t		i2c_send_4(int address, int byte0, int byte1, int byte2, int byte3);
#endif
