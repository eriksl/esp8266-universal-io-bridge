#include "attribute.h"
#include "sys_string.h"
#include "application.h"
#include "spi.h"
#include "util.h"
#include "io_gpio.h"
#include "sys_time.h"
#include "stats.h"

typedef struct
{
	const spi_clock_t clock;
	const unsigned int pre_div;
	const unsigned int div;
} spi_clock_map_t;

typedef struct
{
	unsigned int fill:1;
	unsigned int bits_available;
	unsigned int word;
	unsigned int bit;
	unsigned int bits;
	uint32_t data[SPI_W0_REGISTERS];
} spi_send_buffer_t;

assert_size(spi_send_buffer_t, 84);

typedef struct attr_packed
{
	unsigned int inited:1;
	unsigned int configured:1;
	unsigned int cs_hold:1;
	unsigned int spi_mode:4;
	unsigned int receive_bytes:8;

	struct attr_packed
	{
		unsigned int enabled:1;
		unsigned int io:4;
		unsigned int pin:4;
	} user_cs;
} spi_state_t;

assert_size(spi_state_t, 4);

static roflash const spi_clock_map_t spi_clock_map[] =
{
	{	spi_clock_80M,	1,			 1,	},
	{	spi_clock_40M,	1,			 2,	},
	{	spi_clock_27M,	1,			 3,	},
	{	spi_clock_20M,	1,			 4,	},
	{	spi_clock_16M,	1,			 5,	},
	{	spi_clock_13M,	1,			 6,	},
	{	spi_clock_11M,	1,			 7,	},
	{	spi_clock_10M,	1,			 8,	},
	{	spi_clock_9M,	1,			 9,	},
	{	spi_clock_8M,	1,			10,	},
	{	spi_clock_7M,	1,			11,	},
	{	spi_clock_6M,	1,			13,	},
	{	spi_clock_5M,	1,			16,	},
	{	spi_clock_4M,	1,			20,	},
	{	spi_clock_3M,	2,			13,	},
	{	spi_clock_2M,	1,			40,	},
	{	spi_clock_1M,	2,			40,	},
	{	spi_clock_500k,	4,			40,	},
	{	spi_clock_250k,	8,			40,	},
	{	spi_clock_100k,	20,			40,	},
	{	spi_clock_50k,	40,			40,	},
	{	spi_clock_10k,	200,		40,	},
	{	spi_clock_1k,	2000,		40,	},
	{	spi_clock_none,	0,			 0,	},
};

static roflash const unsigned int mode_table[spi_mode_size][2] =
{
	{	0,	0	},
	{	0,	0	},
	{	0,	1	},
	{	1,	1	},
	{	1,	0	},
};

static spi_send_buffer_t send_buffer;
static spi_state_t state;

attr_inline void wait_completion(void)
{
	stat_spi_wait_cycles = 0;

	while((read_peri_reg(SPI_CMD(1)) & SPI_USR))
		stat_spi_wait_cycles++;
}

attr_result_used bool spi_init(string_t *error, unsigned int io, unsigned int pin_miso, unsigned int pin_mosi, unsigned int pin_sclk, unsigned int pin_cs)
{
	state.inited = 0;
	state.configured = 0;

	if(io != 0)
	{
		if(error)
			string_append(error, "spi: only supported on gpio I/O (0)\n");
		return(false);
	}

	if(pin_miso != 12)
	{
		if(error)
			string_append(error, "spi: miso should be on pin 12\n");
		return(false);
	}

	if(pin_mosi != 13)
	{
		if(error)
			string_append(error, "spi: mosi should be on pin 13\n");
		return(false);
	}

	if(pin_sclk != 14)
	{
		if(error)
			string_append(error, "spi: sclk should be on pin 14\n");
		return(false);
	}

	if(pin_cs != 15)
	{
		if(error)
			string_append(error, "spi: primary cs should be on pin 15\n");
		return(false);
	}

	send_buffer.bits_available = SPI_W0_REGISTERS * SPI_W0_REGISTER_BIT_WIDTH;

	stat_spi_largest_chunk = stat_spi_wait_cycles = 0;
	state.inited = 1;

	return(true);
}

