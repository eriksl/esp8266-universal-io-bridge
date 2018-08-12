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

typedef enum
{
	i2c_delay_send_bit_scl_low = 0,
	i2c_delay_send_bit_scl_high,
	i2c_delay_receive_bit_scl_low,
	i2c_delay_receive_bit_scl_high,
	i2c_delay_fixup_pre_wait,
	i2c_delay_fixup_cycle_phase,
	i2c_delay_fixup_post_wait,
	i2c_delay_send_start_stop_scl_low,
	i2c_delay_send_start_stop_scl_high_1,
	i2c_delay_send_start_stop_scl_high_2,
	i2c_delay_size,
} i2c_delay_enum_t;

typedef struct
{
	unsigned int			delay;
	const unsigned int		factor_slow;
	const unsigned int		factor_fast;
} i2c_delay_t;

static i2c_delay_t i2c_delay[i2c_delay_size] =
{
	{	i2c_delay_send_bit_scl_low,				96,		288		},
	{	i2c_delay_send_bit_scl_high,			280,	690		},
	{	i2c_delay_receive_bit_scl_low,			124,	320		},
	{	i2c_delay_receive_bit_scl_high,			232,	630		},
	{	i2c_delay_fixup_pre_wait,				500,	1332	},
	{	i2c_delay_fixup_cycle_phase,			250,	664		},
	{	i2c_delay_fixup_post_wait,				250,	664		},
	{	i2c_delay_send_start_stop_scl_low,		100,	294		},
	{	i2c_delay_send_start_stop_scl_high_1,	100,	340		},
	{	i2c_delay_send_start_stop_scl_high_2,	60,		210		},
};

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
static i2c_state_t state = i2c_state_invalid;
static i2c_state_t error_state = i2c_state_invalid;

attr_inline void sda_low(void)
{
	gpio_set(sda_pin, 0);
}

attr_inline void sda_high(void)
{
	gpio_set(sda_pin, 1);
}

attr_inline void scl_low(void)
{
	gpio_set(scl_pin, 0);
}

attr_inline void scl_high(void)
{
	gpio_set(scl_pin, 1);
}

attr_inline bool_t sda_is_low(void)
{
	return(!gpio_get(sda_pin));
}

attr_inline bool_t sda_is_high(void)
{
	return(gpio_get(sda_pin));
}

attr_inline bool_t scl_is_low(void)
{
	return(!gpio_get(scl_pin));
}

attr_inline bool_t scl_is_high(void)
{
	return(gpio_get(scl_pin));
}

attr_inline void delay(i2c_delay_enum_t delay_index)
{
	csleep(i2c_delay[delay_index].delay);
}

