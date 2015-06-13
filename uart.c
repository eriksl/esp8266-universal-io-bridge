#include "uart.h"
#include "fifo.h"
#include "user_main.h"

#include <os_type.h>
#include <ets_sys.h>
#include <user_interface.h>

#include "esp-uart.h"
#include "esp-uart-register.h"
#include "esp-missing-decls.h"

void debug(char c)
{
	WRITE_PERI_REG(UART_FIFO(0), c);
}

static uint16_t uart_rx_queue_length(void)
{
	return((READ_PERI_REG(UART_STATUS(0)) >> UART_RXFIFO_CNT_S) & UART_RXFIFO_CNT);
}

static uint16_t uart_tx_queue_length(void)
{
	return((READ_PERI_REG(UART_STATUS(0)) >> UART_TXFIFO_CNT_S) & UART_TXFIFO_CNT);
}

static void uart_callback(void *p)
{
	char data;

	ETS_UART_INTR_DISABLE();

	// receive queue "timeout" or "full" -> data available

	if(READ_PERI_REG(UART_INT_ST(0)) & (UART_RXFIFO_TOUT_INT_ST | UART_RXFIFO_FULL_INT_ST))
	{
		// make sure to fetch all data from the queue, or we'll get a another
		// interrupt immediately after we enable it

		while(uart_rx_queue_length() > 0)
		{
			data = READ_PERI_REG(UART_FIFO(0));

			if(!fifo_full(uart_receive_fifo))
				fifo_push(uart_receive_fifo, data);
		}

		system_os_post(background_task_id, 0, 0);
	}

	// receive transmit queue "empty", room for new data in the queue

	if(READ_PERI_REG(UART_INT_ST(0)) & UART_TXFIFO_EMPTY_INT_ST)
	{
		while(!fifo_empty(uart_send_fifo) && (uart_tx_queue_length() < 64))
			WRITE_PERI_REG(UART_FIFO(0), fifo_pop(uart_send_fifo));

		uart_start_transmit(!fifo_empty(uart_send_fifo));
	}

	// acknowledge all uart interrupts

	WRITE_PERI_REG(UART_INT_CLR(0), 0xfff);
	ETS_UART_INTR_ENABLE();
}

void uart_init(void)
{
	ETS_UART_INTR_DISABLE();

	ETS_UART_INTR_ATTACH(uart_callback,  0);

	PIN_PULLUP_DIS(PERIPHS_IO_MUX_U0TXD_U);
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_U0TXD_U, FUNC_U0TXD);

	uart_div_modify(0, UART_CLK_FREQ / 460800);

	WRITE_PERI_REG(UART_CONF0(0), CALC_UARTMODE(EIGHT_BITS, NONE_BITS, ONE_STOP_BIT));

	SET_PERI_REG_MASK(UART_CONF0(0), UART_RXFIFO_RST | UART_TXFIFO_RST);
	CLEAR_PERI_REG_MASK(UART_CONF0(0), UART_RXFIFO_RST | UART_TXFIFO_RST);

	// Set receive queue "timeout" threshold.
	// when no data comes in for this amount of bytes' times and the queue
	// isn't empty, raise an interrupt.

	// Set receive queue "full" threshold.
	// When the queue grows beyond this threshold, raise an interrupt.

	// Set transmit queue "empty" threshold.
	// If the queue contains less than this numbers of bytes, raise an
	// interrupt.
	// Don't enable the interrupt here but enable it when our fifo has
	// something in it that should be written to the uart's queue, see
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
