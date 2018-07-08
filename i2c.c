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
	i2c_config_scl_wait_cycles = 14000,
	i2c_config_sda_wait_cycles = 10,
	i2c_config_sda_reset_cycles = 32,
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
	"state not send start",
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
static bool_t unconstrained_clock = false;
static bool_t cpu_high_speed = false;
static i2c_state_t state = i2c_state_invalid;
static i2c_state_t error_state = i2c_state_invalid;

always_inline attr_speed static void sda_low(void)
{
	gpio_set(sda_pin, 0);
}

always_inline attr_speed static void sda_high(void)
{
	gpio_set(sda_pin, 1);
}

always_inline attr_speed static void scl_low(void)
{
	gpio_set(scl_pin, 0);
}

always_inline attr_speed static void scl_high(void)
{
	gpio_set(scl_pin, 1);
}

always_inline attr_speed static bool_t sda_is_low(void)
{
	return(!gpio_get(sda_pin));
}

always_inline attr_speed static bool_t sda_is_high(void)
{
	return(gpio_get(sda_pin));
}

always_inline attr_speed static bool_t scl_is_low(void)
{
	return(!gpio_get(scl_pin));
}

always_inline attr_speed static bool_t scl_is_high(void)
{
	return(gpio_get(scl_pin));
}

always_inline attr_speed static void delay(int delay_us_lowspeed)
{
	if(!unconstrained_clock)
		os_delay_us(delay_us_lowspeed);
	else
		if(cpu_high_speed)
		{
			asm volatile("nop");
			asm volatile("nop");
		}
}

iram static i2c_error_t sda_set_test(bool_t val, int delay_val)
{
	int current = i2c_config_sda_wait_cycles;
	int wait_cycles = 0;

	if(val)
	{
		for(sda_high(); current > 0; current--, wait_cycles++)
		{
			if(delay_val)
				delay(delay_val);

			if(sda_is_high())
				break;
		}
	}
	else
	{
		for(sda_low(); current > 0; current--, wait_cycles++)
		{
			if(delay_val)
				delay(delay_val);

			if(sda_is_low())
				break;
		}
	}

	if(wait_cycles < 2)
		return(i2c_error_ok);

	if(current > 0)
	{
		// this line takes ~240/~150 microseconds to complete, so don't add additional delays
		log("sda set test: sda stuck resolved after %d cycles\n", wait_cycles);
		return(i2c_error_ok);
	}

	log("sda set test: sda still stuck after %d cycles, giving up\n", wait_cycles);
	return(i2c_error_sda_stuck);
}

iram static i2c_error_t scl_set_test(bool_t val, int delay_val)
{
	int current = i2c_config_scl_wait_cycles;
	int wait_cycles = 0;

	if(val)
	{
		for(scl_high(); current > 0; current--, wait_cycles++)
		{
			if(delay_val)
				delay(delay_val);

			if(scl_is_high())
				break;
		}
	}
	else
	{
		for(scl_low(); current > 0; current--, wait_cycles++)
		{
			if(delay_val)
				delay(delay_val);

			if(scl_is_low())
				break;
		}
	}

	if(wait_cycles < 2)
		return(i2c_error_ok);

	if(current > 0)
	{
		// this line takes ~240/~150 microseconds to complete, so don't add additional delays
		log("scl set test: bus lock resolved after %d cycles\n", i2c_config_scl_wait_cycles - current);
		return(i2c_error_ok);
	}

	log("scl set test: bus still locked after %d cycles, giving up\n", wait_cycles);
	return(i2c_error_bus_lock);
}

iram static i2c_error_t send_bit(bool_t bit)
{
	i2c_error_t error;

	// at this point scl should be high and sda will be unknown
	// wait for scl to be released by slave (clock stretching)

	if((error = scl_set_test(false, 2)) != i2c_error_ok)
		return(error);

	if((error = sda_set_test(bit, 2)) != i2c_error_ok)
		return(error);

	if((error = scl_set_test(true, 4)) != i2c_error_ok)
		return(error);

	return(i2c_error_ok);
}

iram static i2c_error_t receive_bit(bool_t *bit)
{
	i2c_error_t error;

	// at this point scl should be high and sda will be unknown
	// wait for scl to be released by slave (clock stretching)

	if(state == i2c_state_idle)
		return(i2c_error_invalid_state_idle);

	// make sure sda is off so slave can pull it
	// do it while clock is pulled

	if((error = scl_set_test(false, 4)) != i2c_error_ok)
		return(error);

	sda_high();

	if((error = scl_set_test(true, 4)) != i2c_error_ok)
		return(error);

	// sample at end of scl cycle

	*bit = sda_is_high() ? 1 : 0;

	return(i2c_error_ok);
}

