#include "attribute.h"
#include "sys_string.h"
#include "application.h"
#include "spi.h"
#include "util.h"
#include "io_gpio.h"
#include "sys_time.h"

static bool spi_inited;

typedef struct
{
	const spi_clock_t	clock;
	const unsigned int	pre_div;
	const unsigned int	div;
} spi_clock_map_t;

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

bool spi_init(unsigned int io, unsigned int pin_miso, unsigned int pin_mosi, unsigned int pin_sclk, unsigned int pin_cs, string_t *error)
{
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

	write_peri_reg(PERIPHS_IO_MUX, PERIPHS_IO_MUX_HSPI_ENABLE);

	spi_inited = true;

	return(true);
}

bool spi_send_receive(spi_clock_t clock, spi_mode_t mode, bool cs_hold, int cs_io, int cs_pin,
		bool send_command, uint8_t command,
		unsigned int send_amount, const uint8_t *send_data, unsigned int skip_amount, unsigned int receive_amount, uint8_t *receive_data,
		string_t *error)
{
	unsigned int current, current_value;
	unsigned int clock_pre_div, clock_div;
	unsigned int clock_high, clock_low;
	unsigned int spi_user_static, spi_user_active;
	unsigned int spi_pin_mode_static, spi_pin_mode_active;
	unsigned int spi_user1_static, spi_user1_active;
	unsigned int spi_user2_static, spi_user2_active;
	const spi_clock_map_t *clock_map_ptr;
	bool success;

	if(!spi_inited)
	{
		if(error)
			string_append(error, "spi: not inited");
		return(false);
	}

	if((cs_io == -1) && (cs_pin != -1))
	{
		if(error)
			string_append(error, "spi: invalid cs");
		return(false);
	}

	if((cs_io != -1) && (cs_pin == -1))
	{
		if(error)
			string_append(error, "spi: invalid cs");
		return(false);
	}

	if(send_amount > 64)
	{
		if(error)
			string_append(error, "spi: send amount > 64");
		return(false);
	}

	if(receive_amount > 64)
	{
		if(error)
			string_append(error, "spi: receive amount > 64");
		return(false);
	}

	spi_user_static =		0x00;
	spi_user_active =		0x00;
	spi_user1_static =		0x00;
	spi_user1_active =		0x00;
	spi_user2_static =		0x00;
	spi_user2_active =		0x00;
	spi_pin_mode_static =	0x00;
	spi_pin_mode_active =	0x00;

	for(clock_map_ptr = spi_clock_map; clock_map_ptr != spi_clock_none; clock_map_ptr++)
		if(clock_map_ptr->clock == clock)
			break;

	if(clock_map_ptr->clock == spi_clock_none)
	{
		if(error)
			string_append(error, "spi: invalid speed");
		return(false);
	}

	clock_pre_div = clock_map_ptr->pre_div - 1;
	clock_div =		clock_map_ptr->div - 1;
	clock_high =	clock_div;
	clock_low =		clock_div / 2;

	if((mode <= spi_mode_none) || (mode >= spi_mode_size))
	{
		if(error)
			string_append(error, "spi: invalid mode");
		return(false);
	}

	spi_pin_mode_static |=	mode_table[(unsigned int)mode][0] ? SPI_IDLE_EDGE : 0;		// CPOL
	spi_user_static |=		mode_table[(unsigned int)mode][1] ? SPI_CK_OUT_EDGE : 0;	// CPHA

	spi_user_static |= cs_hold ? (SPI_CS_SETUP | SPI_CS_HOLD) : 0x00;

	if(send_command)
	{
		spi_user_active |= SPI_USR_COMMAND;
		spi_user2_active |= (((1 * 8) - 1) & SPI_USR_COMMAND_BITLEN) << SPI_USR_COMMAND_BITLEN_S;
		spi_user2_active |= (command & SPI_USR_COMMAND_VALUE) << SPI_USR_COMMAND_VALUE_S;
	}

	if(skip_amount > 0)
	{
		spi_user_active |= SPI_USR_DUMMY;
		spi_user1_active |= ((skip_amount - 1) & SPI_USR_DUMMY_CYCLELEN) << SPI_USR_DUMMY_CYCLELEN_S;
	}

	if(receive_amount > 0)
	{
		spi_user_active |= SPI_USR_MISO;
		spi_user1_active |= (((receive_amount * 8) - 1) & SPI_USR_MISO_BITLEN) << SPI_USR_MISO_BITLEN_S;
	}

	if(cs_io != -1)
		spi_pin_mode_active |= SPI_CS0_DIS;

	spi_wait_completion();

	write_peri_reg(SPI_CLOCK(1),
			((clock_pre_div	& SPI_CLKDIV_PRE)	<< SPI_CLKDIV_PRE_S)	|
			((clock_div		& SPI_CLKCNT_N)		<< SPI_CLKCNT_N_S)		|
			((clock_high	& SPI_CLKCNT_H)		<< SPI_CLKCNT_H_S)		|
			((clock_low		& SPI_CLKCNT_L)		<< SPI_CLKCNT_L_S));

	if(send_amount > 0)
	{
		spi_user_active |= SPI_USR_MOSI;
		spi_user1_active |= (((send_amount * 8) - 1) & SPI_USR_MOSI_BITLEN) << SPI_USR_MOSI_BITLEN_S;

		for(current = 0, current_value = 0; current < send_amount; current++)
		{
			current_value |= (send_data[current] & 0xff) << ((current & 0x03) << 3);

			if((current & 0x03) == 0x03)
			{
				write_peri_reg(SPI_W0(1) + (current & ~0x03), current_value);
				current_value = 0;
			}
		}

		if((current & 0x03) != 0)
			write_peri_reg(SPI_W0(1) + (current & ~0x03), current_value);
	}

	write_peri_reg(SPI_USER(1), spi_user_static | spi_user_active);
	write_peri_reg(SPI_USER1(1), spi_user1_static | spi_user1_active);
	write_peri_reg(SPI_USER2(1), spi_user2_static | spi_user2_active);
	write_peri_reg(SPI_PIN(1), spi_pin_mode_static | spi_pin_mode_active );

	if(cs_io != -1)
		success = io_write_pin(error, cs_io, cs_pin, 1) == io_ok;
	else
		success = true;

	if(success)
	{
		set_peri_reg_mask(SPI_CMD(1), SPI_USR);
		spi_wait_completion();

		if(cs_io != -1)
			success = io_write_pin(error, cs_io, cs_pin, 0) == io_ok;
		else
			success = true;
	}

	write_peri_reg(SPI_USER(1), spi_user_static);
	write_peri_reg(SPI_USER1(1), spi_user1_static);
	write_peri_reg(SPI_USER2(1), spi_user2_static);
	write_peri_reg(SPI_PIN(1), spi_pin_mode_static);

	if(!success)
		return(false);

	if(receive_amount > 0)
	{
		for(current = 0, current_value = 0; current < receive_amount; current++)
		{
			if((current & 0x03) == 0x00)
				current_value = read_peri_reg(SPI_W0(1) + (current & ~0x03));

			receive_data[current] = (current_value >> ((current & 0x03) << 3)) & 0xff;
		}
	}

	return(true);
}

