#include "i2c.h"

#include "util.h"

#include <gpio.h>

static uint32_t sda_mask;
static uint32_t scl_mask;
static int transaction_bit_delay;
static i2c_state_t state = i2c_state_invalid;

typedef enum
{
	i2c_direction_receive,
	i2c_direction_send,
} i2c_direction_t;

_Static_assert(sizeof(i2c_direction_t) == 4, "sizeof(i2c_direction_t) != 4");

typedef enum
{
	i2c_config_stretch_clock_timeout = 32,
	i2c_config_scl_waiting_window = 4,
} i2c_config_t;

struct
{
	unsigned int init_done:1;
} i2c_flags =
{
	.init_done = 0
};

static roflash const char state_strings[i2c_state_size][32] =
{
	"invalid",
	"idle",
	"send header",
	"send start",
	"wait for bus to quiesce",
	"wait for bus to keep quiet",
	"send address",
	"address receive ack",
	"address received ack",
	"send data",
	"send data receive ack",
	"send data ack received",
	"receive data",
	"receive data send ack",
	"send stop"
};

static roflash const char error_strings[i2c_error_size][32] =
{
	"ok",
	"uninitialised",
	"state not idle",
	"state idle",
	"state not send header",
	"state not send start",
	"state not send address or data",
	"state not receive ack",
	"state not send ack",
	"state not send stop",
	"bus locked",
	"sda stuck",
	"address NAK",
	"data NAK",
	"receive error",
	"device specific error 1",
	"device specific error 2",
	"device specific error 3",
	"device specific error 4",
	"device specific error 5",
};

irom void i2c_error_format_string(string_t *dst, i2c_error_t error)
{
	if(error != i2c_error_ok)
		string_cat(dst, ": i2c bus error: ");
	else
		string_cat(dst, ": ");

	if(error < i2c_error_size)
		string_cat_ptr(dst, error_strings[error]);
	else
		string_cat(dst, "<unknown error>");

	string_cat(dst, " (in bus state: ");

	if(state < i2c_state_size)
		string_cat_ptr(dst, state_strings[state]);
	else
		string_format(dst, "<unknown state %d>", state);

	string_cat(dst, ")");
}

iram static always_inline void delay(void)
{
	int delay = transaction_bit_delay;

	while(delay-- > 0)
		asm("nop");
}

iram static always_inline void set_io(uint32_t clear, uint32_t set)
{
	gpio_output_set(set, clear, 0, 0);
}

iram static always_inline uint32_t get_io(void)
{
	return(gpio_input_get() & (sda_mask | scl_mask));
}

iram static always_inline void clear_sda(void)
{
	set_io(sda_mask, 0);
}

iram static always_inline void set_sda(void)
{
	set_io(0, sda_mask);
}

iram static always_inline void clear_scl(void)
{
	set_io(scl_mask, 0);
}

iram static always_inline void set_scl(void)
{
	set_io(0, scl_mask);
}

iram static always_inline bool_t sda_is_set(void)
{
	return(!!(get_io() & sda_mask));
}

iram static always_inline bool_t scl_is_set(void)
{
	return(!!(get_io() & scl_mask));
}

iram static always_inline i2c_error_t wait_idle(void)
{
	int current;

	for(current = i2c_config_stretch_clock_timeout; current > 0; current--)
	{
		if(scl_is_set())
			break;

		delay();
	}

	if(current == 0)
		return(i2c_error_bus_lock);

	return(i2c_error_ok);
}

irom static noinline i2c_error_t send_start(void)
{
	i2c_error_t error;
	int current;

	if(state != i2c_state_start_send)
		return(i2c_error_invalid_state_not_send_start);

	// wait for scl and sda to be released by all masters and slaves
	
	state = i2c_state_bus_wait_1;

	if((error = wait_idle()) != i2c_error_ok)
		return(error);

	// set sda to high

	clear_scl();
	delay();

	if(scl_is_set())
		return(i2c_error_bus_lock);

	set_sda();
	delay();

	if(!sda_is_set())
		return(i2c_error_sda_stuck);

	set_scl();
	delay();

	state = i2c_state_bus_wait_2;

	// demand bus is idle for a minimum window

	for(current = i2c_config_scl_waiting_window; current > 0; current--)
		if(!scl_is_set() || !sda_is_set())
			return(i2c_error_bus_lock);

	// generate start condition by leaving scl high and pulling sda low

	clear_sda();
	delay();

	if(sda_is_set())
		return(i2c_error_sda_stuck);

	return(i2c_error_ok);
}

