#include <c_types.h>
#include <ip_addr.h>
#include <espconn.h>
#include <mem.h>
#include <ets_sys.h>
#include <os_type.h>
#include <user_interface.h>

#include "uart.h"
#include "uart_register.h"
#include "esp-missing-decls.h"

enum
{
	periodic_task_id			= 0,
	periodic_task_queue_length	= 64,
};

static struct espconn	*esp_connection;
static os_event_t		receive_task_queue[periodic_task_queue_length];

ICACHE_FLASH_ATTR static uint8_t uart_rxfifo_error(void)
{
	if((READ_PERI_REG(UART_INT_ST(UART0)) & UART_FRM_ERR_INT_ST) == UART_FRM_ERR_INT_ST)
		return(1);

	return(0);
}

ICACHE_FLASH_ATTR static uint8_t uart_rxfifo_full(void)
{
	if((READ_PERI_REG(UART_INT_ST(UART0)) & UART_RXFIFO_FULL_INT_ST) == UART_RXFIFO_FULL_INT_ST)
		return(1);
	
	return(0);
}

ICACHE_FLASH_ATTR static uint8_t uart_rxfifo_available(void)
{
	if((READ_PERI_REG(UART_INT_ST(UART0)) & UART_RXFIFO_TOUT_INT_ST) == UART_RXFIFO_TOUT_INT_ST)
		return(1);

	return(0);
}

ICACHE_FLASH_ATTR static uint8_t uart_rxfifo_length(void)
{
	uint32_t fifo_length;

	fifo_length = READ_PERI_REG(UART_STATUS(UART0));
	fifo_length = fifo_length & (UART_RXFIFO_CNT << UART_RXFIFO_CNT_S);
	fifo_length = (fifo_length >> UART_RXFIFO_CNT_S) & UART_RXFIFO_CNT;

	return(fifo_length);
}

ICACHE_FLASH_ATTR static uint8_t uart_txfifo_length(void)
{
	uint32_t fifo_length;

	fifo_length = READ_PERI_REG(UART_STATUS(UART0));
	fifo_length = fifo_length & (UART_TXFIFO_CNT << UART_TXFIFO_CNT_S);
	fifo_length = (fifo_length >> UART_TXFIFO_CNT_S) & UART_TXFIFO_CNT;

	return(fifo_length);
}

ICACHE_FLASH_ATTR static void uart_rx_callback(void *p)
{
	if(uart_rxfifo_error())
		WRITE_PERI_REG(UART_INT_CLR(UART0), UART_FRM_ERR_INT_CLR);

	if(uart_rxfifo_full() || uart_rxfifo_available())
	{
		ETS_UART_INTR_DISABLE();
		system_os_post(periodic_task_id, 0, 0);
	}
}

ICACHE_FLASH_ATTR static void uart_init(unsigned int baud_rate, unsigned int bits, unsigned int parity, unsigned int stop_bits)
{
	ETS_UART_INTR_ATTACH(uart_rx_callback,  0);
	PIN_PULLUP_DIS(PERIPHS_IO_MUX_U0TXD_U);
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_U0TXD_U, FUNC_U0TXD);
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDO_U, FUNC_U0RTS);

	uart_div_modify(UART0, UART_CLK_FREQ / baud_rate);

	WRITE_PERI_REG(UART_CONF0(UART0), CALC_UARTMODE(bits, parity, stop_bits));

	SET_PERI_REG_MASK(UART_CONF0(UART0), UART_RXFIFO_RST | UART_TXFIFO_RST);
	CLEAR_PERI_REG_MASK(UART_CONF0(UART0), UART_RXFIFO_RST | UART_TXFIFO_RST);

	WRITE_PERI_REG(UART_CONF1(UART0),
			((0x10 & UART_RXFIFO_FULL_THRHD) << UART_RXFIFO_FULL_THRHD_S) |
			((0x10 & UART_RX_FLOW_THRHD) << UART_RX_FLOW_THRHD_S) |
			UART_RX_FLOW_EN |
			(0x02 & UART_RX_TOUT_THRHD) << UART_RX_TOUT_THRHD_S |
			UART_RX_TOUT_EN);

	SET_PERI_REG_MASK(UART_INT_ENA(UART0), UART_RXFIFO_TOUT_INT_ENA | UART_FRM_ERR_INT_ENA);
	WRITE_PERI_REG(UART_INT_CLR(UART0), 0xffff);
	SET_PERI_REG_MASK(UART_INT_ENA(UART0), UART_RXFIFO_FULL_INT_ENA);

	ETS_UART_INTR_ENABLE();
}

