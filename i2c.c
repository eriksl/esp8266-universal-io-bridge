#include "i2c.h"

#include "util.h"

#include <user_interface.h>

#include "config.h"
#include "util.h"
#include "io_gpio.h"

typedef enum
{
	i2c_direction_receive,
	i2c_direction_send,
} i2c_direction_t;

typedef enum
{
	i2c_sequence_normal,
	i2c_sequence_repeated_start,
} i2c_sequence_t;

_Static_assert(sizeof(i2c_direction_t) == 4, "sizeof(i2c_direction_t) != 4");

typedef enum
{
	i2c_config_stretch_clock_timeout = 20000,
} i2c_config_t;

struct
{
	unsigned int init_done:1;
	unsigned int multiplexer:1;
} i2c_flags =
{
	.init_done = 0,
	.multiplexer = 0
};

static roflash const char roflash_state_strings[i2c_state_size][32] =
{
	"invalid",
	"idle",
	"send header",
	"send start",
	"send address",
	"address receive ACK/NAK",
	"address received ACK/NAK",
	"send data",
	"send data receive ACK/NAK",
	"send data ACK/NAK received",
	"receive data",
	"receive data send ACK/NAK",
	"send stop"
};

static roflash const char roflash_error_strings[i2c_error_size][32] =
{
	"ok",
	"uninitialised",
	"state not idle",
	"state idle",
	"state not send header",
	"state not send address or data",
	"state not receive ACK/NAK",
	"state not send ACK/NAK",
	"bus locked",
	"sda stuck",
	"address NAK",
	"data NAK",
	"device specific error 1",
	"device specific error 2",
	"device specific error 3",
	"device specific error 4",
	"device specific error 5",
	"invalid bus",
};

static int sda_pin;
static int scl_pin;
static int i2c_bus_speed_delay;
static i2c_state_t state = i2c_state_invalid;
static i2c_state_t error_state = i2c_state_invalid;

irom void i2c_error_format_string(string_t *dst, i2c_error_t error)
{
	if(error != i2c_error_ok)
		string_append(dst, ": i2c bus error: ");
	else
		string_append(dst, ": ");

	if(error < i2c_error_size)
		string_append_cstr_flash(dst, roflash_error_strings[error]);
	else
		string_append(dst, "<unknown error>");

	string_append(dst, " (in bus state: ");

	if(error_state < i2c_state_size)
		string_append_cstr_flash(dst, roflash_state_strings[error_state]);
	else
		string_format(dst, "<unknown state %d>", error_state);

	string_append(dst, ")");
}

attr_speed iram static inline void microdelay(void)
{
	int delay = i2c_bus_speed_delay;

	while(delay-- > 0)
		asm("nop");
}

attr_speed iram static inline void sda_low(void)
{
	gpio_set(sda_pin, 0);
}

attr_speed iram static inline void sda_high(void)
{
	gpio_set(sda_pin, 1);
}

attr_speed iram static inline void scl_low(void)
{
	gpio_set(scl_pin, 0);
}

attr_speed iram static inline void scl_high(void)
{
	gpio_set(scl_pin, 1);
}

attr_speed iram static inline bool_t sda_is_low(void)
{
	return(!gpio_get(sda_pin));
}

attr_speed iram static inline bool_t sda_is_high(void)
{
	return(gpio_get(sda_pin));
}

attr_speed iram static inline bool_t scl_is_low(void)
{
	return(!gpio_get(scl_pin));
}

attr_speed iram static inline bool_t scl_is_high(void)
{
	return(gpio_get(scl_pin));
}

attr_speed static inline i2c_error_t wait_for_scl(void)
{
	int current;

	for(current = i2c_config_stretch_clock_timeout; current > 0; current--)
	{
		if(scl_is_high())
			break;

		microdelay();
	}

	if(current == 0)
		return(i2c_error_bus_lock);

	return(i2c_error_ok);
}

