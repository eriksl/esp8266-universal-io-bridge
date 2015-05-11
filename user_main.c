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
#include "ap_auth.h"

enum
{
	background_task_id				= 0,
	background_task_queue_length	= 64,
};

static bool		user_init2_done;

static char		uart_send_buffer[1024];
static int16_t	uart_send_buffer_length;

static char		uart_receive_buffer[1024];
static int16_t	uart_receive_buffer_length;

static char		tcp_send_buffer[sizeof(uart_receive_buffer)];
static int16_t	tcp_send_buffer_length;
static bool		tcp_send_buffer_sending;

static struct espconn	*esp_tcp_connection;
static os_event_t		background_task_queue[background_task_queue_length];

ICACHE_FLASH_ATTR static void user_init2(void);

ICACHE_FLASH_ATTR static char uart_rxfifo_length(void)
{
	uint32_t fifo_length;

	fifo_length = READ_PERI_REG(UART_STATUS(0));
	fifo_length = fifo_length & (UART_RXFIFO_CNT << UART_RXFIFO_CNT_S);
	fifo_length = (fifo_length >> UART_RXFIFO_CNT_S) & UART_RXFIFO_CNT;

	return(fifo_length);
}

ICACHE_FLASH_ATTR static char uart_txfifo_length(void)
{
	uint32_t fifo_length;

	fifo_length = READ_PERI_REG(UART_STATUS(0));
	fifo_length = fifo_length & (UART_TXFIFO_CNT << UART_TXFIFO_CNT_S);
	fifo_length = (fifo_length >> UART_TXFIFO_CNT_S) & UART_TXFIFO_CNT;

	return(fifo_length);
}

ICACHE_FLASH_ATTR static void uart_rx_callback(void *p)
{
	ETS_UART_INTR_DISABLE();

	if((READ_PERI_REG(UART_INT_ST(0)) & UART_FRM_ERR_INT_ST) == UART_FRM_ERR_INT_ST)
		WRITE_PERI_REG(UART_INT_CLR(0), UART_FRM_ERR_INT_CLR);

	if((READ_PERI_REG(UART_INT_ST(0)) & UART_RXFIFO_FULL_INT_ST) == UART_RXFIFO_FULL_INT_ST)
		WRITE_PERI_REG(UART_INT_CLR(0), UART_RXFIFO_FULL_INT_CLR);

	if((READ_PERI_REG(UART_INT_ST(0)) & UART_RXFIFO_TOUT_INT_ST) == UART_RXFIFO_TOUT_INT_ST)
		WRITE_PERI_REG(UART_INT_CLR(0), UART_RXFIFO_TOUT_INT_CLR);

	system_os_post(background_task_id, 0, 0);
}

ICACHE_FLASH_ATTR static void uart_init(void)
{
	ETS_UART_INTR_DISABLE();

	ETS_UART_INTR_ATTACH(uart_rx_callback,  0);

	PIN_PULLUP_DIS(PERIPHS_IO_MUX_U0TXD_U);
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_U0TXD_U, FUNC_U0TXD);
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDO_U, FUNC_U0RTS);

	uart_div_modify(0, UART_CLK_FREQ / 460800);

	WRITE_PERI_REG(UART_CONF0(0), CALC_UARTMODE(EIGHT_BITS, NONE_BITS, ONE_STOP_BIT));

	SET_PERI_REG_MASK(UART_CONF0(0), UART_RXFIFO_RST | UART_TXFIFO_RST);
	CLEAR_PERI_REG_MASK(UART_CONF0(0), UART_RXFIFO_RST | UART_TXFIFO_RST);

	WRITE_PERI_REG(UART_CONF1(0),
			((0x10 & UART_RXFIFO_FULL_THRHD) << UART_RXFIFO_FULL_THRHD_S) |
			((0x10 & UART_RX_FLOW_THRHD) << UART_RX_FLOW_THRHD_S) |
			UART_RX_FLOW_EN |
			(0x02 & UART_RX_TOUT_THRHD) << UART_RX_TOUT_THRHD_S |
			UART_RX_TOUT_EN);

	SET_PERI_REG_MASK(UART_INT_ENA(0), UART_RXFIFO_TOUT_INT_ENA | UART_FRM_ERR_INT_ENA);
	WRITE_PERI_REG(UART_INT_CLR(0), 0xffff);
	SET_PERI_REG_MASK(UART_INT_ENA(0), UART_RXFIFO_FULL_INT_ENA);

	ETS_UART_INTR_ENABLE();
}

ICACHE_FLASH_ATTR static int16_t uart_receive(int16_t size, char *buffer)
{
	int16_t current;

	for(current = 0; (current < size) && (uart_rxfifo_length() > 0); current++)
		buffer[current] = READ_PERI_REG(UART_FIFO(0)) & 0xff;

	return(current);
}

ICACHE_FLASH_ATTR static void uart_transmit(int16_t length, char *buffer)
{
	int16_t current;

	for(current = 0; current < length; current++)
		WRITE_PERI_REG(UART_FIFO(0), buffer[current]);
}

ICACHE_FLASH_ATTR static int16_t uart_buffer_receive(int16_t size, char *buffer)
{
	if(uart_receive_buffer_length < size)
		size = uart_receive_buffer_length;

	memcpy(buffer, uart_receive_buffer, size);

	uart_receive_buffer_length = 0;

	return(size);
}

