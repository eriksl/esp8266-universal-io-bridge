#include "user_main.h"

#include "uart.h"
#include "util.h"
#include "application.h"
#include "config.h"
#include "gpios.h"
#include "stats.h"
#include "i2c.h"
#include "i2c_sensor.h"
#include "display.h"
#include "http.h"

#include <stdlib.h>
#include <ip_addr.h>
#include <sntp.h>
#include <espconn.h>
#include <user_interface.h>
#include <os_type.h>
#include <ets_sys.h>

typedef enum
{
	wlan_bootstrap_state_skip,
	wlan_bootstrap_state_start,
	wlan_bootstrap_state_done
} wlan_bootstrap_state_t;

_Static_assert(sizeof(wlan_bootstrap_state_t) == 4, "sizeof(telnet_strip_state) != 4");

typedef enum
{
    ts_raw,
    ts_dodont,
    ts_data,
} telnet_strip_state_t;

_Static_assert(sizeof(telnet_strip_state_t) == 4, "sizeof(telnet_strip_state) != 4");

queue_t *uart_send_queue;
queue_t *uart_receive_queue;
queue_t *tcp_cmd_receive_queue;

os_event_t background_task_queue[background_task_queue_length];

static ETSTimer periodic_timer;

static wlan_bootstrap_state_t wlan_bootstrap_state;

static struct
{
	unsigned int disconnect:1;
	unsigned int reset:1;
	unsigned int init_i2c_sensors:1;
	unsigned int init_displays:1;
	unsigned int init_ntp_bogus:1;
	unsigned int http_disconnect:1;
} action;

static char *tcp_cmd_receive_buffer;
static char *tcp_cmd_send_buffer;
static bool tcp_cmd_send_buffer_busy;
static struct espconn *esp_cmd_tcp_connection;

static char *tcp_data_send_buffer;
static bool tcp_data_send_buffer_busy;
static struct espconn *esp_data_tcp_connection;

static char *tcp_http_send_buffer;
static bool tcp_http_send_buffer_busy;
static struct espconn *esp_http_tcp_connection;

irom static void user_init2(void);

irom static void ntp_init(void)
{
	if(ip_addr_valid(config->ntp_server))
	{
		sntp_setserver(0, &config->ntp_server);
		sntp_set_timezone(config->ntp_timezone);
		sntp_init();
		action.init_ntp_bogus = 0;
	}
	else
		action.init_ntp_bogus = 1;
}

irom static void ntp_periodic(void)
{
	const char *timestring;

	if(action.init_ntp_bogus) // server not configured
		return;

	timestring = sntp_get_real_time(sntp_get_current_timestamp());

	rt_hours = string_to_int(&timestring[11]);
	rt_mins = string_to_int(&timestring[14]);
}

irom static void tcp_accept(struct espconn *esp_config, esp_tcp *esp_tcp_config,
		unsigned int port, unsigned int timeout, void (*connect_callback)(struct espconn *))
{
	memset(esp_tcp_config, 0, sizeof(*esp_tcp_config));
	memset(esp_config, 0, sizeof(*esp_config));

	esp_tcp_config->local_port = port;
	esp_config->type = ESPCONN_TCP;
	esp_config->state = ESPCONN_NONE;
	esp_config->proto.tcp = esp_tcp_config;
	espconn_regist_connectcb(esp_config, (espconn_connect_callback)connect_callback);
	espconn_accept(esp_config);
	espconn_regist_time(esp_config, timeout, 0);
	espconn_tcp_set_max_con_allow(esp_config, 1);
}

irom noinline static void config_wlan(const char *ssid, const char *passwd)
{
	struct station_config station_config;

	if(config_get_flag(config_flag_print_debug))
		dprintf("Configure wlan, set ssid=\"%s\", passwd=\"%s\"\r\n", ssid, passwd);

	if(config_get_flag(config_flag_wlan_sdk_connect))
		wifi_station_set_auto_connect(1);
	else
	{
		wifi_station_set_auto_connect(0);
		wifi_station_disconnect();
	}

	wifi_set_opmode(STATION_MODE);

	memset(&station_config, 0, sizeof(station_config));
	strlcpy(station_config.ssid, ssid, sizeof(station_config.ssid));
	strlcpy(station_config.password, passwd, sizeof(station_config.password));
	station_config.bssid_set = 0;

	wifi_station_set_config(&station_config);
	wifi_station_connect();
}

irom noinline static void wlan_bootstrap(void)
{
	char ssid[32];
	char passwd[32];
	unsigned int current;
	unsigned int data;

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

	strlcpy(config->ssid, ssid, sizeof(config->ssid));
	strlcpy(config->passwd, passwd, sizeof(config->passwd));
	config_write();

	wlan_bootstrap_state = wlan_bootstrap_state_done;
}

