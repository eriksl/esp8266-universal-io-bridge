#include "uart.h"

#include "queue.h"
#include "user_main.h"
#include "stats.h"
#include "util.h"
#include "io_gpio.h"

#include <os_type.h>
#include <ets_sys.h>
#include <user_interface.h>

#include "esp-uart-register.h"

irom attr_pure uart_parity_t uart_string_to_parity(const string_t *src)
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

irom void uart_parity_to_string(string_t *dst, uart_parity_t ix)
{
	static const char *parity[] =
	{
		"none",
		"even",
		"odd",
	};

	string_format(dst, "%s", ix <= parity_odd ? parity[ix] : "<error>");
}

irom attr_pure attr_const char uart_parity_to_char(uart_parity_t ix)
{
	static const char *parity = "NEO";

	if(ix > parity_odd)
		return('-');

	return(parity[ix]);
}

irom void uart_parameters_to_string(string_t *dst, const uart_parameters_t *params)
{
	string_format(dst, "%u %u%c%u",
			params->baud_rate,
			params->data_bits,
			uart_parity_to_char(params->parity),
			params->stop_bits);
}

iram static int uart_rx_fifo_length(void)
{
	return((read_peri_reg(UART_STATUS(0)) >> UART_RXFIFO_CNT_S) & UART_RXFIFO_CNT);
}

iram static int uart_tx_fifo_length(void)
{
	return((read_peri_reg(UART_STATUS(0)) >> UART_TXFIFO_CNT_S) & UART_TXFIFO_CNT);
}

iram static void uart_callback(void *p)
{
	char data;

	ETS_UART_INTR_DISABLE();

	// receive fifo "timeout" or "full" -> data available

	if(read_peri_reg(UART_INT_ST(0)) & (UART_RXFIFO_TOUT_INT_ST | UART_RXFIFO_FULL_INT_ST))
	{
		stat_uart_rx_interrupts++;

		// make sure to fetch all data from the fifo, or we'll get a another
		// interrupt immediately after we enable it

		while(uart_rx_fifo_length() > 0)
		{
			data = read_peri_reg(UART_FIFO(0));

			if(!queue_full(&uart_receive_queue))
				queue_push(&uart_receive_queue, data);
		}

		system_os_post(background_task_id, 0, 0);
	}

	// receive transmit fifo "empty", room for new data in the fifo

	if(read_peri_reg(UART_INT_ST(0)) & UART_TXFIFO_EMPTY_INT_ST)
	{
		stat_uart_tx_interrupts++;

		while(!queue_empty(&uart_send_queue) && (uart_tx_fifo_length() < 64))
			write_peri_reg(UART_FIFO(0), queue_pop(&uart_send_queue));

		uart_start_transmit(!queue_empty(&uart_send_queue));
	}

	// acknowledge all uart interrupts

	write_peri_reg(UART_INT_CLR(0), 0xffff);
	ETS_UART_INTR_ENABLE();
}

irom void uart_init(int baud, int data_bits, int stop_bits, uart_parity_t parity)
{
	int data_mask, stop_mask, parity_mask;

	ETS_UART_INTR_DISABLE();
	ETS_UART_INTR_ATTACH(uart_callback,  0);

	write_peri_reg(UART_CLKDIV(0), UART_CLK_FREQ / baud);

	data_mask = data_bits - 5;

	if(stop_bits == 2)
		stop_mask = 0x03;
	else
		stop_mask = 0x01;

	switch(parity)
	{
		case(parity_odd): parity_mask = UART_PARITY_EN | UART_PARITY; break;
		case(parity_even): parity_mask = UART_PARITY_EN; break;
		default: parity_mask = 0; break;
	}

	write_peri_reg(UART_CONF0(0),
			((data_mask & UART_BIT_NUM) << UART_BIT_NUM_S) |
			((stop_mask & UART_STOP_BIT_NUM) << UART_STOP_BIT_NUM_S) |
			parity_mask);

	set_peri_reg_mask(UART_CONF0(0), UART_RXFIFO_RST | UART_TXFIFO_RST);
	clear_peri_reg_mask(UART_CONF0(0), UART_RXFIFO_RST | UART_TXFIFO_RST);

	// Set receive fifo "timeout" threshold.
	// when no data comes in for this amount of bytes' times and the fifo
	// isn't empty, raise an interrupt.

	// Set receive fifo "full" threshold.
	// When the fifo grows beyond this threshold, raise an interrupt.

	// Set transmit fifo "empty" threshold.
	// If the fifo contains less than this numbers of bytes, raise an
	// interrupt.
	// Don't enable the interrupt here but enable it when the fifo has
	// something in it that should be written to the uart's fifo, see
	// uart_start_transmit().

	write_peri_reg(UART_CONF1(0),
			(( 2 & UART_RX_TOUT_THRHD) << UART_RX_TOUT_THRHD_S) | UART_RX_TOUT_EN |
			((16 & UART_RXFIFO_FULL_THRHD) << UART_RXFIFO_FULL_THRHD_S) |
			((64 & UART_TXFIFO_EMPTY_THRHD) << UART_TXFIFO_EMPTY_THRHD_S));

	write_peri_reg(UART_INT_CLR(0), 0xffff);
	write_peri_reg(UART_INT_ENA(0), UART_RXFIFO_TOUT_INT_ENA | UART_RXFIFO_FULL_INT_ENA);

	ETS_UART_INTR_ENABLE();
}

iram void uart_start_transmit(char c)
{
	if(c)
		set_peri_reg_mask(UART_INT_ENA(0), UART_TXFIFO_EMPTY_INT_ENA);
	else
		clear_peri_reg_mask(UART_INT_ENA(0), UART_TXFIFO_EMPTY_INT_ENA);
}
