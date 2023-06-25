#ifndef _spi_h_
#define _spi_h_

#include "io_gpio.h"
#include "stats.h"
#include "eagle.h"

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

extern const char help_description_spi_configure[];
extern const char help_description_spi_start[];
extern const char help_description_spi_write[];
extern const char help_description_spi_transmit[];
extern const char help_description_spi_receive[];
extern const char help_description_spi_finish[];

attr_result_used bool spi_init(string_t *error, unsigned int io);
attr_result_used bool spi_configure(string_t *error, spi_mode_t mode, bool cs_hold, int user_cs_io, int user_cs_pin);
attr_result_used bool spi_start(string_t *error);
attr_result_used unsigned int spi_write_bits_used(void);
attr_result_used unsigned int spi_write_bits_available(void);
attr_result_used bool spi_write(unsigned int bits, uint32_t value);
attr_result_used bool spi_transmit(string_t *error, spi_clock_t clock,
		unsigned int command_length_bits, unsigned int command, unsigned int address_length_bits, unsigned int address, unsigned int skip_bits, unsigned int receive_bytes);
attr_result_used bool spi_receive(string_t *error, unsigned int receive_bytes, uint8_t *receive_data);
attr_result_used bool spi_finish(string_t *error);

attr_result_used app_action_t application_function_spi_configure(app_params_t *);
attr_result_used app_action_t application_function_spi_start(app_params_t *);
attr_result_used app_action_t application_function_spi_write(app_params_t *);
attr_result_used app_action_t application_function_spi_transmit(app_params_t *);
attr_result_used app_action_t application_function_spi_receive(app_params_t *);
attr_result_used app_action_t application_function_spi_finish(app_params_t *);

#endif
