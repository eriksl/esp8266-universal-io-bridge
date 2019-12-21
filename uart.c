#include "uart.h"

#include "dispatch.h"
#include "stats.h"
#include "io_gpio.h"
#include "sdk.h"
#include "eagle.h"

#include <stdint.h>
#include <stdbool.h>

typedef struct
{
	bool			enabled;
	unsigned int	character;
} autofill_info_t;

static bool init_done = false;

static autofill_info_t autofill_info[2] =
{
	{ false, 0 },
	{ false, 0 },
};

static queue_t uart_send_queue[2];
static queue_t uart_receive_queue;

attr_inline int rx_fifo_length(unsigned int uart)
{
	return((read_peri_reg(UART_STATUS(uart)) >> UART_RXFIFO_CNT_S) & UART_RXFIFO_CNT);
}

attr_inline int tx_fifo_length(unsigned int uart)
{
	return((read_peri_reg(UART_STATUS(uart)) >> UART_TXFIFO_CNT_S) & UART_TXFIFO_CNT);
}

attr_inline void enable_transmit_int(unsigned int uart, bool enable)
{
	if(enable)
		set_peri_reg_mask(UART_INT_ENA(uart), UART_TXFIFO_EMPTY_INT_ENA);
	else
		clear_peri_reg_mask(UART_INT_ENA(uart), UART_TXFIFO_EMPTY_INT_ENA);
}

attr_inline void enable_receive_int(unsigned int uart, bool enable)
{
	if(enable)
		set_peri_reg_mask(UART_INT_ENA(uart), UART_RXFIFO_TOUT_INT_ENA | UART_RXFIFO_FULL_INT_ENA);
	else
		clear_peri_reg_mask(UART_INT_ENA(uart), UART_RXFIFO_TOUT_INT_ENA | UART_RXFIFO_FULL_INT_ENA);
}

attr_inline void clear_interrupts(unsigned int uart)
{
	write_peri_reg(UART_INT_CLR(uart), 0xffff);
}

static void clear_fifos(unsigned int uart)
{
	set_peri_reg_mask(UART_CONF0(uart), UART_RXFIFO_RST | UART_TXFIFO_RST);
	clear_peri_reg_mask(UART_CONF0(uart), UART_RXFIFO_RST | UART_TXFIFO_RST);
}

iram static void uart_callback(void *p)
{
	unsigned int uart0_int_status, uart1_int_status;

	ets_isr_mask(1 << ETS_UART_INUM);

	uart0_int_status = read_peri_reg(UART_INT_ST(0));
	uart1_int_status = read_peri_reg(UART_INT_ST(1));

	if(uart0_int_status & (UART_RXFIFO_TOUT_INT_ST | UART_RXFIFO_FULL_INT_ST)) // data in input fifo of uart0
	{
		stat_uart0_rx_interrupts++;
		enable_receive_int(0, false); // disable input info data available interrupts while the fifo is not empty
		dispatch_post_task(0, task_uart_fetch_fifo, 0);
	}

	if(uart0_int_status & UART_TXFIFO_EMPTY_INT_ST) // space available in the output fifo of uart0
	{
		stat_uart0_tx_interrupts++;
		enable_transmit_int(0, false); // disable output fifo space available interrupts while the fifo hasn't been filled
		dispatch_post_task(0, task_uart_fill_fifo, 0);
	}

	if(uart1_int_status & UART_TXFIFO_EMPTY_INT_ST) // space available in the output fifo of uart1
	{
		stat_uart1_tx_interrupts++;
		enable_transmit_int(1, false); // disable output fifo space available interrupts while the fifo hasn't been filled
		dispatch_post_task(0, task_uart_fill_fifo, 1);
	}

	// acknowledge all uart interrupts

	clear_interrupts(0);
	clear_interrupts(1);

	ets_isr_unmask(1 << ETS_UART_INUM);
}

iram attr_pure bool uart_full(unsigned int uart)
{
	if(!init_done)
		return(true);

	return(queue_full(&uart_send_queue[uart]));
}

iram void uart_send(unsigned int uart, unsigned int byte)
{
	if(!init_done)
		return;

	if(!queue_full(&uart_send_queue[uart]))
		queue_push(&uart_send_queue[uart], byte);
}

iram void uart_send_string(unsigned int uart, const string_t *string)
{
	unsigned int current, length;

	if(!init_done)
		return;

	for(current = 0, length = string_length(string); (current < length) && !queue_full(&uart_send_queue[uart]); current++)
		queue_push(&uart_send_queue[uart], string_at(string, current));

	uart_flush(uart);
}

iram void uart_flush(unsigned int uart)
{
	if(!init_done)
		return;

	enable_transmit_int(uart, !queue_empty(&uart_send_queue[uart]));
}

