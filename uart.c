#include "uart.h"

#include "queue.h"
#include "user_main.h"
#include "stats.h"
#include "util.h"

#include <os_type.h>
#include <ets_sys.h>
#include <user_interface.h>

#include "esp-uart-register.h"

ICACHE_FLASH_ATTR uart_parity_t uart_string_to_parity(const char *str)
{
	uart_parity_t rv;

	if(!strcmp(str, "none"))
		rv = parity_none;
	else if(!strcmp(str, "even"))
		rv = parity_even;
	else if(!strcmp(str, "odd"))
		rv = parity_odd;
	else
		rv = parity_error;

	return(rv);
}

ICACHE_FLASH_ATTR const char *uart_parity_to_string(uart_parity_t ix)
{
	static const char *parity[] =
	{
		"none",
		"even",
		"odd",
	};

	if(ix > parity_odd)
		return("error");

	return(parity[ix]);
}

ICACHE_FLASH_ATTR char uart_parity_to_char(uart_parity_t ix)
{
	static const char *parity = "NEO";

	if(ix > parity_odd)
		return('-');

	return(parity[ix]);
}

ICACHE_FLASH_ATTR uint16_t uart_parameters_to_string(const uart_parameters_t *params, uint16_t size, char *string)
{
	snprintf(string, size, "%u %u%c%u",
			params->baud_rate,
			params->data_bits,
			uart_parity_to_char(params->parity),
			params->stop_bits);

	return(strlen(string));
}

static uint16_t uart_rx_fifo_length(void)
{
	return((READ_PERI_REG(UART_STATUS(0)) >> UART_RXFIFO_CNT_S) & UART_RXFIFO_CNT);
}

static uint16_t uart_tx_fifo_length(void)
{
	return((READ_PERI_REG(UART_STATUS(0)) >> UART_TXFIFO_CNT_S) & UART_TXFIFO_CNT);
}

static void uart_callback(void *p)
{
	char data;

	ETS_UART_INTR_DISABLE();

	// receive fifo "timeout" or "full" -> data available

	if(READ_PERI_REG(UART_INT_ST(0)) & (UART_RXFIFO_TOUT_INT_ST | UART_RXFIFO_FULL_INT_ST))
	{
		stat_uart_rx_interrupts++;

		// make sure to fetch all data from the fifo, or we'll get a another
		// interrupt immediately after we enable it

		while(uart_rx_fifo_length() > 0)
		{
			data = READ_PERI_REG(UART_FIFO(0));

			if(!queue_full(uart_receive_queue))
				queue_push(uart_receive_queue, data);
		}

		system_os_post(background_task_id, 0, 0);
	}

	// receive transmit fifo "empty", room for new data in the fifo

	if(READ_PERI_REG(UART_INT_ST(0)) & UART_TXFIFO_EMPTY_INT_ST)
	{
		stat_uart_tx_interrupts++;

		while(!queue_empty(uart_send_queue) && (uart_tx_fifo_length() < 64))
			WRITE_PERI_REG(UART_FIFO(0), queue_pop(uart_send_queue));

		uart_start_transmit(!queue_empty(uart_send_queue));
	}

	// acknowledge all uart interrupts

	WRITE_PERI_REG(UART_INT_CLR(0), 0xffff);
	ETS_UART_INTR_ENABLE();
}

ICACHE_FLASH_ATTR void uart_init(const uart_parameters_t *params)
{
	uint8_t data_bits;
	uint8_t stop_bits;
	uart_parity_t parity;

	ETS_UART_INTR_DISABLE();

	ETS_UART_INTR_ATTACH(uart_callback,  0);

	PIN_PULLUP_DIS(PERIPHS_IO_MUX_U0TXD_U);
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_U0TXD_U, FUNC_U0TXD);

	WRITE_PERI_REG(UART_CLKDIV(0), UART_CLK_FREQ / params->baud_rate);

	data_bits = params->data_bits - 5;

	if(params->stop_bits == 2)
		stop_bits = 0x03;
	else
		stop_bits = 0x01;

	if(params->parity == parity_odd)
		parity = UART_PARITY_EN | UART_PARITY;
	else if(params->parity == parity_even)
		parity = UART_PARITY_EN;
	else
		parity = 0;

	WRITE_PERI_REG(UART_CONF0(0),
			((data_bits & UART_BIT_NUM) << UART_BIT_NUM_S) |
			((stop_bits & UART_STOP_BIT_NUM) << UART_STOP_BIT_NUM_S) |
			parity);

	SET_PERI_REG_MASK(UART_CONF0(0), UART_RXFIFO_RST | UART_TXFIFO_RST);
	CLEAR_PERI_REG_MASK(UART_CONF0(0), UART_RXFIFO_RST | UART_TXFIFO_RST);

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

	WRITE_PERI_REG(UART_CONF1(0),
			(( 2 & UART_RX_TOUT_THRHD) << UART_RX_TOUT_THRHD_S) | UART_RX_TOUT_EN |
			((16 & UART_RXFIFO_FULL_THRHD) << UART_RXFIFO_FULL_THRHD_S) |
			((64 & UART_TXFIFO_EMPTY_THRHD) << UART_TXFIFO_EMPTY_THRHD_S));

	WRITE_PERI_REG(UART_INT_CLR(0), 0xffff);
	WRITE_PERI_REG(UART_INT_ENA(0), UART_RXFIFO_TOUT_INT_ENA | UART_RXFIFO_FULL_INT_ENA);

	ETS_UART_INTR_ENABLE();
}

void uart_start_transmit(char c)
{
	if(c)
		SET_PERI_REG_MASK(UART_INT_ENA(0), UART_TXFIFO_EMPTY_INT_ENA);
	else
		CLEAR_PERI_REG_MASK(UART_INT_ENA(0), UART_TXFIFO_EMPTY_INT_ENA);
}