irom static noinline i2c_error_t send_stop(void)
{
	i2c_error_t error;

	state = i2c_state_stop_send;

	// at this point scl should be high and sda is unknown
	// wait for scl to be released by all masters and slaves
	
	if((error = wait_idle()) != i2c_error_ok)
		return(error);

	delay();

	// set sda to low

	clear_scl();
	delay();

	if(scl_is_set())
		return(i2c_error_bus_lock);

	clear_sda();
	delay();

	if(sda_is_set())
		return(i2c_error_sda_stuck);

	set_scl();
	delay();

	if(sda_is_set())
		return(i2c_error_sda_stuck);

	if(!scl_is_set())
		return(i2c_error_bus_lock);

	// now generate the stop condition by leaving scl high and setting sda high

	set_sda();
	delay();

	if(!scl_is_set())
		return(i2c_error_bus_lock);

	if(!sda_is_set())
		return(i2c_error_sda_stuck);

	state = i2c_state_idle;

	return(i2c_error_ok);
}

iram static noinline i2c_error_t send_bit(bool_t bit)
{
	i2c_error_t error;

	// at this point scl should be high and sda will be unknown
	// wait for scl to be released by slave (clock stretching)
	
	if((error = wait_idle()) != i2c_error_ok)
		return(error);
	
	clear_scl();
	delay();

	if(scl_is_set())
		return(i2c_error_bus_lock);

	if(bit)
	{
		set_sda();
		delay();

		if(!sda_is_set())
			return(i2c_error_sda_stuck);
	}
	else
	{
		clear_sda();
		delay();

		if(sda_is_set())
			return(i2c_error_sda_stuck);
	}

	set_scl();
	delay();

	// take care of clock stretching

	if((error = wait_idle()) != i2c_error_ok)
		return(error);

	return(i2c_error_ok);
}

irom static i2c_error_t send_byte(int byte)
{
	i2c_error_t error;
	int current;

	if((state != i2c_state_address_send) && (state != i2c_state_data_send_data))
		return(i2c_error_invalid_state_not_send_address_or_data);

	for(current = 8; current > 0; current--)
	{
		if((error = send_bit(byte & 0x80)) != i2c_error_ok)
			return(error);
		byte <<= 1;
	}

	return(i2c_error_ok);
}

iram static noinline i2c_error_t receive_bit(bool_t *bit)
{
	i2c_error_t error;

	// at this point scl should be high and sda is unknown,
	// but should be high before reading
	
	if(state == i2c_state_idle)
		return(i2c_error_invalid_state_idle);

	// wait for scl to be released by slave (clock stretching)
	
	if((error = wait_idle()) != i2c_error_ok)
		return(error);
	
	// make sure sda is high (open) so slave can pull it
	// do it while clock is pulled
	
	clear_scl();
	set_sda();

	// wait for slave to pull/release sda

	delay();

	// release clock again

	set_scl();

	// take care of clock stretching

	if((error = wait_idle()) != i2c_error_ok)
		return(error);

	delay();

	*bit = sda_is_set() ? 1 : 0;

	return(i2c_error_ok);
}

irom static noinline i2c_error_t receive_byte(uint8_t *byte)
{
	int current;
	bool_t bit;
	i2c_error_t error;

	for(*byte = 0, current = 8; current > 0; current--)
	{
		*byte <<= 1;

		if((error = receive_bit(&bit)) != i2c_error_ok)
			return(error);

		if(bit)
			*byte |= 0x01;
	}

	return(i2c_error_ok);
}

iram static noinline i2c_error_t send_ack(bool_t ack)
{
	i2c_error_t error;

	if(state != i2c_state_data_receive_ack_send)
		return(i2c_error_invalid_state_not_send_ack);

	if((error = send_bit(!ack)) != i2c_error_ok)
		return(error);

	return(i2c_error_ok);
}

iram static noinline i2c_error_t receive_ack(bool_t *ack)
{
	i2c_error_t error;
	bool_t bit;

	if((state != i2c_state_address_ack_receive) && (state != i2c_state_data_send_ack_receive))
		return(i2c_error_invalid_state_not_receive_ack);

	if((error = receive_bit(&bit)) != i2c_error_ok)
		return(error);

	*ack = !bit;

	return(i2c_error_ok);
}