iram static i2c_error_t send_start(void)
{
	i2c_error_t error;

	if(state != i2c_state_start_send)
		return(i2c_error_invalid_state_not_send_start);

	// make sure SDA is released and set it to high

	if((error = send_bit(1)) != i2c_error_ok)
		return(error);

	// send actual start condition

	delay(1);
	if((error = sda_set_test(false, 2)) != i2c_error_ok)
		return(error);

	return(i2c_error_ok);
}

iram static i2c_error_t send_stop(void)
{
	i2c_error_t error;

	// release SDA from last slave's ACK and set it low

	if((error = send_bit(0)) != i2c_error_ok)
		return(error);

	// send actual stop condition

	delay(2);
	if((error = sda_set_test(true, 2)) != i2c_error_ok)
		return(error);

	if(sda_is_low())
	{
		log("send stop: sda still low\n");
		return(i2c_error_sda_stuck);
	}

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

iram static i2c_error_t receive_byte(uint8_t *byte)
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

iram static i2c_error_t send_header(int address, i2c_direction_t direction)
{
	i2c_error_t error;
	bool_t bit;

	if(state != i2c_state_header_send)
		return(i2c_error_invalid_state_not_send_header);

	state = i2c_state_address_send;

	// send slave address

	address <<= 1;
	address |= direction == i2c_direction_receive ? 0x01 : 0x00;

	if((error = send_byte(address)) != i2c_error_ok)
		return(error);

	state = i2c_state_address_ack_receive;

	// receive ACK

	if((error = receive_bit(&bit)) != i2c_error_ok)
		return(error);

	state = i2c_state_address_ack_received;

	if(bit)
		return(i2c_error_address_nak);

	return(i2c_error_ok);
}

iram static i2c_error_t i2c_send_sequence(int address, i2c_sequence_t sequence_type, int length, const uint8_t *bytes)
{
	int current;
	i2c_error_t error;
	bool_t bit;

	if(!i2c_flags.init_done)
		return(i2c_error_no_init);

	if(state != i2c_state_idle)
	{
		error = i2c_error_invalid_state_not_idle;
		goto error_reset;
	}

	state = i2c_state_start_send;

	if((error = send_start()) != i2c_error_ok)
		return(error);

	state = i2c_state_header_send;

	if((error = send_header(address, i2c_direction_send)) != i2c_error_ok)
		goto error_reset;

	for(current = 0; current < length; current++)
	{
		state = i2c_state_data_send_data;

		if((error = send_byte(bytes[current])) != i2c_error_ok)
			goto error_reset;

		state = i2c_state_data_send_ack_receive;

		// receive ACK

		if((error = receive_bit(&bit)) != i2c_error_ok)
			goto error_reset;

		state = i2c_state_data_send_ack_received;

		if(bit)
		{
			error = i2c_error_data_nak;
			goto error_reset;
		}
	}

	if((sequence_type != i2c_sequence_repeated_start) && ((error = send_stop()) != i2c_error_ok))
		goto error_reset;

	state = i2c_state_idle;

	return(i2c_error_ok);

error_reset:
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
		goto error_reset;
	}

	state = i2c_state_start_send;

	if((error = send_start()) != i2c_error_ok)
		return(error);

	state = i2c_state_header_send;

	if((error = send_header(address, i2c_direction_receive)) != i2c_error_ok)
		goto error_reset;

	for(current = 0; current < length; current++)
	{
		state = i2c_state_data_receive_data;

		if((error = receive_byte(&bytes[current])) != i2c_error_ok)
			goto error_reset;

		// send ack

		state = i2c_state_data_receive_ack_send;

		if((error = send_bit((current + 1) >= length)) != i2c_error_ok)
			goto error_reset;
	}

	if((error = send_stop()) != i2c_error_ok)
		goto error_reset;

	state = i2c_state_idle;

	return(i2c_error_ok);

error_reset:
	i2c_reset();
	return(error);
}

irom i2c_error_t i2c_send(int address, int length, const uint8_t *bytes)
{
	return(i2c_send_sequence(address, i2c_sequence_normal, length, bytes));
}

irom i2c_error_t i2c_receive(int address, int length, uint8_t *bytes)
{
	return(i2c_receive_sequence(address, length, bytes));
}

irom i2c_error_t i2c_send_receive(int address, int sendlength, const uint8_t *sendbytes, int receivelength, uint8_t *receivebytes)
{
	i2c_error_t error;

	if((error = (i2c_send_sequence(address, i2c_sequence_normal, sendlength, sendbytes))) != i2c_error_ok)
		return(error);

	return(i2c_receive_sequence(address, receivelength, receivebytes));
}

