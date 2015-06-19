#include "user_main.h"

#include "uart.h"
#include "util.h"
#include "application.h"
#include "config.h"

#include <ip_addr.h>
#include <espconn.h>
#include <user_interface.h>

typedef enum
{
    ts_raw,
    ts_dodont,
    ts_data,
} telnet_strip_state_t;

queue_t *uart_send_queue;
queue_t *uart_receive_queue;
queue_t *tcp_cmd_receive_queue;

os_event_t background_task_queue[background_task_queue_length];

static char *tcp_cmd_receive_buffer;
static char *tcp_cmd_send_buffer;
static char tcp_cmd_send_buffer_busy;
static struct espconn *esp_cmd_tcp_connection;

static char *tcp_data_send_buffer;
static char tcp_data_send_buffer_busy;
static struct espconn *esp_data_tcp_connection;

ICACHE_FLASH_ATTR static void user_init2(void);

ICACHE_FLASH_ATTR static void tcp_accept(struct espconn *esp_config, esp_tcp *esp_tcp_config,
		uint16_t port, void (*connect_callback)(struct espconn *))
{
	memset(esp_tcp_config, 0, sizeof(*esp_tcp_config));
	esp_tcp_config->local_port = port;
	memset(esp_config, 0, sizeof(*esp_config));
	esp_config->type = ESPCONN_TCP;
	esp_config->state = ESPCONN_NONE;
	esp_config->proto.tcp = esp_tcp_config;
	espconn_regist_connectcb(esp_config, (espconn_connect_callback)connect_callback);
	espconn_accept(esp_config);
	esp_cmd_tcp_connection = 0;

	espconn_tcp_set_max_con_allow(esp_config, 1);
}