ICACHE_FLASH_ATTR static void uart_buffer_transmit(int16_t length, char *buffer)
{
	if((length + uart_send_buffer_length) > sizeof(uart_send_buffer))
		length = sizeof(uart_send_buffer) - uart_send_buffer_length;

	memcpy(uart_send_buffer + uart_send_buffer_length, buffer, length);

	uart_send_buffer_length += length;
}

ICACHE_FLASH_ATTR static void background_task(os_event_t *events)
{
	// currently 100 according to uart.h, let's hope it's correct, divide by 2 just to be sure...
	static const int uart_tx_fifo_size = TX_BUFF_SIZE / 2;

	int16_t	length, tx_fifo_left, current;
	bool	request_post;

	request_post = false;

	if(!user_init2_done)
		return(user_init2());

	length = uart_receive(sizeof(uart_receive_buffer) - uart_receive_buffer_length, uart_receive_buffer + uart_receive_buffer_length);
	uart_receive_buffer_length += length;

	if(esp_tcp_connection && (uart_receive_buffer_length > 0))
	{
		if(!tcp_send_buffer_sending)
		{
			tcp_send_buffer_sending = true;
			tcp_send_buffer_length = uart_buffer_receive(sizeof(tcp_send_buffer), tcp_send_buffer);
			espconn_sent(esp_tcp_connection, tcp_send_buffer, tcp_send_buffer_length);
		}
		else
			request_post = true;
	}

	if(uart_rxfifo_length() > 0)
		request_post = true;

	if(uart_send_buffer_length > 0)
	{
		tx_fifo_left = uart_tx_fifo_size - uart_txfifo_length();

		if(tx_fifo_left < 0) // actually shouldn't happen
			tx_fifo_left = 0;

		if(uart_send_buffer_length < tx_fifo_left)
			length = uart_send_buffer_length;
		else
			length = tx_fifo_left;

		uart_transmit(length, uart_send_buffer);

		// move all bytes left unsent to the front of the buffer
		for(current = length; current < uart_send_buffer_length; current++)
			uart_send_buffer[current - length] = uart_send_buffer[current];

		uart_send_buffer_length = uart_send_buffer_length - length;

		if(uart_send_buffer_length > 0)
			request_post = true;
	}

	if(request_post)
		system_os_post(background_task_id, 0, 0);

	ETS_UART_INTR_ENABLE();
}

ICACHE_FLASH_ATTR static void server_receive_callback(void *arg, char *data, uint16_t length)
{
	if(esp_tcp_connection)
	{
		uart_buffer_transmit(length, data);
		system_os_post(background_task_id, 0, 0);
	}
}

ICACHE_FLASH_ATTR static void server_data_sent_callback(void *arg)
{
    tcp_send_buffer_sending = false;
}

ICACHE_FLASH_ATTR static void server_disconnect_callback(void *arg)
{
	esp_tcp_connection = 0;
}

ICACHE_FLASH_ATTR static void server_connnect_callback(void *arg)
{
	struct espconn *new_connection = (struct espconn *)arg;

	if(esp_tcp_connection)
		espconn_disconnect(new_connection);
	else
	{
		esp_tcp_connection	= new_connection;

		espconn_regist_recvcb(new_connection, server_receive_callback);
		espconn_regist_sentcb(new_connection, server_data_sent_callback);
		espconn_regist_disconcb(new_connection, server_disconnect_callback);
	}
}

ICACHE_FLASH_ATTR void user_init(void)
{
	user_init2_done				= false;
	uart_send_buffer_length		= 0;
	uart_receive_buffer_length	= 0;
	tcp_send_buffer_length		= 0;
	tcp_send_buffer_sending		= false;

	uart_init();

	system_os_task(background_task, background_task_id, background_task_queue, background_task_queue_length);

	system_os_post(background_task_id, 0, 0);
}

ICACHE_FLASH_ATTR static void user_init2(void)
{
	// create ap_auth.h and #define ap_ssid / ap_password accordingly
	static struct station_config station_config = { ap_ssid, ap_password, 0, { 0, 0, 0, 0, 0, 0 } };
	static struct espconn esp_server_config;
	static esp_tcp esp_tcp_config;

	wifi_set_sleep_type(NONE_SLEEP_T);
	wifi_set_opmode_current(STATION_MODE);
	wifi_station_set_auto_connect(0);
	wifi_station_disconnect();
	wifi_station_set_config_current(&station_config);
	wifi_station_connect();

	espconn_tcp_set_max_con(1);

	esp_tcp_connection = 0;

	memset(&esp_tcp_config, 0, sizeof(esp_tcp_config));
	esp_tcp_config.local_port = 23;

	memset(&esp_server_config, 0, sizeof(esp_server_config));
	esp_server_config.type		= ESPCONN_TCP;
	esp_server_config.state		= ESPCONN_NONE;
	esp_server_config.proto.tcp	= &esp_tcp_config;

	espconn_regist_connectcb(&esp_server_config, server_connnect_callback);
	espconn_accept(&esp_server_config);
	espconn_regist_time(&esp_server_config, 30, 0);

	user_init2_done = true;
}