irom i2c_error_t i2c_send_receive_repeated_start(int address, int sendlength, const uint8_t *sendbytes, int receivelength, uint8_t *receivebytes)
{
	i2c_error_t error;

	if((error = (i2c_send_sequence(address, i2c_sequence_repeated_start, sendlength, sendbytes))) != i2c_error_ok)
		return(error);

	return(i2c_receive_sequence(address, receivelength, receivebytes));
}

irom i2c_error_t i2c_send1(int address, int byte0)
{
	uint8_t bytes[1] = { byte0 };

	return(i2c_send(address, sizeof(bytes), bytes));
}

irom i2c_error_t i2c_send2(int address, int byte0, int byte1)
{
	uint8_t bytes[2] = { byte0, byte1 };

	return(i2c_send(address, sizeof(bytes), bytes));
}

irom i2c_error_t i2c_send3(int address, int byte0, int byte1, int byte2)
{
	uint8_t bytes[3] = { byte0, byte1, byte2 };

	return(i2c_send(address, sizeof(bytes), bytes));
}

irom i2c_error_t i2c_send4(int address, int byte0, int byte1, int byte2, int byte3)
{
	uint8_t bytes[4] = { byte0, byte1, byte2, byte3 };

	return(i2c_send(address, sizeof(bytes), bytes));
}

irom i2c_error_t i2c_send1_receive(int address, int byte0, int receivelength, uint8_t *receivebytes)
{
	uint8_t bytes[1] = { byte0 };

	return(i2c_send_receive(address, sizeof(bytes), bytes, receivelength, receivebytes));
}

irom i2c_error_t i2c_send1_receive_repeated_start(int address, int byte0, int receivelength, uint8_t *receivebytes)
{
	uint8_t bytes[1] = { byte0 };

	return(i2c_send_receive_repeated_start(address, sizeof(bytes), bytes, receivelength, receivebytes));
}

irom i2c_error_t i2c_send2_receive_repeated_start(int address, int byte0, int byte1, int receivelength, uint8_t *receivebytes)
{
	uint8_t bytes[2] = { byte0, byte1 };

	return(i2c_send_receive_repeated_start(address, sizeof(bytes), bytes, receivelength, receivebytes));
}

irom i2c_error_t i2c_select_bus(unsigned int bus)
{
	if(!i2c_flags.multiplexer)
		return((bus == 0) ? i2c_error_ok : i2c_error_invalid_bus);

	if(bus >= i2c_busses)
		return(i2c_error_invalid_bus);

	bus = 1 << bus;
	bus >>= 1;

	return(i2c_send1(0x70, bus));
}

irom i2c_error_t i2c_reset(void)
{
	i2c_error_t error;
	int current;
	int wait_cycles = 0;

	if(!i2c_flags.init_done)
		return(i2c_error_no_init);

	if(state != i2c_state_idle)
		error_state = state;

	state = i2c_state_idle;

	if((error = scl_set_test(true, 0)) != i2c_error_ok)
		return(error);

	// if SDA still asserted by slave, cycle SCL until they release it

	if(sda_is_low())
	{
		for(current = i2c_config_sda_reset_cycles; current > 0; current--, wait_cycles++)
		{
			delay(2);
			scl_low();
			delay(2);
			sda_high();
			delay(2);
			scl_high();
			delay(2);

			if(sda_is_high())
				break;
		}

		if(sda_is_low())
		{
			log("i2c_reset: sda stuck still stuck after %d cycles, giving up\n", wait_cycles);
			return(i2c_error_sda_stuck);
		}

		// this line takes ~240/~150 microseconds to complete, so don't add extra delays
		log("i2c_reset: sda stuck resolved after %d cycles\n", wait_cycles);
	}

	if((error = scl_set_test(true, 0)) != i2c_error_ok)
		return(error);

	if((error = send_stop()) != i2c_error_ok)
	{
		log("i2c_reset: send_stop error: %d\n", error);
		return(error);
	}

	return(i2c_error_ok);
}

irom void i2c_init(int sda_in, int scl_in)
{
	uint8_t byte;

	sda_pin = sda_in;
	scl_pin = scl_in;

	i2c_flags.init_done = 1;

	unconstrained_clock = !!config_flags_get().flag.i2c_high_speed;
	cpu_high_speed = !!config_flags_get().flag.cpu_high_speed;

	i2c_reset();

	if(i2c_receive(0x70, 1, &byte) == i2c_error_ok)
	{
		i2c_flags.multiplexer = 1;
		i2c_select_bus(0);
	}
}

irom void i2c_get_info(i2c_info_t *i2c_info)
{
	i2c_info->multiplexer = i2c_flags.multiplexer ? 1 : 0;
	i2c_info->buses = i2c_flags.multiplexer ? i2c_busses : 1;
}

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