static void background_task(os_event_t *events)
{
	uint16_t tcp_data_send_buffer_length;
	uint16_t tcp_cmd_receive_buffer_length;

	// send data in the uart receive fifo to tcp

	if(!queue_empty(uart_receive_queue) && !tcp_data_send_buffer_busy)
	{
		// data available and can be sent now

		tcp_data_send_buffer_length = 0;

		while((tcp_data_send_buffer_length < buffer_size) && !queue_empty(uart_receive_queue))
			tcp_data_send_buffer[tcp_data_send_buffer_length++] = queue_pop(uart_receive_queue);

		if(tcp_data_send_buffer_length > 0)
		{
			tcp_data_send_buffer_busy = 1;
			espconn_sent(esp_data_tcp_connection, tcp_data_send_buffer, tcp_data_send_buffer_length);
		}
	}

	// if there is still data in uart receive fifo that can't be
	// sent to tcp yet, tcp_sent_callback will call us when it can

	// process data in the command receive queue, but only if a complete
	// line is present (queue_lf > 0) and the output of the previous command
	// is already flushed out

	uint8_t byte;
	uint8_t telnet_strip_state;
	uint8_t eol;

	if(!tcp_cmd_send_buffer_busy)
	{
		eol = 0;
		telnet_strip_state = ts_raw;
		tcp_cmd_receive_buffer_length = 0;

		while(!eol &&
				!queue_empty(tcp_cmd_receive_queue) &&
				(queue_lf(tcp_cmd_receive_queue) > 0) &&
				((tcp_cmd_receive_buffer_length + 1) < buffer_size))
		{
			byte = (uint8_t)queue_pop(tcp_cmd_receive_queue);

			switch(telnet_strip_state)
			{
				case(ts_raw):
				{
					if(byte == 0xff)
						telnet_strip_state = ts_dodont;
					else if(byte == '\n')
						eol = 1;
					else if((byte >= ' ') && (byte <= '~'))
						tcp_cmd_receive_buffer[tcp_cmd_receive_buffer_length++] = (char)byte;

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

		if(eol)
		{
			tcp_cmd_receive_buffer[tcp_cmd_receive_buffer_length] = '\0';
			if(application_content(tcp_cmd_receive_buffer, buffer_size, tcp_cmd_send_buffer))
			{
				tcp_cmd_send_buffer_busy = 1;
				espconn_sent(esp_cmd_tcp_connection, tcp_cmd_send_buffer, strlen(tcp_cmd_send_buffer));
			}
			else
				espconn_disconnect(esp_cmd_tcp_connection);
		}
	}

	application_periodic();
}

ICACHE_FLASH_ATTR static void tcp_data_sent_callback(void *arg)
{
    tcp_data_send_buffer_busy = 0;

	// retry to send data still in the fifo

	system_os_post(background_task_id, 0, 0);
}

ICACHE_FLASH_ATTR static void tcp_data_receive_callback(void *arg, char *data, uint16_t length)
{
	uint16_t current;
	uint8_t byte;
	uint8_t telnet_strip_state;

	telnet_strip_state = ts_raw;

	for(current = 0; (current < length) && !queue_full(uart_send_queue); current++)
	{
		byte = (uint8_t)data[current];

		switch(telnet_strip_state)
		{
			case(ts_raw):
			{
				if(config.strip_telnet && (byte == 0xff))
					telnet_strip_state = ts_dodont;
				else
					queue_push(uart_send_queue, (char)byte);

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

	uart_start_transmit(!queue_empty(uart_send_queue));
}

ICACHE_FLASH_ATTR static void tcp_data_disconnect_callback(void *arg)
{
	esp_data_tcp_connection = 0;
}

ICACHE_FLASH_ATTR static void tcp_data_connect_callback(struct espconn *new_connection)
{
	if(esp_data_tcp_connection)
		espconn_disconnect(new_connection);
	else
	{
		esp_data_tcp_connection	= new_connection;
		tcp_data_send_buffer_busy = 0;

		espconn_regist_recvcb(esp_data_tcp_connection, tcp_data_receive_callback);
		espconn_regist_sentcb(esp_data_tcp_connection, tcp_data_sent_callback);
		espconn_regist_disconcb(esp_data_tcp_connection, tcp_data_disconnect_callback);

		espconn_set_opt(esp_data_tcp_connection, ESPCONN_REUSEADDR);

		queue_flush(uart_send_queue);
		queue_flush(uart_receive_queue);
	}
}

ICACHE_FLASH_ATTR static void tcp_cmd_sent_callback(void *arg)
{
    tcp_cmd_send_buffer_busy = 0;
}

ICACHE_FLASH_ATTR static void tcp_cmd_receive_callback(void *arg, char *data, uint16_t length)
{
	uint16_t current;

	for(current = 0; !queue_full(tcp_cmd_receive_queue) && (current < length); current++)
		queue_push(tcp_cmd_receive_queue, data[current]);

	if(current > 0)
		system_os_post(background_task_id, 0, 0);
}

ICACHE_FLASH_ATTR static void tcp_cmd_disconnect_callback(void *arg)
{
	esp_cmd_tcp_connection = 0;
}

ICACHE_FLASH_ATTR static void tcp_cmd_connect_callback(struct espconn *new_connection)
{
	if(esp_cmd_tcp_connection)
		espconn_disconnect(new_connection);
	else
	{
		esp_cmd_tcp_connection = new_connection;
		tcp_cmd_send_buffer_busy = 0;

		espconn_regist_recvcb(esp_cmd_tcp_connection, tcp_cmd_receive_callback);
		espconn_regist_sentcb(esp_cmd_tcp_connection, tcp_cmd_sent_callback);
		espconn_regist_disconcb(esp_cmd_tcp_connection, tcp_cmd_disconnect_callback);

		espconn_set_opt(esp_cmd_tcp_connection, ESPCONN_REUSEADDR);

		queue_flush(tcp_cmd_receive_queue);
	}
}

ICACHE_FLASH_ATTR void user_init(void)
{
	if(!(uart_send_queue = queue_new(buffer_size)))
		reset();

	if(!(uart_receive_queue = queue_new(buffer_size)))
		reset();

	if(!(tcp_cmd_receive_queue = queue_new(buffer_size)))
		reset();

	if(!(tcp_cmd_receive_buffer = malloc(buffer_size)))
		reset();

	if(!(tcp_cmd_send_buffer = malloc(buffer_size)))
		reset();

	if(!(tcp_data_send_buffer = malloc(buffer_size)))
		reset();

	config_read();
	system_set_os_print(config.print_debug);
	uart_init(&config.uart);
	system_init_done_cb(user_init2);
}

ICACHE_FLASH_ATTR static void user_init2(void)
{
	static struct espconn esp_cmd_config, esp_data_config;
	static esp_tcp esp_cmd_tcp_config, esp_data_tcp_config;

	wifi_set_sleep_type(NONE_SLEEP_T);

	tcp_accept(&esp_data_config, &esp_data_tcp_config, 23, tcp_data_connect_callback);
	espconn_regist_time(&esp_data_config, 0, 0);
	esp_data_tcp_connection = 0;

	tcp_accept(&esp_cmd_config, &esp_cmd_tcp_config, 24, tcp_cmd_connect_callback);
	espconn_regist_time(&esp_cmd_config, 30, 0);
	esp_cmd_tcp_connection = 0;

	application_init();

	system_os_task(background_task, background_task_id, background_task_queue, background_task_queue_length);
	system_os_post(background_task_id, 0, 0);
}