iram attr_pure bool uart_empty(unsigned int uart)
{
	if(!init_done)
		return(false);

	return(queue_empty(&uart_receive_queue));
}

iram unsigned int uart_receive(unsigned int uart)
{
	if(!init_done)
		return(-1);

	return(queue_pop(&uart_receive_queue));
}

iram void uart_clear_receive_queue(unsigned int uart)
{
	if(!init_done)
		return;

	queue_flush(&uart_receive_queue);
}

void uart_task_handler_fetch_fifo(unsigned int uart)
{
	unsigned int byte;

	// make sure to fetch all data from the fifo, or we'll get a another
	// interrupt immediately after we enable it

	while(rx_fifo_length(uart) > 0)
	{
		byte = read_peri_reg(UART_FIFO(uart));

		if(!queue_full(&uart_receive_queue))
			queue_push(&uart_receive_queue, byte);
	}

	clear_interrupts(uart);
	enable_receive_int(uart, true);

	if(uart_bridge_active)
		dispatch_post_task(0, task_uart_bridge, 0);
}

void uart_task_handler_fill_fifo(unsigned int uart)
{
	if(autofill_info[uart].enabled)
	{
		while(tx_fifo_length(uart) < 128)
			write_peri_reg(UART_FIFO(uart), autofill_info[uart].character);

		clear_interrupts(uart);
		enable_transmit_int(uart, true);
	}
	else
	{
		while(!queue_empty(&uart_send_queue[uart]) && (tx_fifo_length(uart) < 128))
			write_peri_reg(UART_FIFO(uart), queue_pop(&uart_send_queue[uart]));

		clear_interrupts(uart);
		enable_transmit_int(uart, !queue_empty(&uart_send_queue[uart]));
	}
}

void uart_baudrate(unsigned int uart, unsigned int baudrate)
{
	if(baudrate == 0)
		return;

	clear_fifos(uart);
	write_peri_reg(UART_CLKDIV(uart), UART_CLK_FREQ / baudrate);
}

void uart_data_bits(unsigned int uart, unsigned int data_bits)
{
	if((data_bits > 4) && (data_bits < 9))
		data_bits -= 5;
	else
		data_bits = 8 - 5;

	clear_fifos(uart);

	clear_set_peri_reg_mask(UART_CONF0(uart),
			(0xff		& UART_BIT_NUM) << UART_BIT_NUM_S,
			(data_bits	& UART_BIT_NUM) << UART_BIT_NUM_S);
}

void uart_stop_bits(unsigned int uart, unsigned int stop_bits)
{
	switch(stop_bits)
	{
		case(2): stop_bits = 0x03; break;
		default: stop_bits = 0x01; break;
	}

	clear_fifos(uart);

	clear_set_peri_reg_mask(UART_CONF0(uart),
			(				UART_STOP_BIT_NUM) << UART_STOP_BIT_NUM_S,
			(stop_bits &	UART_STOP_BIT_NUM) << UART_STOP_BIT_NUM_S);
}

void uart_parity(unsigned int uart, uart_parity_t parity)
{
	unsigned int parity_mask;

	clear_fifos(uart);

	switch(parity)
	{
		case(parity_odd): parity_mask = UART_PARITY_EN | UART_PARITY; break;
		case(parity_even): parity_mask = UART_PARITY_EN; break;
		default: parity_mask = 0x00; break;
	}

	clear_set_peri_reg_mask(UART_CONF0(uart), UART_PARITY_EN | UART_PARITY,
			(parity_mask & (UART_PARITY_EN | UART_PARITY)));
}

void uart_loopback(unsigned int uart, bool enable)
{
	clear_fifos(uart);

	clear_set_peri_reg_mask(UART_CONF0(uart), UART_LOOPBACK,
			enable ? UART_LOOPBACK : 0);
}

bool uart_invert(unsigned int uart, uart_direction_t dir, bool enable)
{
	if(uart > 1)
		false;

	switch(dir)
	{
		case(uart_dir_rx):
		{
			if(enable)
				clear_set_peri_reg_mask(UART_CONF0(uart), 0, UART_RXD_INV);
			else
				clear_set_peri_reg_mask(UART_CONF0(uart), UART_RXD_INV, 0);

			break;
		}

		case(uart_dir_tx):
		{
			if(enable)
				clear_set_peri_reg_mask(UART_CONF0(uart), 0, UART_TXD_INV);
			else
				clear_set_peri_reg_mask(UART_CONF0(uart), UART_TXD_INV, 0);

			break;
		}

		default:
		{
			return(false);
		}
	}

	return(true);
}