iram static noinline i2c_error_t send_start(void)
{
	state = i2c_state_start_send;

	microdelay();
	microdelay();

	if(scl_is_low())
	{
		scl_high();
		microdelay();
		microdelay();
	}

	if(scl_is_low())
		return(i2c_error_bus_lock);

	if(sda_is_low())
	{
		scl_low();
		microdelay();
		microdelay();
		sda_high();
		microdelay();
		microdelay();
		scl_high();
		microdelay();
		microdelay();
	}

	if(sda_is_low())
		return(i2c_error_sda_stuck);

	// start condition

	sda_low();

	microdelay();
	microdelay();

	if(scl_is_low())
		return(i2c_error_bus_lock);

	if(sda_is_high())
		return(i2c_error_sda_stuck);

	return(i2c_error_ok);
}

iram static noinline i2c_error_t send_stop(void)
{
	state = i2c_state_stop_send;

	// at this point sda is unknown and scl should be off

	if(scl_is_low())
		return(i2c_error_bus_lock);

	microdelay();
	microdelay();
	scl_low();
	microdelay();
	microdelay();
	sda_low();
	microdelay();
	microdelay();
	scl_high();
	microdelay();
	microdelay();
	sda_high();
	microdelay();
	microdelay();

	if(scl_is_low())
		return(i2c_error_bus_lock);

	if(sda_is_low())
		return(i2c_error_sda_stuck);

	state = i2c_state_idle;

	return(i2c_error_ok);
}

irom static void i2c_reset(void)
{
	i2c_error_t error;
	int try;
	int delaycounter;

	if(!i2c_flags.init_done)
		return;

	if(state != i2c_state_idle)
		error_state = state;

	for(try = 16; try > 0; try--)
	{
		for(delaycounter = 8; delaycounter > 0; delaycounter--)
			microdelay();

		if((error = send_stop()) == i2c_error_ok)
			break;
	}

	state = i2c_state_idle;
}

iram static noinline i2c_error_t send_bit(bool_t bit)
{
	i2c_error_t error;

	// at this point scl should be off and sda will be unknown
	// wait for scl to be released by slave (clock stretching)

	if((error = wait_for_scl()) != i2c_error_ok)
		return(error);

	scl_low();
	microdelay();

	if(bit)
	{
		sda_high();

		microdelay();

		if(sda_is_low())
			return(i2c_error_sda_stuck);
	}
	else
	{
		sda_low();

		microdelay();

		if(sda_is_high())
			return(i2c_error_sda_stuck);
	}

	scl_high();

	microdelay();
	microdelay();

	// take care of clock stretching

	if((error = wait_for_scl()) != i2c_error_ok)
		return(error);

	return(i2c_error_ok);
}

iram static i2c_error_t send_byte(int byte)
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

	if((error = wait_for_scl()) != i2c_error_ok)
		return(error);

	// make sure sda is off so slave can pull it
	// do it while clock is pulled

	scl_low();
	microdelay();
	sda_high();
	microdelay();
	scl_high();
	microdelay();
	microdelay();

	// take care of clock stretching

	if((error = wait_for_scl()) != i2c_error_ok)
		return(error);

	*bit = sda_is_high() ? 1 : 0;

	return(i2c_error_ok);
}

attr_speed iram static noinline i2c_error_t receive_byte(uint8_t *byte)
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