attr_result_used bool spi_configure(string_t *error, spi_mode_t mode, bool cs_hold, int user_cs_io, int user_cs_pin)
{
	unsigned int reg_pin_mode;
	unsigned int reg_spi_config;

	if(!state.inited)
	{
		if(error)
			string_append(error, "spi: not inited");
		return(false);
	}

	if((mode <= spi_mode_none) || (mode >= spi_mode_size))
	{
		if(error)
			string_append(error, "spi: invalid mode");
		return(false);
	}

	state.user_cs.enabled = 0;

	if((user_cs_io >= 0) && (user_cs_pin >= 0))
	{
		if((user_cs_io >= io_id_size) || (user_cs_pin >= max_pins_per_io))
		{
			if(error)
				string_append(error, "spi: invalid io/pin for user cs\n");
		}
		else
		{
			state.user_cs.enabled = 1;
			state.user_cs.io = user_cs_io;
			state.user_cs.pin = user_cs_pin;
		}
	}

	reg_spi_config = read_peri_reg(PERIPHS_IO_MUX);
	reg_spi_config &= ~(PERIPHS_IO_MUX_HSPI_SYSCLK);
	reg_spi_config |= PERIPHS_IO_MUX_HSPI_ENABLE;
	write_peri_reg(PERIPHS_IO_MUX, reg_spi_config);

	state.spi_mode = mode;

	reg_pin_mode |= SPI_CS1_DIS | SPI_CS2_DIS;

	if(state.user_cs.enabled)
		reg_pin_mode |= SPI_CS0_DIS;

	state.cs_hold = cs_hold;
	state.configured = 1;

	if(!spi_start(error))
		return(false);

	return(true);
}

attr_result_used bool spi_start(string_t *error)
{
	unsigned int current;

	if(!state.inited || !state.configured)
	{
		if(error)
			string_append(error, "spi start: not inited\n");
		return(false);
	}

	send_buffer.bits = 0;
	send_buffer.word = 0;
	send_buffer.bit = 0;
	send_buffer.fill = 0;

	for(current = 0; current < SPI_W0_REGISTERS; current++)
		send_buffer.data[current] = 0;

	return(true);
}

attr_result_used static bool spi_write_8(unsigned int value)
{
	stat_spi_8++;

	send_buffer.data[send_buffer.word] |= (value & 0xff) << send_buffer.bit;
	send_buffer.bits += 8;

	if(send_buffer.bit < 24)
		send_buffer.bit += 8;
	else
	{
		send_buffer.bit = 0;
		send_buffer.word++;
	}

	return(true);
}

attr_result_used static bool spi_write_16(unsigned int value)
{
	stat_spi_16++;

	value = ((value & 0x00ff) << 8) |
			((value & 0xff00) >> 8);

	send_buffer.data[send_buffer.word] |= (value & 0xffff) << send_buffer.bit;
	send_buffer.bits += 16;

	if(send_buffer.bit < 16)
		send_buffer.bit += 16;
	else
	{
		send_buffer.bit = 0;
		send_buffer.word++;
	}

	return(true);
}

attr_result_used bool spi_write(unsigned int bits, uint32_t value)
{
	int bit;

	roflash static const int buffer_bit_index_map[32] =
	{
		 7,  6,  5,  4,  3,  2,  1,  0,
		15, 14, 13, 12, 11, 10,  9,  8,
		23, 22, 21, 20, 19, 18, 17, 16,
		31, 30, 29, 28, 27, 26, 25, 24,
	};

	if(!state.inited || !state.configured)
		return(false);

	if((send_buffer.bits + bits) >= send_buffer.bits_available)
		return(false);

	if((bits == 8) && (send_buffer.bit % 8) == 0)
		return(spi_write_8(value));

	if((bits == 16) && (send_buffer.bit % 16) == 0)
		return(spi_write_16(value));

	for(bit = bits - 1; bit >= 0; bit--)
	{
		if(value & (1UL << bit))
			send_buffer.data[send_buffer.word] |= (1UL << buffer_bit_index_map[send_buffer.bit]);

		send_buffer.bits++;

		if(send_buffer.bit < 31)
			send_buffer.bit++;
		else
		{
			send_buffer.bit = 0;
			send_buffer.word++;
		}
	}

	return(true);
}

