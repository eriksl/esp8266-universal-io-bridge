#ifndef i2c_h
#define i2c_h

#include <stdint.h>

typedef enum
{
	i2c_state_invalid = 0,
	i2c_state_idle,
	i2c_state_header_send,
	i2c_state_start_send,
	i2c_state_bus_wait_1,
	i2c_state_bus_wait_2,
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

_Static_assert(sizeof(i2c_state_t) == 4, "sizeof(i2c_state_t) != 4");

typedef enum
{
	i2c_error_ok = 0,
	i2c_error_no_init,
	i2c_error_invalid_state_not_idle,
	i2c_error_invalid_state_idle,
	i2c_error_invalid_state_not_send_header,
	i2c_error_invalid_state_not_send_start,
	i2c_error_invalid_state_not_send_address_or_data,
	i2c_error_invalid_state_not_receive_ack,
	i2c_error_invalid_state_not_send_ack,
	i2c_error_invalid_state_not_send_stop,
	i2c_error_bus_lock,
	i2c_error_sda_stuck,
	i2c_error_address_nak,
	i2c_error_data_nak,
	i2c_error_receive_error,
	i2c_error_device_error_1,
	i2c_error_device_error_2,
	i2c_error_device_error_3,
	i2c_error_device_error_4,
	i2c_error_device_error_5,
	i2c_error_error,
	i2c_error_size = i2c_error_error
} i2c_error_t;

_Static_assert(sizeof(i2c_error_t) == 4, "sizeof(i2c_error_t) != 4");

i2c_error_t		i2c_init(unsigned int sda_index, unsigned int scl_index);
i2c_error_t		i2c_reset(void);
i2c_error_t		i2c_send(unsigned int address, unsigned int length, const uint8_t *bytes);
i2c_error_t		i2c_receive(unsigned int address, unsigned int length, uint8_t *bytes);
unsigned int	i2c_error_format_string(const char *tag, i2c_error_t error,
					unsigned int size, char *dst);

i2c_error_t		i2c_send_1(unsigned int address, unsigned int byte0);
i2c_error_t		i2c_send_2(unsigned int address, unsigned int byte0, unsigned int byte1);
i2c_error_t		i2c_send_3(unsigned int address, unsigned int byte0, unsigned int byte1, unsigned int byte2);
#endif