irom noinline static void process_uart_fifo(void)
{
	unsigned int tcp_data_send_buffer_length;

	// send data in the uart receive fifo to tcp

	if(!queue_empty(uart_receive_queue) && !tcp_data_send_buffer_busy)
	{
		// data available and can be sent now

		tcp_data_send_buffer_length = 0;

		while((tcp_data_send_buffer_length < buffer_size) && !queue_empty(uart_receive_queue))
			tcp_data_send_buffer[tcp_data_send_buffer_length++] = queue_pop(uart_receive_queue);

		if(tcp_data_send_buffer_length > 0)
		{
			tcp_data_send_buffer_busy = true;
			espconn_send(esp_data_tcp_connection, tcp_data_send_buffer, tcp_data_send_buffer_length);
		}
	}

	// if there is still data in uart receive fifo that can't be
	// sent to tcp yet, tcp_sent_callback will call us when it can
}

irom noinline static void process_command(void)
{
	// process data in the command receive queue, but only if a complete
	// line is present (queue_lf > 0) and the output of the previous command
	// is already flushed out

	unsigned int tcp_cmd_receive_buffer_length;
	unsigned int byte;
	telnet_strip_state_t telnet_strip_state;
	bool_t eol;

	if(queue_empty(tcp_cmd_receive_queue))
		return;

	if(queue_lf(tcp_cmd_receive_queue) == 0)
		return;

	if(tcp_cmd_send_buffer_busy)
		return;

	eol = 0;
	telnet_strip_state = ts_raw;
	tcp_cmd_receive_buffer_length = 0;

	while(!eol &&
			!queue_empty(tcp_cmd_receive_queue) &&
			(queue_lf(tcp_cmd_receive_queue) > 0) &&
			((tcp_cmd_receive_buffer_length + 1) < buffer_size))
	{
		byte = queue_pop(tcp_cmd_receive_queue);

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

	if(!eol)
		return;

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
			static roflash const char str[] = "> empty command\n";
			strlcpy_roflash(tcp_cmd_send_buffer, str, buffer_size);

			break;
		}
		case(app_action_disconnect):
		{
			static roflash const char str[] = "> disconnect\n";
			strlcpy_roflash(tcp_cmd_send_buffer, str, buffer_size);
			action.disconnect = 1;

			break;
		}
		case(app_action_reset):
		{
			static roflash const char str[] = "> reset\n";
			strlcpy_roflash(tcp_cmd_send_buffer, str, buffer_size);
			action.disconnect = 1;
			action.http_disconnect = 1;
			action.reset = 1;

			break;
		}
	}

	tcp_cmd_send_buffer_busy = true;

	if(espconn_send(esp_cmd_tcp_connection, tcp_cmd_send_buffer, strlen(tcp_cmd_send_buffer)))
		tcp_cmd_send_buffer_busy = false;

	return;
}

irom static void background_task(os_event_t *events)
{
	stat_background_task++;

	if(wlan_bootstrap_state == wlan_bootstrap_state_start)
	{
		if(queue_lf(uart_receive_queue))
		{
			wlan_bootstrap();
			wlan_bootstrap_state = wlan_bootstrap_state_done;
		}

		if(stat_timer_slow > 100) // ~10 secs
		{
			if(config_get_flag(config_flag_print_debug))
				dprintf("%s\r\n", "Returning to normal uart bridge mode\r\n");

			wlan_bootstrap_state = wlan_bootstrap_state_done;
		}
	}

	if(action.disconnect)
	{
		espconn_disconnect(esp_cmd_tcp_connection);
		action.disconnect = 0;
	}

	if(action.http_disconnect)
	{
		espconn_disconnect(esp_http_tcp_connection);
		action.http_disconnect = 0;
	}

	if(wlan_bootstrap_state != wlan_bootstrap_state_start)
		process_uart_fifo();

	if(action.init_i2c_sensors)
	{
		uint32_t now = system_get_time();
		i2c_sensor_init();
		action.init_i2c_sensors = 0;
		stat_i2c_init_time_us = system_get_time() - now;
	}

	if(action.init_displays)
	{
		uint32_t now = system_get_time();
		display_init(config->display_default_msg);
		action.init_displays = 0;
		stat_display_init_time_us = system_get_time() - now;
	}

	process_command();
}

irom static void tcp_data_sent_callback(void *arg)
{
    tcp_data_send_buffer_busy = false;

	// retry to send data still in the fifo

	system_os_post(background_task_id, 0, 0);
}