attr_result_used bool spi_transmit(string_t *error, spi_clock_t clock,
		unsigned int command_length_bits, unsigned int command,
		unsigned int address_length_bits, unsigned int address,
		unsigned int skip_bits,
		unsigned int receive_bytes)
{
	const spi_clock_map_t *clock_map_ptr;
	unsigned int clock_pre_div, clock_div;
	unsigned int clock_high, clock_low;
	unsigned int w0cur, w0size;
	unsigned int spi_user;
	unsigned int spi_user1;
	unsigned int spi_user2;
	unsigned int spi_addr;
	unsigned int spi_pin_mode;
	unsigned int spi_clock;

	if(!state.inited || !state.configured)
	{
		if(error)
			string_append(error, "spi transmit: not inited or not configured");
		return(false);
	}

	if((command_length_bits > 16) || (address_length_bits > 31) || (skip_bits > 8) || (receive_bytes > 64))
	{
		if(error)
			string_append(error, "spi transmit: parameter error");
		return(false);
	}

	if((command_length_bits == 0) && (address_length_bits == 0) && (send_buffer.bits == 0) && (receive_bytes == 0))
		return(true);

	for(clock_map_ptr = spi_clock_map; clock_map_ptr->clock != spi_clock_none; clock_map_ptr++)
		if(clock_map_ptr->clock == clock)
			break;

	if(clock_map_ptr->clock == spi_clock_none)
	{
		if(error)
			string_append(error, "spi transmit: invalid speed");
		return(false);
	}

	if(send_buffer.bits > stat_spi_largest_chunk)
		stat_spi_largest_chunk = send_buffer.bits;

	spi_user = mode_table[state.spi_mode][1] ? SPI_CK_OUT_EDGE : 0;	// CPHA
	spi_user1 = 0x00;
	spi_user2 = 0x00;
	spi_addr = 0x00;
	spi_pin_mode = mode_table[state.spi_mode][0] ? SPI_IDLE_EDGE : 0;	// CPOL

	if(command_length_bits > 0)
	{
		spi_user  |= SPI_USR_COMMAND;

		if(command_length_bits < 9)
			command <<= (8 - command_length_bits);
		else
		{
			command <<= (16 - command_length_bits);
			command = ((command & 0x00ff) << 8) | ((command & 0xff00) >> 8);
		}

		spi_user2 |= ((command_length_bits - 1) & SPI_USR_COMMAND_BITLEN) << SPI_USR_COMMAND_BITLEN_S;
		spi_user2 |= (command & SPI_USR_COMMAND_VALUE) << SPI_USR_COMMAND_VALUE_S;
	}

	if(address_length_bits > 0)
	{
		spi_user  |= SPI_USR_ADDR;
		address <<= (32 - address_length_bits);
		spi_user1 |= ((address_length_bits - 1) & SPI_USR_ADDR_BITLEN) << SPI_USR_ADDR_BITLEN_S;
		spi_addr   = address;
	}

	if(skip_bits > 0)
	{
		spi_user  |= SPI_USR_DUMMY;
		spi_user1 |= ((skip_bits - 1) & SPI_USR_DUMMY_CYCLELEN) << SPI_USR_DUMMY_CYCLELEN_S;
	}

	if(send_buffer.bits > 0)
	{
		spi_user  |= SPI_USR_MOSI;
		spi_user1 |= ((send_buffer.bits - 1) & SPI_USR_MOSI_BITLEN) << SPI_USR_MOSI_BITLEN_S;
	}

	if(receive_bytes > 0)
	{
		spi_user  |= SPI_USR_MISO;
		spi_user1 |= (((receive_bytes * 8) - 1) & SPI_USR_MISO_BITLEN) << SPI_USR_MISO_BITLEN_S;
		state.receive_bytes = receive_bytes;
	}

	clock_pre_div = clock_map_ptr->pre_div - 1;
	clock_div =		clock_map_ptr->div - 1;
	clock_high =	((clock_div + 1) / 2) - 1;
	clock_low =		clock_div;

	spi_clock =	((clock_pre_div	& SPI_CLKDIV_PRE)	<< SPI_CLKDIV_PRE_S)	|
				((clock_div		& SPI_CLKCNT_N)		<< SPI_CLKCNT_N_S)		|
				((clock_high	& SPI_CLKCNT_H)		<< SPI_CLKCNT_H_S)		|
				((clock_low		& SPI_CLKCNT_L)		<< SPI_CLKCNT_L_S);

	if(clock == spi_clock_80M)
		spi_clock |= SPI_CLK_EQU_SYSCLK;

	if(state.cs_hold)
		spi_user |= SPI_CS_SETUP | SPI_CS_HOLD;

	wait_completion();

	if(send_buffer.bits > 0)
	{
		w0size = (send_buffer.bits / SPI_W0_REGISTER_BIT_WIDTH) + 1;

		if(w0size > SPI_W0_REGISTERS)
			w0size = SPI_W0_REGISTERS;

		for(w0cur = 0; w0cur < w0size; w0cur++)
			write_peri_reg(SPI_W0(1) + (w0cur * 4), send_buffer.data[send_buffer.fill ? 0 : w0cur]);
	}

	write_peri_reg(SPI_ADDR(1), spi_addr);
	write_peri_reg(SPI_USER(1), spi_user);
	write_peri_reg(SPI_USER1(1), spi_user1);
	write_peri_reg(SPI_USER2(1), spi_user2);
	write_peri_reg(SPI_CLOCK(1), spi_clock);
	write_peri_reg(SPI_PIN(1), spi_pin_mode);

	if(state.user_cs.enabled && (io_write_pin(error, state.user_cs.io, state.user_cs.pin, 1) != io_ok))
	{
		if(error)
			string_append(error, "spi: user cs issue (1)");
		return(false);
	}

	set_peri_reg_mask(SPI_CMD(1), SPI_USR);

	return(true);
}