void uart_autofill(unsigned int uart, bool enable, unsigned int character)
{
	if((uart == 0) || (uart == 1))
	{
		autofill_info[uart].enabled = enable;
		autofill_info[uart].character = character;

		enable_transmit_int(uart, enable);
	}
}

void uart_is_autofill(unsigned int uart, bool *enable, unsigned int *character)
{
	if((uart == 0) || (uart == 1))
	{
		*enable = autofill_info[uart].enabled;
		*character = autofill_info[uart].character;
	}
}

void uart_init(void)
{
	static char uart_send_queue_buffer0[1024];
	static char uart_send_queue_buffer1[1024];
	static char uart_receive_queue_buffer[1024];

	ets_isr_mask(1 << ETS_UART_INUM);
	ets_isr_attach(ETS_UART_INUM, uart_callback, 0);

	queue_new(&uart_send_queue[0], sizeof(uart_send_queue_buffer0), uart_send_queue_buffer0);
	queue_new(&uart_send_queue[1], sizeof(uart_send_queue_buffer1), uart_send_queue_buffer1);
	queue_new(&uart_receive_queue, sizeof(uart_receive_queue_buffer), uart_receive_queue_buffer);

	clear_fifos(0);
	clear_fifos(1);

	// Set receive fifo "timeout" threshold.
	// when no data comes in for this amount of bytes' times and the fifo
	// isn't empty, raise an interrupt.

	// Set receive fifo "full" threshold.
	// When the fifo grows beyond this threshold, raise an interrupt.

	// Set transmit fifo "empty" threshold.
	// If the fifo contains less than this numbers of bytes, raise an
	// interrupt.

	uart_invert(0, uart_dir_tx, false);
	uart_invert(0, uart_dir_rx, false);
	uart_invert(1, uart_dir_tx, false);
	uart_invert(1, uart_dir_rx, false);

	write_peri_reg(UART_CONF1(0),
			((2 & UART_RX_TOUT_THRHD) << UART_RX_TOUT_THRHD_S) |
			UART_RX_TOUT_EN |
			((8 & UART_RXFIFO_FULL_THRHD) << UART_RXFIFO_FULL_THRHD_S) |
			((8 & UART_TXFIFO_EMPTY_THRHD) << UART_TXFIFO_EMPTY_THRHD_S));

	write_peri_reg(UART_CONF1(1),
			((8 & UART_TXFIFO_EMPTY_THRHD) << UART_TXFIFO_EMPTY_THRHD_S));

	clear_interrupts(0);
	clear_interrupts(1);

	// Don't enable the send fifo interrupt here but enable it when the fifo has
	// something in it that should be written to the uart's fifo

	enable_receive_int(0, true);
	enable_transmit_int(0, false);
	enable_transmit_int(1, false);

	ets_isr_unmask(1 << ETS_UART_INUM);

	init_done = true;
}

attr_pure uart_parity_t uart_string_to_parity(const string_t *src)
{
	uart_parity_t rv;

	if(string_match_cstr(src, "none"))
		rv = parity_none;
	else if(string_match_cstr(src, "even"))
		rv = parity_even;
	else if(string_match_cstr(src, "odd"))
		rv = parity_odd;
	else
		rv = parity_error;

	return(rv);
}

void uart_parity_to_string(string_t *dst, uart_parity_t ix)
{
	static const char *parity[] =
	{
		"none",
		"even",
		"odd",
	};

	string_format(dst, "%s", ix <= parity_odd ? parity[ix] : "<error>");
}

attr_const char uart_parity_to_char(uart_parity_t ix)
{
	static const char *parity = "NEO";

	if(ix > parity_odd)
		return('-');

	return(parity[ix]);
}

void uart_parameters_to_string(string_t *dst, const uart_parameters_t *params)
{
	string_format(dst, "%lu %u%c%u",
			params->baud_rate,
			params->data_bits,
			uart_parity_to_char(params->parity),
			params->stop_bits);
}

void uart_set_initial(unsigned int uart)
{
	unsigned int baud;
	unsigned int data;
	unsigned int stop;
	unsigned int parity_int;
	uart_parity_t parity;

	if(!config_get_uint("uart.baud.%u", &baud, uart, -1))
		baud = 115200;

	if(!config_get_uint("uart.data.%u", &data, uart, -1))
		data = 8;

	if(!config_get_uint("uart.stop.%u", &stop, uart, -1))
		stop = 1;

	if(config_get_uint("uart.parity.%u", &parity_int, uart, -1))
		parity = (uart_parity_t)parity_int;
	else
		parity = parity_none;

	uart_baudrate(uart, baud);
	uart_data_bits(uart, data);
	uart_stop_bits(uart, stop);
	uart_parity(uart, parity);
}
