#include "user_main.h"

#include "uart.h"
#include "util.h"
#include "application.h"
#include "config.h"
#include "gpios.h"
#include "stats.h"
#include "i2c.h"
#include "i2c_sensor.h"

#include <ip_addr.h>
#include <espconn.h>
#include <user_interface.h>
#include <os_type.h>
#include <ets_sys.h>

typedef enum __attribute__ ((__packed__))
{
	init_start,
	init_done
} init_state_t;

_Static_assert(sizeof(gpio_id_t) == 1, "sizeof(telnet_strip_state) != 1");

typedef enum __attribute__ ((__packed__))
{
    ts_raw,
    ts_dodont,
    ts_data,
} telnet_strip_state_t;

_Static_assert(sizeof(gpio_id_t) == 1, "sizeof(telnet_strip_state) != 1");

queue_t *uart_send_queue;
queue_t *uart_receive_queue;
queue_t *tcp_cmd_receive_queue;

os_event_t background_task_queue[background_task_queue_length];

static ETSTimer periodic_timer;

static init_state_t init_state = init_start;

static struct
{
	unsigned int disconnect:1;
	unsigned int reset:1;
} action;

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

ICACHE_FLASH_ATTR noinline static void config_wlan(const char *ssid, const char *passwd)
{
	struct station_config station_config;

	if(config.print_debug)
		dprintf("Configure wlan, set ssid=\"%s\", passwd=\"%s\"\r\n", ssid, passwd);

	wifi_station_set_auto_connect(0);
	wifi_station_disconnect();
	wifi_set_opmode(STATION_MODE);

	memset(&station_config, 0, sizeof(station_config));
	strlcpy(station_config.ssid, ssid, sizeof(station_config.ssid));
	strlcpy(station_config.password, passwd, sizeof(station_config.password));
	station_config.bssid_set = 0;

	wifi_station_set_config(&station_config);
	wifi_station_connect();
}

static void background_task(os_event_t *events)
{
	static uint32_t prev_system_time_ms = 0;
	uint16_t tcp_data_send_buffer_length;
	uint16_t tcp_cmd_receive_buffer_length;
	uint32_t current_system_time_ms;
	uint16_t missed_ticks;
	char ssid[32];
	char passwd[32];
	uint16_t current;
	char data;

	stat_background_task++;

	if((init_state != init_done) && (stat_timer > 300)) // ~30 seconds after start
	{
		if(config.print_debug)
			dprintf("%s\r\n", "Returning to normal uart bridge mode\r\n");
		init_state = init_done;
	}

	if(init_state == init_start) // wait for WLAN config over UART
	{
		if(queue_lf(uart_receive_queue))
		{
			for(current = 0; current < (sizeof(ssid) - 1); current++)
			{
				if(queue_empty(uart_receive_queue))
					break;

				data = queue_pop(uart_receive_queue);

				if(data == ' ')
					break;

				ssid[current] = data;
			}

			ssid[current] = '\0';

			for(current = 0; current < (sizeof(passwd) - 1); current++)
			{
				if(queue_empty(uart_receive_queue))
					break;

				data = queue_pop(uart_receive_queue);

				if(data == '\n')
					break;

				passwd[current] = data;
			}

			passwd[current] = '\0';

			config_wlan(ssid, passwd);

			strlcpy(config.ssid, ssid, sizeof(config.ssid));
			strlcpy(config.passwd, passwd, sizeof(config.passwd));
			config_write();

			init_state = init_done;
		}
	}
	else
	{
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
	}

	// if there is still data in uart receive fifo that can't be
	// sent to tcp yet, tcp_sent_callback will call us when it can

	if(action.disconnect)
	{
		espconn_disconnect(esp_cmd_tcp_connection);
		action.disconnect = 0;
	}

	// process data in the command receive queue, but only if a complete
	// line is present (queue_lf > 0) and the output of the previous command
	// is already flushed out

	uint8_t byte;
	telnet_strip_state_t telnet_strip_state;
	bool_t eol;

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

			switch(application_content(tcp_cmd_receive_buffer, buffer_size, tcp_cmd_send_buffer))
			{
				case(app_action_normal):
				case(app_action_error):
				{
					/* no special action for now */

					break;
				}
				case(app_action_empty):
				{
					strlcpy(tcp_cmd_send_buffer, "> empty command\n", buffer_size);

					break;
				}
				case(app_action_disconnect):
				{
					strlcpy(tcp_cmd_send_buffer, "> disconnect\n", buffer_size);
					action.disconnect = 1;

					break;
				}
				case(app_action_reset):
				{
					strlcpy(tcp_cmd_send_buffer, "> reset\n", buffer_size);
					action.disconnect = 1;
					action.reset = 1;

					break;
				}
			}

			tcp_cmd_send_buffer_busy = 1;
			espconn_sent(esp_cmd_tcp_connection, tcp_cmd_send_buffer, strlen(tcp_cmd_send_buffer));
		}
	}

	current_system_time_ms = system_get_time() / 1000;

	if(prev_system_time_ms == 0)
		prev_system_time_ms = current_system_time_ms;

	missed_ticks = (current_system_time_ms - prev_system_time_ms) / 100; // 10 ms per tick

	if(missed_ticks > 8) // wraparound occurred
	{
		stat_application_periodic_wrapped++;
		missed_ticks = 1;
	}

	if(missed_ticks > 0)
	{
		while(missed_ticks-- > 0)
			application_periodic();

		prev_system_time_ms = current_system_time_ms;
	}
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
	telnet_strip_state_t telnet_strip_state;

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
		espconn_disconnect(new_connection); // not allowed but won't occur anyway
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
	if(action.reset)
	{
		msleep(10);
		reset();
	}

	esp_cmd_tcp_connection = 0;
}

ICACHE_FLASH_ATTR static void tcp_cmd_connect_callback(struct espconn *new_connection)
{
	if(esp_cmd_tcp_connection)
		espconn_disconnect(new_connection); // not allowed but won't occur anyway
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

static void periodic_timer_callback(void *arg)
{
	(void)arg;

	stat_timer++;

	system_os_post(background_task_id, 0, 0);
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

	action.reset = 0;
	action.disconnect = 0;

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

	gpios_init();
	i2c_sensor_init();

	config_wlan(config.ssid, config.passwd);

	tcp_accept(&esp_data_config, &esp_data_tcp_config, 23, tcp_data_connect_callback);
	espconn_regist_time(&esp_data_config, 0, 0);
	esp_data_tcp_connection = 0;

	tcp_accept(&esp_cmd_config, &esp_cmd_tcp_config, 24, tcp_cmd_connect_callback);
	espconn_regist_time(&esp_cmd_config, 30, 0);
	esp_cmd_tcp_connection = 0;

	system_os_task(background_task, background_task_id, background_task_queue, background_task_queue_length);

	if(config.print_debug)
	{
		dprintf("\r\n%s\r\n", "You now can enter wlan ssid and passwd within 30 seconds.");
		dprintf("%s\r\n", "Use exactly one space between them and a linefeed at the end.");
	}

	os_timer_setfn(&periodic_timer, periodic_timer_callback, (void *)0);
	os_timer_arm(&periodic_timer, 100, 1);
}
