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

#define TCP_CORK // line_buffer tcp output

enum
{
	background_task_id				= 0,
	background_task_queue_length	= 64,
	uart_send_buffer_size			= 1024,
	uart_receive_buffer_size		= 1024,
	tcp_send_buffer_size			= 1024,
};

static char		*uart_send_buffer;
static int16_t	uart_send_buffer_length;

static char		*uart_receive_buffer;
static int16_t	uart_receive_buffer_length;

static char		*tcp_send_buffer;
static int16_t	tcp_send_buffer_length;
static bool		tcp_send_buffer_sending;

static struct espconn	*esp_tcp_connection;
static os_event_t		background_task_queue[background_task_queue_length];

ICACHE_FLASH_ATTR static void user_init2(void);

ICACHE_FLASH_ATTR static void crash(void)
{
	for(;;)
		(void)0;
}

static char uart_rxfifo_length(void)
{
	uint32_t fifo_length;

	fifo_length = READ_PERI_REG(UART_STATUS(0));
	fifo_length = fifo_length & (UART_RXFIFO_CNT << UART_RXFIFO_CNT_S);
	fifo_length = (fifo_length >> UART_RXFIFO_CNT_S) & UART_RXFIFO_CNT;

	return(fifo_length);
}

static char uart_txfifo_length(void)
{
	uint32_t fifo_length;

	fifo_length = READ_PERI_REG(UART_STATUS(0));
	fifo_length = fifo_length & (UART_TXFIFO_CNT << UART_TXFIFO_CNT_S);
	fifo_length = (fifo_length >> UART_TXFIFO_CNT_S) & UART_TXFIFO_CNT;

	return(fifo_length);
}

static void uart_callback(void *p)
{
	ETS_UART_INTR_DISABLE();

	if((READ_PERI_REG(UART_INT_ST(0)) & UART_RXFIFO_TOUT_INT_ST) == UART_RXFIFO_TOUT_INT_ST)
		WRITE_PERI_REG(UART_INT_CLR(0), UART_RXFIFO_TOUT_INT_CLR);

	system_os_post(background_task_id, 0, 0);
}

static void uart_init(void)
{
	ETS_UART_INTR_DISABLE();

	ETS_UART_INTR_ATTACH(uart_callback,  0);

	PIN_PULLUP_DIS(PERIPHS_IO_MUX_U0TXD_U);
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_U0TXD_U, FUNC_U0TXD);

	uart_div_modify(0, UART_CLK_FREQ / 460800);

	WRITE_PERI_REG(UART_CONF0(0), CALC_UARTMODE(EIGHT_BITS, NONE_BITS, ONE_STOP_BIT));

	SET_PERI_REG_MASK(UART_CONF0(0), UART_RXFIFO_RST | UART_TXFIFO_RST);
	CLEAR_PERI_REG_MASK(UART_CONF0(0), UART_RXFIFO_RST | UART_TXFIFO_RST);

	WRITE_PERI_REG(UART_CONF1(0),
			(0x02 & UART_RX_TOUT_THRHD) << UART_RX_TOUT_THRHD_S |
			UART_RX_TOUT_EN);

	WRITE_PERI_REG(UART_INT_CLR(0), 0xff);
	SET_PERI_REG_MASK(UART_INT_ENA(0), UART_RXFIFO_TOUT_INT_ENA);

	uart_send_buffer_length = 0;
	uart_receive_buffer_length = 0;

	ETS_UART_INTR_ENABLE();
}

static void uart_flush(void)
{
	while(uart_rxfifo_length() > 0)
		READ_PERI_REG(UART_FIFO(0));

	uart_receive_buffer_length = 0;
}

static int16_t uart_receive(int16_t size, char *buffer)
{
	int16_t current;
	char data;

	for(current = 0; (current < size) && (uart_rxfifo_length() > 0); current++)
	{
		data = READ_PERI_REG(UART_FIFO(0)) & 0xff;
		buffer[current] = data;

#ifdef TCP_CORK
		if(data == '\n')
			return(current + 1);
#endif
	}

	return(current);
}

static void uart_transmit(int16_t length, char *buffer)
{
	int16_t current;

	for(current = 0; current < length; current++)
		WRITE_PERI_REG(UART_FIFO(0), buffer[current]);
}

static int16_t uart_buffer_receive(int16_t size, char *buffer)
{
	if(uart_receive_buffer_length < size)
		size = uart_receive_buffer_length;

	memcpy(buffer, uart_receive_buffer, size);

	uart_receive_buffer_length = 0;

	return(size);
}

static void uart_buffer_transmit(int16_t length, char *buffer)
{
	if((length + uart_send_buffer_length) > uart_send_buffer_size)
		length = uart_send_buffer_size - uart_send_buffer_length;

	memcpy(uart_send_buffer + uart_send_buffer_length, buffer, length);

	uart_send_buffer_length += length;
}

static void background_task(os_event_t *events)
{
	static const int uart_tx_fifo_size = 128;

	int16_t	length, tx_fifo_left, current;
	bool	request_post;

	request_post = false;

	if(esp_tcp_connection)
	{
		length = uart_receive(uart_receive_buffer_size - uart_receive_buffer_length, uart_receive_buffer + uart_receive_buffer_length);
		uart_receive_buffer_length += length;
	}
	else
		uart_flush();

#ifdef TCP_CORK
	if((uart_receive_buffer_length > 0) && (uart_receive_buffer[uart_receive_buffer_length - 1] == '\n'))
#else
	if(uart_receive_buffer_length > 0)
#endif
	{
		if(!tcp_send_buffer_sending)
		{
			tcp_send_buffer_sending = true;
			tcp_send_buffer_length = uart_buffer_receive(tcp_send_buffer_size, tcp_send_buffer);
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

	// IMPORTANT: uart interrupts can only be (re-)enabled after ALL bytes
	// from the RX-fifo are fetched, otherwise another interrupt will be
	// sent immediately. That's why they're not re-enabled in the interrupt
	// handler.

	ETS_UART_INTR_ENABLE();
}

static void server_receive_callback(void *arg, char *data, uint16_t length)
{
	if(esp_tcp_connection)
	{
		uart_buffer_transmit(length, data);
		system_os_post(background_task_id, 0, 0);
	}
}

static void server_data_sent_callback(void *arg)
{
    tcp_send_buffer_sending = false;
}

static void server_disconnect_callback(void *arg)
{
	esp_tcp_connection = 0;
}

static void server_connnect_callback(void *arg)
{
	struct espconn *new_connection = (struct espconn *)arg;

	if(esp_tcp_connection)
		espconn_disconnect(new_connection);
	else
	{
		esp_tcp_connection	= new_connection;
		tcp_send_buffer_length = 0;
		tcp_send_buffer_sending = false;

		espconn_regist_recvcb(new_connection, server_receive_callback);
		espconn_regist_sentcb(new_connection, server_data_sent_callback);
		espconn_regist_disconcb(new_connection, server_disconnect_callback);

		espconn_set_opt(new_connection, ESPCONN_REUSEADDR);

		uart_flush();
	}
}

ICACHE_FLASH_ATTR void user_init(void)
{
	if(!(uart_send_buffer = os_malloc(uart_send_buffer_size)))
		crash();

	if(!(uart_receive_buffer = os_malloc(uart_receive_buffer_size)))
		crash();

	if(!(tcp_send_buffer = os_malloc(tcp_send_buffer_size)))
		crash();

	system_init_done_cb(user_init2);
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

	uart_init();

	system_os_task(background_task, background_task_id, background_task_queue, background_task_queue_length);
	system_os_post(background_task_id, 0, 0);
}