irom static void tcp_data_receive_callback(void *arg, char *data, unsigned short length)
{
	unsigned int current;
	unsigned int byte;
	bool_t strip_telnet;
	telnet_strip_state_t telnet_strip_state;

	strip_telnet = config_get_flag(config_flag_strip_telnet);
	telnet_strip_state = ts_raw;

	for(current = 0; (current < length) && !queue_full(uart_send_queue); current++)
	{
		byte = data[current];

		switch(telnet_strip_state)
		{
			case(ts_raw):
			{
				if(strip_telnet && (byte == 0xff))
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

irom static void tcp_data_disconnect_callback(void *arg)
{
	esp_data_tcp_connection = 0;
}

irom static void tcp_data_connect_callback(struct espconn *new_connection)
{
	if(esp_data_tcp_connection)
		espconn_disconnect(new_connection); // not allowed but won't occur anyway
	else
	{
		esp_data_tcp_connection	= new_connection;
		tcp_data_send_buffer_busy = false;

		espconn_regist_recvcb(esp_data_tcp_connection, tcp_data_receive_callback);
		espconn_regist_sentcb(esp_data_tcp_connection, tcp_data_sent_callback);
		espconn_regist_disconcb(esp_data_tcp_connection, tcp_data_disconnect_callback);

		espconn_set_opt(esp_data_tcp_connection, ESPCONN_REUSEADDR);

		queue_flush(uart_send_queue);
		queue_flush(uart_receive_queue);
	}
}

irom static void tcp_cmd_sent_callback(void *arg)
{
    tcp_cmd_send_buffer_busy = false;
}

irom static void tcp_cmd_receive_callback(void *arg, char *data, unsigned short length)
{
	unsigned int current;

	for(current = 0; !queue_full(tcp_cmd_receive_queue) && (current < length); current++)
		queue_push(tcp_cmd_receive_queue, data[current]);

	if(current > 0)
		system_os_post(background_task_id, 0, 0);
}

irom static void tcp_cmd_reconnect_callback(void *arg, int8_t err)
{
    tcp_cmd_send_buffer_busy = false;
}

irom static void tcp_cmd_disconnect_callback(void *arg)
{
    tcp_cmd_send_buffer_busy = false;

	if(action.reset)
	{
		msleep(10);
		reset();
	}

	esp_cmd_tcp_connection = 0;
}

irom static void tcp_cmd_connect_callback(struct espconn *new_connection)
{
	if(esp_cmd_tcp_connection)
		espconn_disconnect(new_connection); // not allowed but won't occur anyway
	else
	{
		esp_cmd_tcp_connection = new_connection;

		espconn_regist_recvcb(esp_cmd_tcp_connection, tcp_cmd_receive_callback);
		espconn_regist_sentcb(esp_cmd_tcp_connection, tcp_cmd_sent_callback);
		espconn_regist_reconcb(esp_cmd_tcp_connection, tcp_cmd_reconnect_callback);
		espconn_regist_disconcb(esp_cmd_tcp_connection, tcp_cmd_disconnect_callback);

		espconn_set_opt(esp_cmd_tcp_connection, ESPCONN_REUSEADDR);

		queue_flush(tcp_cmd_receive_queue);

		snprintf(tcp_cmd_send_buffer, buffer_size, "OK\n");

		tcp_cmd_send_buffer_busy = true;

		if(espconn_send(esp_cmd_tcp_connection, tcp_cmd_send_buffer, strlen(tcp_cmd_send_buffer)))
			tcp_cmd_send_buffer_busy = false;
	}
}

irom static void tcp_http_sent_callback(void *arg)
{
    tcp_http_send_buffer_busy = false;
}

irom static void tcp_http_receive_callback(void *arg, char *data, unsigned short length)
{
	http_action_t http_action;
	http_buffer_t request, reply;

	if(!tcp_http_send_buffer_busy)
	{
		data[length] = '\0';

		request.string = data;
		request.size = 0;
		request.length = length;

		reply.string = tcp_http_send_buffer;
		reply.size = buffer_size;
		reply.length = 0;

		http_action = http_process_request(request, &reply);

		(void)http_action; //FIXME

		tcp_http_send_buffer_busy = true;
		espconn_send(esp_http_tcp_connection, tcp_http_send_buffer, reply.length);
	}

	action.http_disconnect = 1;
}

irom static void tcp_http_disconnect_callback(void *arg)
{
	esp_http_tcp_connection = 0;
}

irom static void tcp_http_connect_callback(struct espconn *new_connection)
{
	if(esp_http_tcp_connection)
		espconn_disconnect(new_connection); // not allowed but won't occur anyway
	else
	{
		esp_http_tcp_connection = new_connection;

		espconn_regist_recvcb(esp_http_tcp_connection, tcp_http_receive_callback);
		espconn_regist_sentcb(esp_http_tcp_connection, tcp_http_sent_callback);
		espconn_regist_disconcb(esp_http_tcp_connection, tcp_http_disconnect_callback);

		espconn_set_opt(esp_http_tcp_connection, ESPCONN_REUSEADDR);

		tcp_http_send_buffer_busy = false;
	}
}

irom noinline static void periodic_timer_slowpath(void)
{
	stat_timer_slow++;

	if(++ut_tens > 9)
	{
		ut_tens = 0;

		if(++ut_secs > 59)
		{
			ut_secs = 0;

			if(++ut_mins > 59)
			{
				ut_mins = 0;

				if(++ut_hours > 23)
				{
					ut_hours = 0;
					ut_days++;
				}
			}
		}
	}

	if(++rt_tens > 9)
	{
		rt_tens = 0;

		if(++rt_secs > 59)
		{
			rt_secs = 0;

			if(++rt_mins > 59)
			{
				rt_mins = 0;

				if(++rt_hours > 23)
				{
					rt_hours = 0;
					rt_days++;
				}
			}
		}
	}

	system_os_post(background_task_id, 0, 0);
}

iram static void periodic_timer_callback(void *arg)
{
	static unsigned int timer_slow_skipped = 0;
	static unsigned int timer_second_skipped = 0;
	static unsigned int timer_minute_skipped = 0;

	(void)arg;

	stat_timer_fast++;
	timer_slow_skipped++;
	timer_second_skipped++;
	timer_minute_skipped++;

	// timer runs on 100 Hz == 10 ms

	gpios_periodic();

	// run background task every 10 Hz = 100 ms

	if(timer_slow_skipped > 9)
	{
		timer_slow_skipped = 0;
		periodic_timer_slowpath();
	}

	// run display background task every second  = 1000 ms

	if(timer_second_skipped > 99)
	{
		stat_timer_second++;
		timer_second_skipped = 0;
		display_periodic();
	}

	// check ntp every minute = 60000 ms

	if(timer_minute_skipped > 5999)
	{
		stat_timer_minute++;
		timer_minute_skipped = 0;
		ntp_periodic();
	}
}

irom void user_init(void);

irom void user_init(void)
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

	if(!(tcp_http_send_buffer = malloc(buffer_size)))
		reset();

	if(!(tcp_data_send_buffer = malloc(buffer_size)))
		reset();

	if(!config_init())
		reset();

	action.reset = 0;
	action.disconnect = 0;

	config_read();
	uart_init(&config->uart);
	system_set_os_print(config_get_flag(config_flag_print_debug));

	if(config->wlan_trigger_gpio >= 0)
		gpios_set_wlan_trigger(config->wlan_trigger_gpio);

	if(config_get_flag(config_flag_phy_force_g))
		wifi_set_phy_mode(PHY_MODE_11G);
	else
		wifi_set_phy_mode(PHY_MODE_11N);

	if(config_get_flag(config_flag_wlan_power_save))
		wifi_set_sleep_type(MODEM_SLEEP_T);
	else
		wifi_set_sleep_type(NONE_SLEEP_T);

	system_init_done_cb(user_init2);
}

irom static void user_init2(void)
{
	static struct espconn esp_cmd_config, esp_data_config, esp_http_config;
	static esp_tcp esp_cmd_tcp_config, esp_data_tcp_config, esp_http_tcp_config;

	ntp_init();
	gpios_init();
	action.init_i2c_sensors = 1;
	action.init_displays = 1;
	action.init_ntp_bogus = 0;
	action.http_disconnect = 0;

	config_wlan(config->ssid, config->passwd);

	esp_data_tcp_connection = 0;
	esp_cmd_tcp_connection = 0;
	esp_http_tcp_connection = 0;

	tcp_accept(&esp_data_config, &esp_data_tcp_config, config->bridge_tcp_port, 0, tcp_data_connect_callback);
	tcp_accept(&esp_cmd_config, &esp_cmd_tcp_config, 24, 30, tcp_cmd_connect_callback);
	tcp_accept(&esp_http_config, &esp_http_tcp_config, 80, 30, tcp_http_connect_callback);

	system_os_task(background_task, background_task_id, background_task_queue, background_task_queue_length);

	if(config_get_flag(config_flag_disable_wlan_bootstrap))
		wlan_bootstrap_state = wlan_bootstrap_state_skip;
	else
	{
		if(config_get_flag(config_flag_print_debug))
		{
			dprintf("\r\n%s\r\n", "You now can enter wlan ssid and passwd within 10 seconds.");
			dprintf("%s\r\n", "Use exactly one space between them and a linefeed at the end.");
		}
		wlan_bootstrap_state = wlan_bootstrap_state_start;
	}

	if(config_get_flag(config_flag_cpu_high_speed))
		system_update_cpu_freq(160);
	else
		system_update_cpu_freq(80);

	os_timer_setfn(&periodic_timer, periodic_timer_callback, (void *)0);
	os_timer_arm(&periodic_timer, 10, 1); // fast system timer = 100 Hz = 10 ms
}