ICACHE_FLASH_ATTR static uint16_t uart_receive(uint16_t size, uint8 *buffer)
{
	uint16_t current;

	for(current = 0; (current < size) && (uart_rxfifo_length() > 0); current++)
		buffer[current] = READ_PERI_REG(UART_FIFO(UART0)) & 0xff;

	return(current);
}

ICACHE_FLASH_ATTR static void uart_transmit(uint16_t length, uint8 *buffer)
{
	uint16_t current;

	for (current = 0; current < length; current++)
	{
		while(uart_txfifo_length() > 126)
			(void)0;

		WRITE_PERI_REG(UART_FIFO(UART0), buffer[current]);
	}
}

ICACHE_FLASH_ATTR static void uart_periodic(os_event_t *events)
{
	char		buffer[256];
	uint16_t	length;

	while(uart_rxfifo_length() > 0)
	{
		//WRITE_PERI_REG(0x60000914, 0x73); // watchdog timer

		length = uart_receive(sizeof(buffer), buffer);

		if((length > 0) && esp_connection)
			espconn_sent(esp_connection, buffer, length);
	}

	if(uart_rxfifo_full())
		WRITE_PERI_REG(UART_INT_CLR(UART0), UART_RXFIFO_FULL_INT_CLR);

	if(uart_rxfifo_available())
		WRITE_PERI_REG(UART_INT_CLR(UART0), UART_RXFIFO_TOUT_INT_CLR);

	ETS_UART_INTR_ENABLE();
}


ICACHE_FLASH_ATTR static void server_receive_callback(void *arg, char *data, uint16_t length)
{
	if(esp_connection)
		uart_transmit(length, data);
}

ICACHE_FLASH_ATTR static void server_disconnect_callback(void *arg)
{
	esp_connection = 0;
}

ICACHE_FLASH_ATTR static void server_connnect_callback(void *arg)
{
	struct espconn *new_connection = (struct espconn *)arg;

	if(esp_connection)
		espconn_disconnect(new_connection);
	else
	{
		esp_connection	= new_connection;

		espconn_regist_recvcb(new_connection, server_receive_callback);
		espconn_regist_disconcb(new_connection, server_disconnect_callback);
	}
}

ICACHE_FLASH_ATTR void user_init(void)
{
	static struct espconn esp_server_config;
	static esp_tcp esp_tcp_config;

	struct station_config station_config = { "xxx", "xxx", 0, { 0, 0, 0, 0, 0, 0 } };

	wifi_set_sleep_type(NONE_SLEEP_T);
	wifi_station_set_auto_connect(1);
	wifi_set_opmode_current(STATION_MODE);
	wifi_station_disconnect();
	wifi_station_set_config_current(&station_config);
	wifi_station_connect();

	espconn_tcp_set_max_con(1);

	esp_connection = 0;

	memset(&esp_tcp_config, 0, sizeof(esp_tcp_config));
	esp_tcp_config.local_port = 23;

	memset(&esp_server_config, 0, sizeof(esp_server_config));
	esp_server_config.type		= ESPCONN_TCP;
	esp_server_config.state		= ESPCONN_NONE;
	esp_server_config.proto.tcp	= &esp_tcp_config;

	espconn_regist_connectcb(&esp_server_config, server_connnect_callback);
	espconn_accept(&esp_server_config);
	espconn_regist_time(&esp_server_config, 30, 0);

	uart_init(460800, EIGHT_BITS, PARITY_DISABLE, ONE_STOP_BIT);

	system_os_task(uart_periodic, periodic_task_id, receive_task_queue, periodic_task_queue_length);
}
