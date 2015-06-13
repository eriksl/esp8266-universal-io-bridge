#include "user_main.h"

#include "uart.h"
#include "ap_auth.h"

#include <os_type.h>
#include <ets_sys.h>
#include <ip_addr.h>
#include <espconn.h>
#include <mem.h>
#include <user_interface.h>

#include "esp-missing-decls.h"

typedef enum
{
    ts_raw,
    ts_dodont,
    ts_data,
} telnet_strip_state_t;

flags_t flags = { 0 };

fifo_t *uart_send_fifo;
fifo_t *uart_receive_fifo;

os_event_t background_task_queue[background_task_queue_length];

static char *tcp_send_buffer;
static char tcp_send_buffer_busy;
static struct espconn *esp_tcp_connection;

ICACHE_FLASH_ATTR static void user_init2(void);

ICACHE_FLASH_ATTR static void watchdog_crash(void)
{
	for(;;)
		(void)0;
}

static void background_task(os_event_t *events)
{
	uint16_t tcp_send_buffer_length;

	// send data in the uart receive fifo to tcp

	if(!fifo_empty(uart_receive_fifo) && !tcp_send_buffer_busy)
	{
		// data available and can be sent now

		tcp_send_buffer_length = 0;

		while((tcp_send_buffer_length < buffer_size) && !fifo_empty(uart_receive_fifo))
			tcp_send_buffer[tcp_send_buffer_length++] = fifo_pop(uart_receive_fifo);

		if(tcp_send_buffer_length > 0)
		{
			tcp_send_buffer_busy = 1;
			espconn_sent(esp_tcp_connection, tcp_send_buffer, tcp_send_buffer_length);
		}
	}

	// if there is still data in uart receive fifo that can't be
	// sent to tcp yet, tcp_sent_callback will call us when it can
}

static void server_data_sent_callback(void *arg)
{
    tcp_send_buffer_busy = 0;

	// retry to send data still in the fifo

	system_os_post(background_task_id, 0, 0);
}

static void server_receive_callback(void *arg, char *data, uint16_t length)
{
	uint16_t current;
	uint8_t byte;
	uint8_t telnet_strip_state;

	if(!esp_tcp_connection)
		return;

	telnet_strip_state = ts_raw;

	for(current = 0; (current < length) && !fifo_full(uart_send_fifo); current++)
	{
		byte = (uint8_t)data[current];

		switch(telnet_strip_state)
		{
			case(ts_raw):
			{
				if(flags.strip_telnet && (byte == 0xff))
					telnet_strip_state = ts_dodont;
				else
					fifo_push(uart_send_fifo, (char)byte);

				break;
			}

			case(ts_dodont):
			{
				telnet_strip_state = ts_data;
				break;
			}

			case(ts_data):
			{
				telnet_strip_state = ts_raw;
				break;
			}
		}
	}

	uart_start_transmit(!fifo_empty(uart_send_fifo));
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
		tcp_send_buffer_busy = 0;

		espconn_regist_recvcb(new_connection, server_receive_callback);
		espconn_regist_sentcb(new_connection, server_data_sent_callback);
		espconn_regist_disconcb(new_connection, server_disconnect_callback);

		espconn_set_opt(new_connection, ESPCONN_REUSEADDR);

		fifo_flush(uart_send_fifo);
		fifo_flush(uart_receive_fifo);
	}
}

ICACHE_FLASH_ATTR void user_init(void)
{
	flags.strip_telnet = 1; // FIXME

	if(!(uart_send_fifo = fifo_new(buffer_size)))
		watchdog_crash();

	if(!(uart_receive_fifo = fifo_new(buffer_size)))
		watchdog_crash();

	if(!(tcp_send_buffer = os_malloc(buffer_size)))
		watchdog_crash();

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