irom static noinline i2c_error_t send_header(int address, i2c_direction_t direction)
{
	i2c_error_t error;
	bool_t ack;

	if(state != i2c_state_header_send)
		return(i2c_error_invalid_state_not_send_header);

	address <<= 1;
	address |= direction == i2c_direction_receive ? 0x01 : 0x00;

	state = i2c_state_start_send;

	if((error = send_start()) != i2c_error_ok)
		return(error);

	state = i2c_state_address_send;

	if((error = send_byte(address)) != i2c_error_ok)
		return(error);

	state = i2c_state_address_ack_receive;

	if((error = receive_ack(&ack)) != i2c_error_ok)
		return(error);

	state = i2c_state_address_ack_received;

	if(!ack)
		return(i2c_error_address_nak);

	return(i2c_error_ok);
}

irom i2c_error_t i2c_reset(void)
{
	int current;

	if(!i2c_flags.init_done)
		return(i2c_error_no_init);

	send_stop();

	// if someone is holding the sda line, simulate clock cycles
	// to make them release it

	for(current = i2c_config_stretch_clock_timeout; current > 0; current--)
	{
		if(sda_is_set())
			break;

		clear_scl();
		delay();
		set_scl();
		delay();
	}

	if(!sda_is_set())
		return(i2c_error_sda_stuck);

	if(!scl_is_set())
		return(i2c_error_bus_lock);

	return(i2c_error_ok);
}

irom i2c_error_t i2c_init(int sda_pin, int scl_pin, int i2c_delay)
{
	sda_mask = 1 << sda_pin;
	scl_mask = 1 << scl_pin;
	transaction_bit_delay = i2c_delay;

	i2c_flags.init_done = 1;

	return(i2c_reset());
}

irom i2c_error_t i2c_send(int address, int length, const uint8_t *bytes)
{
	int current;
	i2c_error_t error;
	bool_t ack;

	if(!i2c_flags.init_done)
		return(i2c_error_no_init);

	if(state != i2c_state_idle)
		return(i2c_error_invalid_state_not_idle);

	state = i2c_state_header_send;

	if((error = send_header(address, i2c_direction_send)) != i2c_error_ok)
		return(error);

	for(current = 0; current < length; current++)
	{
		state = i2c_state_data_send_data;

		if((error = send_byte(bytes[current])) != i2c_error_ok)
			return(error);

		state = i2c_state_data_send_ack_receive;

		if((error = receive_ack(&ack)) != i2c_error_ok)
			return(error);

		state = i2c_state_data_send_ack_received;

		if(!ack)
			return(i2c_error_data_nak);
	}

	if((error = send_stop()) != i2c_error_ok)
		return(error);

	return(i2c_error_ok);
}

irom i2c_error_t i2c_receive(int address, int length, uint8_t *bytes)
{
	int current;
	i2c_error_t error;

	if(!i2c_flags.init_done)
		return(i2c_error_no_init);

	if(state != i2c_state_idle)
		return(i2c_error_invalid_state_not_idle);

	state = i2c_state_header_send;

	if((error = send_header(address, i2c_direction_receive)) != i2c_error_ok)
		return(error);

	for(current = 0; current < length; current++)
	{
		state = i2c_state_data_receive_data;

		if((error = receive_byte(&bytes[current])) != i2c_error_ok)
			return(error);

		state = i2c_state_data_receive_ack_send;

		if((error = send_ack((current + 1) < length)) != i2c_error_ok)
			return(error);
	}

	if((error = send_stop()) != i2c_error_ok)
		return(error);

	return(i2c_error_ok);
}

irom i2c_error_t i2c_send_1(int address, int byte0)
{
	uint8_t bytes[1];

	bytes[0] = byte0;

	return(i2c_send(address, 1, bytes));
}

irom i2c_error_t i2c_send_2(int address, int byte0, int byte1)
{
	uint8_t bytes[2];

	bytes[0] = byte0;
	bytes[1] = byte1;

	return(i2c_send(address, 2, bytes));
}

irom i2c_error_t i2c_send_3(int address, int byte0, int byte1, int byte2)
{
	uint8_t bytes[3];

	bytes[0] = byte0;
	bytes[1] = byte1;
	bytes[2] = byte2;

	return(i2c_send(address, 3, bytes));
}