iram static i2c_error_t sda_set_test(bool_t val, i2c_delay_enum_t delay_index)
{
	int current = i2c_config_sda_wait_cycles;
	int wait_cycles = 0;

	if(val)
	{
		for(sda_high(); current > 0; current--, wait_cycles++)
		{
			delay(delay_index);

			if(sda_is_high())
				break;
		}
	}
	else
	{
		for(sda_low(); current > 0; current--, wait_cycles++)
		{
			delay(delay_index);

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

iram static i2c_error_t scl_set_test(bool_t val, i2c_delay_enum_t delay_index)
{
	int current = i2c_config_scl_wait_cycles;
	int wait_cycles = 0;

	if(val)
	{
		for(scl_high(); current > 0; current--, wait_cycles++)
		{
			delay(delay_index);

			if(scl_is_high())
				break;
		}
	}
	else
	{
		for(scl_low(); current > 0; current--, wait_cycles++)
		{
			delay(delay_index);

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

	// at this point SCL should be high and sda will be unknown
	// wait for SCL to be released by slave (clock stretching)

	if((error = scl_set_test(false, i2c_delay_send_bit_scl_low)) != i2c_error_ok)
	{
		log("\nsend_bit 1\n");
		return(error);
	}

	if((error = sda_set_test(bit, i2c_delay_send_bit_scl_low)) != i2c_error_ok)
	{
		log("\nsend_bit 2\n");
		return(error);
	}

	if((error = scl_set_test(true, i2c_delay_send_bit_scl_high)) != i2c_error_ok)
	{
		log("send_bit 3\n");
		return(error);
	}

	return(i2c_error_ok);
}

iram static i2c_error_t receive_bit(bool_t *bit)
{
	i2c_error_t error;

	// at this point SCL should be high and sda will be unknown
	// wait for SCL to be released by slave (clock stretching)

	if(state == i2c_state_idle)
		return(i2c_error_invalid_state_idle);

	// make sure SDA is off so slave can pull it
	// do it while SCL is pulled

	if((error = scl_set_test(false, i2c_delay_receive_bit_scl_low)) != i2c_error_ok)
		return(error);

	delay(i2c_delay_receive_bit_scl_low);

	// don't check SDA here, because the slave might already have pulled it low, which is OK

	sda_high();

	if((error = scl_set_test(true, i2c_delay_receive_bit_scl_high)) != i2c_error_ok)
		return(error);

	// sample at end of SCL cycle

	*bit = sda_is_high() ? 1 : 0;

	return(i2c_error_ok);
}

iram static i2c_error_t send_start(void)
{
	i2c_error_t error;

	if(state != i2c_state_start_send)
		return(i2c_error_invalid_state_not_send_start);

	// make sure SDA is released and set it to high

	if((error = scl_set_test(false, i2c_delay_send_start_stop_scl_low)) != i2c_error_ok)
		return(error);

	if((error = sda_set_test(true, i2c_delay_send_start_stop_scl_low)) != i2c_error_ok)
		return(error);

	if((error = scl_set_test(true, i2c_delay_send_start_stop_scl_high_1)) != i2c_error_ok)
		return(error);

	// send actual start condition

	if((error = sda_set_test(false, i2c_delay_send_start_stop_scl_high_2)) != i2c_error_ok)
		return(error);

	return(i2c_error_ok);
}

iram static i2c_error_t send_stop(void)
{
	i2c_error_t error;

	// release SDA from last slave's ACK and set it low

	if((error = scl_set_test(false, i2c_delay_send_start_stop_scl_low)) != i2c_error_ok)
		return(error);

	if((error = sda_set_test(false, i2c_delay_send_start_stop_scl_low)) != i2c_error_ok)
		return(error);

	if((error = scl_set_test(true, i2c_delay_send_start_stop_scl_high_1)) != i2c_error_ok)
		return(error);

	// send actual stop condition

	if((error = sda_set_test(true, i2c_delay_send_start_stop_scl_high_2)) != i2c_error_ok)
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

irom noinline static i2c_error_t i2c_reset_fixup_bus(void)
{
	i2c_error_t error;
	int current;
	int wait_cycles = 0;

	if((error = scl_set_test(true, i2c_delay_fixup_pre_wait)) != i2c_error_ok)
		return(error);

	// if SDA still asserted by slave, cycle SCL until they release it

	if(sda_is_low())
	{
		for(current = i2c_config_sda_reset_cycles; current > 0; current--, wait_cycles++)
		{
			delay(i2c_delay_fixup_cycle_phase);
			scl_low();
			delay(i2c_delay_fixup_cycle_phase);
			sda_high();
			delay(i2c_delay_fixup_cycle_phase);
			scl_high();
			delay(i2c_delay_fixup_cycle_phase);

			if(sda_is_high())
				break;
		}

		if(sda_is_low())
		{
			log("i2c-reset-fixup-bus: sda stuck still stuck after %d cycles, giving up\n", wait_cycles);
			return(i2c_error_sda_stuck);
		}

		// this line takes ~240/~150 microseconds to complete, so don't add extra delays
		log("i2c-reset-fixup-bus: sda stuck resolved after %d cycles\n", wait_cycles);
	}

	if((error = scl_set_test(true, i2c_delay_fixup_post_wait)) != i2c_error_ok)
	{
		log("i2c-reset-fixup-bus: bus lock: %d\n", error);
		return(error);
	}

	return(i2c_error_ok);
}

iram i2c_error_t i2c_reset(void)
{
	i2c_error_t error;

	if(!i2c_flags.init_done)
		return(i2c_error_no_init);

	if(state != i2c_state_idle)
		error_state = state;

	state = i2c_state_idle;

	if(sda_is_low() || scl_is_low())
		if((error = i2c_reset_fixup_bus()) != i2c_error_ok)
			return(error);

	if((error = send_stop()) != i2c_error_ok)
	{
		log("i2c-reset: send_stop error: %d\n", error);
		return(error);
	}

	return(i2c_error_ok);
}

irom void i2c_init(int sda_in, int scl_in)
{
	string_init(varname_i2c_speed_delay, "i2c.speed_delay");
	uint8_t byte;
	i2c_delay_enum_t current;
	i2c_delay_t *entry;
	unsigned int config_factor, user_factor;
	int user_config_value;

	sda_pin = sda_in;
	scl_pin = scl_in;

	if(!config_get_int(&varname_i2c_speed_delay, -1, -1, &user_config_value))
		user_config_value = 1000;

	user_factor = (unsigned int)user_config_value;

	for(current = 0; current < i2c_delay_size; current++)
	{
		entry = &i2c_delay[current];

		if(entry->delay != current)
			continue;

		if(config_flags_get().flag.cpu_high_speed)
			config_factor = entry->factor_fast;
		else
			config_factor = entry->factor_slow;

		entry->delay = user_factor * config_factor / 1000;
	}

	i2c_flags.init_done = 1;

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