attr_result_used bool spi_receive(string_t *error, unsigned int size, uint8_t *receive_data)
{
	unsigned int current, current_value;

	if(!state.inited || !state.configured)
	{
		if(error)
			string_append(error, "spi receive: not inited or not configured");
		return(false);
	}

	if(state.receive_bytes == 0)
	{
		if(error)
			string_append(error, "spi receive: no data\n");
		return(false);
	}

	wait_completion();

	for(current = 0, current_value = 0; (current < size) && (current < state.receive_bytes); current++)
	{
		if((current & 0x03) == 0x00)
			current_value = read_peri_reg(SPI_W0(1) + (current & ~0x03));

		receive_data[current] = (current_value >> ((current & 0x03) << 3)) & 0xff;
	}

	state.receive_bytes = 0;

	return(true);
}

attr_result_used bool spi_finish(string_t *error)
{
	if(!state.inited || !state.configured)
	{
		if(error)
			string_append(error, "spi finish: not inited or not configured");
		return(false);
	}

	wait_completion();

	if(state.user_cs.enabled)
	{
		if(io_write_pin(error, state.user_cs.io, state.user_cs.pin, 0) != io_ok)
		{
			if(error)
				string_append(error, "spi finish: user cs issue");
			return(false);
		}
	}

	state.receive_bytes = 0;

	return(true);
}

roflash const char help_description_spi_configure[] = "configure SPI interface\n"
		"> usage: spc <mode=0-3> <cs delay=0|1> [<user cs io> <user cs pin>]\n";

app_action_t application_function_spi_configure(string_t *src, string_t *dst)
{
	string_new(, error, 64);
	unsigned int	spi_mode;
	spi_mode_t		spi_mode_enum;
	unsigned int	cs_delay;
	int				user_cs_io, user_cs_pin;

	if(parse_uint(1, src, &spi_mode, 0, ' ') != parse_ok)
		goto usage;

	spi_mode_enum = (spi_mode_t)(spi_mode + spi_mode_0);

	if((spi_mode_enum <= spi_mode_none) || (spi_mode_enum >= spi_mode_size))
		goto usage;

	if(parse_uint(2, src, &cs_delay, 0, ' ') != parse_ok)
		goto usage;

	if(parse_int(3, src, &user_cs_io, 0, ' ') != parse_ok)
		user_cs_io = -1;

	if((user_cs_io < -1) || (user_cs_io > 15))
		goto usage;

	if(parse_int(4, src, &user_cs_pin, 0, ' ') != parse_ok)
		user_cs_pin = -1;

	if((user_cs_pin < -1) || (user_cs_pin >= max_pins_per_io))
		goto usage;

	if(!spi_configure(&error, spi_mode_enum, cs_delay, user_cs_io, user_cs_pin))
	{
		string_format(dst, "spi configure failed: %s\n", string_to_cstr(&error));
		return(app_action_error);
	}

	string_append(dst, "spi configure ok\n");
	return(app_action_normal);

usage:
	string_append_cstr_flash(dst, help_description_spi_configure);

	string_append(dst, "> spi mode:\n");
	string_append(dst, ">    0 clk=0 pha=0\n");
	string_append(dst, ">    1 clk=0 pha=1\n");
	string_append(dst, ">    2 clk=1 pha=0\n");
	string_append(dst, ">    3 clk=1 pha=1\n");

	return(app_action_error);
}

roflash const char help_description_spi_start[] = "prepare writing SPI send buffer data\n"
		"usage: sps\n";

app_action_t application_function_spi_start(string_t *src, string_t *dst)
{
	string_new(, error, 64);

	if(!spi_start(&error))
	{
		string_format(dst, "spi start failed: %s\n", string_to_cstr(&error));
		return(app_action_error);
	}

	string_append(dst, "spi start ok\n");
	return(app_action_normal);
}