app_action_t application_function_spi_write_read(string_t *src, string_t *dst)
{
	uint8_t			sendbytes[64];
	uint8_t			receivebytes[64];
	unsigned int	current;
	unsigned int	byte;
	unsigned int	clock_speed;
	spi_clock_t		clock_speed_enum;
	unsigned int	spi_mode;
	spi_mode_t		spi_mode_enum;
	unsigned int	cs_hold;
	int				cs_io, cs_pin;
	unsigned int	to_write;
	unsigned int	to_skip;
	unsigned int	to_read;
	const			spi_clock_map_t *clock_map_ptr;

	if(parse_uint(1, src, &clock_speed, 0, ' ') != parse_ok)
		goto usage;

	clock_speed_enum = (spi_clock_t)clock_speed;

	if((clock_speed_enum <= spi_clock_none) || (clock_speed_enum >= spi_clock_size))
		goto usage;

	if(parse_uint(2, src, &spi_mode, 0, ' ') != parse_ok)
		goto usage;

	spi_mode_enum = (spi_mode_t)(spi_mode + spi_mode_0);

	if((spi_mode_enum <= spi_mode_none) || (spi_mode_enum >= spi_mode_size))
	{
		string_append(dst, "spi_mode: 0, 1, 2, 3\n");
		goto usage;
	}

	if(parse_uint(3, src, &cs_hold, 0, ' ') != parse_ok)
		goto usage;

	if(cs_hold > 1)
	{
		string_append(dst, "cs_hold: 0 or 1\n");
		goto usage;
	}

	if(parse_int(4, src, &cs_io, 0, ' ') != parse_ok)
		goto usage;

	if((cs_io < -1) || (cs_io > 15))
	{
		string_append(dst, "cs_io: -1, 0 - 15\n");
		goto usage;
	}

	if(parse_int(5, src, &cs_pin, 0, ' ') != parse_ok)
		goto usage;

	if((cs_pin < -1) || (cs_pin >= max_pins_per_io))
	{
		string_append(dst, "cs_pin: -1, 0 - 15\n");
		goto usage;
	}

	if(parse_uint(6, src, &to_read, 0, ' ') != parse_ok)
		goto usage;

	if(to_read > (int)sizeof(receivebytes))
	{
		string_format(dst, "swr: max read %u bytes\n", sizeof(receivebytes));
		goto usage;
	}

	if(parse_uint(7, src, &to_skip, 0, ' ') != parse_ok)
		goto usage;

	for(to_write = 0; to_write <= sizeof(sendbytes); to_write++)
	{
		if(parse_uint(to_write + 8, src, &byte, 16, ' ') != parse_ok)
			break;

		sendbytes[to_write] = (uint8_t)(byte & 0xff);
	}

	if(!spi_send_receive(clock_speed_enum, spi_mode_enum, cs_hold != 0, cs_io, cs_pin, false, 0, to_write, sendbytes, to_skip, to_read, receivebytes, dst))
	{
		string_append(dst, "\n");
		return(app_action_error);
	}

	string_format(dst, "spiwriteread: written %u bytes, received %u bytes and skipped %u cycles:", to_write, to_read, to_skip);

	for(current = 0; current < to_read; current++)
		string_format(dst, " %02x", receivebytes[current]);

	string_append(dst, "\n");

	return(app_action_normal);

usage:
	string_append(dst, "usage: swr <clock speed index> <spi mode> <cs hold> <cs io> <cs pin> <# of bytes to read> <skip cycles> <bytes to send ...>\n");
	string_append(dst, "> clock speed index:\n");

	for(clock_map_ptr = spi_clock_map; clock_map_ptr->clock != spi_clock_none; clock_map_ptr++)
		string_format(dst, ">    %2u %10u\n", clock_map_ptr->clock, (unsigned int)(80000000.0 / clock_map_ptr->pre_div / clock_map_ptr->div));

	string_append(dst, "> spi mode:\n");
	string_append(dst, ">    0 clk=0 pha=0\n");
	string_append(dst, ">    1 clk=0 pha=1\n");
	string_append(dst, ">    2 clk=1 pha=0\n");
	string_append(dst, ">    3 clk=1 pha=1\n");

	return(app_action_error);
}