iram static noinline i2c_error_t send_header(int address, i2c_direction_t direction)
{
	i2c_error_t error;
	bool_t ack;

	if(state != i2c_state_header_send)
		return(i2c_error_invalid_state_not_send_header);

	address <<= 1;
	address |= direction == i2c_direction_receive ? 0x01 : 0x00;

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

iram static i2c_error_t i2c_send_sequence(int address, i2c_sequence_t sequence_type, int length, const uint8_t *bytes)
{
	int current;
	i2c_error_t error;
	bool_t ack;

	if(!i2c_flags.init_done)
		return(i2c_error_no_init);

	if(state != i2c_state_idle)
	{
		error = i2c_error_invalid_state_not_idle;
		goto bail;
	}

	state = i2c_state_header_send;

	if((error = send_header(address, i2c_direction_send)) != i2c_error_ok)
		goto bail;

	for(current = 0; current < length; current++)
	{
		state = i2c_state_data_send_data;

		if((error = send_byte(bytes[current])) != i2c_error_ok)
			goto bail;

		state = i2c_state_data_send_ack_receive;

		if((error = receive_ack(&ack)) != i2c_error_ok)
			goto bail;

		state = i2c_state_data_send_ack_received;

		if(!ack)
		{
			error = i2c_error_data_nak;
			goto bail;
		}
	}

	if((sequence_type == i2c_sequence_normal) && (error = send_stop()) != i2c_error_ok)
		goto bail;

	return(i2c_error_ok);

bail:
	i2c_reset();
	return(error);
}

iram static i2c_error_t i2c_receive_sequence(int address, int length, uint8_t *bytes)
{
	int current;
	i2c_error_t error;

	if(!i2c_flags.init_done)
		return(i2c_error_no_init);

	// state can be idle (normal) or active (repeated start)
	if((state != i2c_state_idle) && (state != i2c_state_data_send_ack_received))
	{
		error = i2c_error_invalid_state_not_idle;
		goto bail;
	}

	state = i2c_state_header_send;

	if((error = send_header(address, i2c_direction_receive)) != i2c_error_ok)
		goto bail;

	for(current = 0; current < length; current++)
	{
		state = i2c_state_data_receive_data;

		if((error = receive_byte(&bytes[current])) != i2c_error_ok)
			goto bail;

		state = i2c_state_data_receive_ack_send;

		if((error = send_ack((current + 1) < length)) != i2c_error_ok)
			goto bail;
	}

	if((error = send_stop()) != i2c_error_ok)
		goto bail;

	return(i2c_error_ok);

bail:
	i2c_reset();
	return(error);
}

irom void i2c_init(int sda_in, int scl_in)
{
	uint8_t byte;

	sda_pin = sda_in;
	scl_pin = scl_in;

	i2c_flags.init_done = 1;

	if(config_flags_get().flag.i2c_high_speed)
		i2c_bus_speed_delay = 2;
	else
	{
		if(config_flags_get().flag.cpu_high_speed)
			i2c_bus_speed_delay = 66;
		else
			i2c_bus_speed_delay = 24;
	}

	i2c_reset();

	if(i2c_receive(0x70, 1, &byte) == i2c_error_ok)
	{
		i2c_flags.multiplexer = 1;
		i2c_select_bus(0);
	}
}

irom i2c_error_t i2c_send(int address, int length, const uint8_t *bytes)
{
	return(i2c_send_sequence(address, i2c_sequence_normal, length, bytes));
}

irom i2c_error_t i2c_receive(int address, int length, uint8_t *bytes)
{
	return(i2c_receive_sequence(address, length, bytes));
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

irom i2c_error_t i2c_send_4(int address, int byte0, int byte1, int byte2, int byte3)
{
	uint8_t bytes[4];

	bytes[0] = byte0;
	bytes[1] = byte1;
	bytes[2] = byte2;
	bytes[3] = byte3;

	return(i2c_send(address, 4, bytes));
}

irom i2c_error_t i2c_send_receive(int address, int sendbyte0, int length, uint8_t *receivebytes)
{
	uint8_t sendbytes[1];
	i2c_error_t rv;

	sendbytes[0] = sendbyte0 & 0xff;

	if((rv = (i2c_send_sequence(address, i2c_sequence_normal, 1, sendbytes))) != i2c_error_ok)
		return(rv);

	return(i2c_receive(address, length, receivebytes));
}

irom i2c_error_t i2c_send_receive_repeated_start(int address, int sendbyte0, int length, uint8_t *receivebytes)
{
	uint8_t sendbytes[1];
	i2c_error_t rv;

	sendbytes[0] = sendbyte0 & 0xff;

	if((rv = (i2c_send_sequence(address, i2c_sequence_repeated_start, 1, sendbytes))) != i2c_error_ok)
		return(rv);

	return(i2c_receive(address, length, receivebytes));
}

irom i2c_error_t i2c_select_bus(unsigned int bus)
{
	if(!i2c_flags.multiplexer)
		return((bus == 0) ? i2c_error_ok : i2c_error_invalid_bus);

	if(bus >= i2c_busses)
		return(i2c_error_invalid_bus);

	bus = 1 << bus;
	bus >>= 1;

	return(i2c_send_1(0x70, bus));
}

irom void i2c_get_info(i2c_info_t *i2c_info)
{
	i2c_info->multiplexer = i2c_flags.multiplexer ? 1 : 0;
	i2c_info->buses = i2c_flags.multiplexer ? i2c_busses : 1;
	i2c_info->delay = i2c_bus_speed_delay;
}