roflash const char help_description_spi_write[] = "write data to SPI send buffer\n"
		"usage: spw <bits=0-32> <value>\n";

app_action_t application_function_spi_write(string_t *src, string_t *dst)
{
	unsigned int bits, value;

	if(parse_uint(1, src, &bits, 0, ' ') != parse_ok)
		goto usage;

	if(parse_uint(2, src, &value, 0, ' ') != parse_ok)
		goto usage;

	if(!spi_write(bits, value))
	{
		string_format(dst, "spi write failed\n");
		return(app_action_error);
	}

	string_append(dst, "spi write ok\n");
	return(app_action_normal);

usage:
	string_append_cstr_flash(dst, help_description_spi_write);
	return(app_action_error);
}

roflash const char help_description_spi_transmit[] = "execute the SPI transaction (send and receive)\n"
		"usage: spt <clock speed index>\n"
		"           <command length bits (0-15 bits)> <command value (hex)\n"
		"           <address length bits (0-31 bits)> <address value (hex)\n"
		"           <receive bytes (0-64)\n";

app_action_t application_function_spi_transmit(string_t *src, string_t *dst)
{
	string_new(, error, 64);
	unsigned int command_length, command;
	unsigned int address_length, address;
	unsigned int skip_length;
	unsigned int receive_bytes;
	unsigned int clock_speed;
	spi_clock_t clock_speed_enum;
	const spi_clock_map_t *clock_map_ptr;

	if(parse_uint(1, src, &clock_speed, 0, ' ') != parse_ok)
		goto usage;

	clock_speed_enum = (spi_clock_t)clock_speed;

	if((clock_speed_enum <= spi_clock_none) || (clock_speed_enum >= spi_clock_size))
		goto usage;

	if((parse_uint(2, src, &command_length, 0, ' ') != parse_ok) || (parse_uint(3, src, &command, 16, ' ') != parse_ok))
		goto usage;

	if((parse_uint(4, src, &address_length, 0, ' ') != parse_ok) || (parse_uint(5, src, &address, 16, ' ') != parse_ok))
		goto usage;

	if(parse_uint(6, src, &skip_length, 0, ' ') != parse_ok)
		goto usage;

	if(parse_uint(7, src, &receive_bytes, 0, ' ') != parse_ok)
		goto usage;

	if(!spi_transmit(&error, clock_speed_enum, command_length, command, address_length, address, skip_length, receive_bytes))
	{
		string_format(dst, "spi transmit failed: %s\n", string_to_cstr(&error));
		return(app_action_error);
	}

	string_format(dst, "spi transmit ok\n");

	return(app_action_normal);

usage:
	string_append_cstr_flash(dst, help_description_spi_transmit);
	string_append(dst, ">\n");

	for(clock_map_ptr = spi_clock_map; clock_map_ptr->clock != spi_clock_none; clock_map_ptr++)
		string_format(dst, ">    %2u %10u\n", clock_map_ptr->clock, (unsigned int)(80000000.0 / clock_map_ptr->pre_div / clock_map_ptr->div));

	return(app_action_error);
}

roflash const char help_description_spi_receive[] = "fetch data received by SPI transaction\n"
		"usage: spr <number of bytes to read (0 - 63)>\n";

app_action_t application_function_spi_receive(string_t *src, string_t *dst)
{
	string_new(, error, 64);
	unsigned int current, length;
	uint8_t buffer[64];

	if(parse_uint(1, src, &length, 0, ' ') != parse_ok)
		goto usage;

	if(length > 64)
		goto usage;

	if(!spi_receive(&error, length, buffer))
	{
		string_format(dst, "spi receive failed: %s\n", string_to_cstr(&error));
		return(app_action_error);
	}

	string_append(dst, "spi receive ok, received: ");

	for(current = 0; current < length; current++)
		string_format(dst, "%02x ", buffer[current]);

	string_append(dst, "\n");
	return(app_action_normal);

usage:
	string_append_cstr_flash(dst, help_description_spi_receive);
	return(app_action_error);
}

roflash const char help_description_spi_finish[] = "finish SPI transaction\n"
		"usage: spf\n";

app_action_t application_function_spi_finish(string_t *src, string_t *dst)
{
	string_new(, error, 64);

	if(!spi_finish(&error))
	{
		string_format(dst, "spi finish failed: %s\n", string_to_cstr(&error));
		return(app_action_error);
	}

	string_append(dst, "spi finish ok\n");
	return(app_action_normal);
}
