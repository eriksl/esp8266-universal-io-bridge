#ifndef _spi_h_
#define _spi_h_

#include "io_gpio.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
	spi_mode_none = 0,	//	CPOL	CPHA
	spi_mode_0,			//	0		0
	spi_mode_1,			//	0		1
	spi_mode_2,			//	1		0
	spi_mode_3,			//	1		1
	spi_mode_size,
} spi_mode_t;

typedef enum
{
	spi_clock_none = 0,
	spi_clock_80M,
	spi_clock_40M,
	spi_clock_27M,
	spi_clock_20M,
	spi_clock_16M,
	spi_clock_13M,
	spi_clock_11M,
	spi_clock_10M,
	spi_clock_9M,
	spi_clock_8M,
	spi_clock_7M,
	spi_clock_6M,
	spi_clock_5M,
	spi_clock_4M,
	spi_clock_3M,
	spi_clock_2M,
	spi_clock_1M,
	spi_clock_500k,
	spi_clock_250k,
	spi_clock_100k,
	spi_clock_50k,
	spi_clock_10k,
	spi_clock_1k,
	spi_clock_size,
} spi_clock_t;

attr_inline bool spi_ready(void)
{
	return((read_peri_reg(SPI_CMD(1)) & SPI_USR) == 0);
}

attr_inline void spi_wait_completion(void)
{
	while(!spi_ready())
		(void)0;
}

attr_result_used bool spi_init(unsigned int io, unsigned int pin_miso, unsigned int pin_mosi, unsigned int pin_spiclk, unsigned int pin_spics, string_t *error);
attr_result_used bool spi_send_receive(spi_clock_t clock, spi_mode_t mode, bool cs_hold, int cs_io, int cs_pin,
						bool send_command, uint8_t command,
						unsigned int send_amount, const uint8_t *send_data, unsigned int skip_amount, unsigned int receive_amount, uint8_t *receive_data,
						string_t *error);
attr_result_used app_action_t application_function_spi_write_read(string_t *src, string_t *dst);

#endif
