#include "user_main.h"

#include "util.h"
#include "application.h"
#include "io.h"
#include "stats.h"
#include "i2c.h"
#include "display.h"

#include <stdlib.h>
#include <sntp.h>
#include <espconn.h>

#include <rboot-api.h>

typedef enum
{
	ts_copy,
	ts_dodont,
	ts_data,
} telnet_strip_state_t;

typedef struct
{
	esp_tcp tcp_config;
	struct espconn parent_socket;
	struct espconn *child_socket;
	string_t receive_buffer;
	string_t *send_buffer;
	bool_t receive_ready;
	bool_t send_busy;
} espsrv_t;

_Static_assert(sizeof(telnet_strip_state_t) == 4, "sizeof(telnet_strip_state) != 4");

queue_t data_send_queue;
queue_t data_receive_queue;

os_event_t background_task_queue[background_task_queue_length];

static ETSTimer fast_timer;
static ETSTimer slow_timer;

static struct
{
	unsigned int disconnect:1;
	unsigned int reset:1;
	unsigned int init_i2c_sensors:1;
	unsigned int init_displays:1;
} bg_action =
{
	.disconnect = 0,
	.reset = 0,
	.init_i2c_sensors = 0,
	.init_displays = 0,
};

static espsrv_t cmd;
static espsrv_t data;

irom static void user_init2(void);

irom static void ntp_init(void)
{
	if(ip_addr_valid(config.ntp.server))
	{
		sntp_stop();
		sntp_setserver(0, &config.ntp.server);
		sntp_set_timezone(config.ntp.timezone);
		sntp_init();
	}
}

irom static bool ntp_periodic(void)
{
	static int delay = 0;
	static bool_t initial_burst = true;
	struct tm *tm;
	time_t ticks;

	delay++;

	if(delay < 10) // always check once a second or less
		return(false);

	if(!initial_burst && (delay < 6000)) // after initial burst only check every 10 minutes
		return(false);

	delay = 0;

	if(!ip_addr_valid(config.ntp.server))
		return(false);

	ticks = sntp_get_current_timestamp();

	if(ticks > 0)
	{
		initial_burst = false;

		tm = sntp_localtime(&ticks);

		rt_hours = tm->tm_hour;
		rt_mins  = tm->tm_min;

		ntp_init();	// FIXME SDK bug, stop and start ntp to get continuous updating
	}

	return(true);
}

irom static void tcp_accept(espsrv_t *espsrv, string_t *send_buffer,
		int port, int timeout, void (*connect_callback)(struct espconn *))
{
	espsrv->send_buffer = send_buffer;

	ets_memset(&espsrv->tcp_config, 0, sizeof(espsrv->tcp_config));
	ets_memset(&espsrv->parent_socket, 0, sizeof(espsrv->parent_socket));
	espsrv->child_socket = (struct espconn *)0;

	espsrv->tcp_config.local_port = port;
	espsrv->parent_socket.proto.tcp = &espsrv->tcp_config;
	espsrv->parent_socket.type = ESPCONN_TCP;
	espsrv->parent_socket.state = ESPCONN_NONE;

	espconn_regist_connectcb(&espsrv->parent_socket, (espconn_connect_callback)connect_callback);
	espconn_accept(&espsrv->parent_socket);
	espconn_regist_time(&espsrv->parent_socket, timeout, 0);
	espconn_tcp_set_max_con_allow(&espsrv->parent_socket, 1);
}

irom static void tcp_data_sent_callback(void *arg)
{
	string_clear(data.send_buffer);
	data.send_busy = false;

	// retry to send data still in the fifo

	system_os_post(background_task_id, 0, 0);
}

irom static void tcp_data_receive_callback(void *arg, char *buffer, unsigned short length)
{
	int current, byte;
	bool_t strip_telnet;
	telnet_strip_state_t telnet_strip_state;

	strip_telnet = config_get_flag(config_flag_strip_telnet);
	telnet_strip_state = ts_copy;

	for(current = 0; (current < length) && !queue_full(&data_send_queue); current++)
	{
		byte = buffer[current];

		switch(telnet_strip_state)
		{
			case(ts_copy):
			{
				if(strip_telnet && (byte == 0xff))
					telnet_strip_state = ts_dodont;
				else
					queue_push(&data_send_queue, (char)byte);

				break;
			}

			case(ts_dodont):
			{
				telnet_strip_state = ts_data;
				break;
			}

			case(ts_data):
			{
				telnet_strip_state = ts_copy;
				break;
			}
		}
	}

	uart_start_transmit(!queue_empty(&data_send_queue));
}

irom static void tcp_data_disconnect_callback(void *arg)
{
	data.child_socket = 0;
}

irom static void tcp_data_connect_callback(struct espconn *new_connection)
{
	if(data.child_socket)
		espconn_disconnect(new_connection); // not allowed but won't occur anyway
	else
	{
		data.child_socket = new_connection;
		data.send_busy = false;

		espconn_regist_recvcb(data.child_socket, tcp_data_receive_callback);
		espconn_regist_sentcb(data.child_socket, tcp_data_sent_callback);
		espconn_regist_disconcb(data.child_socket, tcp_data_disconnect_callback);

		espconn_set_opt(data.child_socket, ESPCONN_REUSEADDR | ESPCONN_NODELAY);

		queue_flush(&data_send_queue);
		queue_flush(&data_receive_queue);
	}
}

irom static void tcp_cmd_sent_callback(void *arg)
{
	cmd.send_busy = false;
}

irom static void tcp_cmd_receive_callback(void *arg, char *buffer, unsigned short length)
{
	if(cmd.receive_ready)
		return;

	if(length > 0)
	{
		if(buffer[length - 1] == '\n')
		{
			if((length > 1) && (buffer[length - 2] == '\r'))
				length -= 2;
			else
				length--;
		}
		else
		{
			if(buffer[length - 1] == '\r')
			{
				if((length > 1) && (buffer[length - 2] == '\n'))
					length -= 2;
				else
					length--;
			}
		}
	}

	buffer[length] = '\0';
	string_set(&cmd.receive_buffer, buffer, length, length);
	cmd.receive_ready = true;

	system_os_post(background_task_id, 0, 0);
}

irom static void tcp_cmd_reconnect_callback(void *arg, int8_t err)
{
	cmd.send_busy = false;
}

irom static void tcp_cmd_disconnect_callback(void *arg)
{
	cmd.send_busy = false;
	cmd.receive_ready = false;
	cmd.child_socket = 0;

	if(bg_action.reset)
	{
		msleep(10);
		reset();
	}
}

irom static void tcp_cmd_connect_callback(struct espconn *new_connection)
{
	if(cmd.child_socket)
		espconn_disconnect(new_connection); // not allowed but won't occur anyway
	else
	{
		cmd.child_socket = new_connection;

		espconn_regist_recvcb(cmd.child_socket, tcp_cmd_receive_callback);
		espconn_regist_sentcb(cmd.child_socket, tcp_cmd_sent_callback);
		espconn_regist_reconcb(cmd.child_socket, tcp_cmd_reconnect_callback);
		espconn_regist_disconcb(cmd.child_socket, tcp_cmd_disconnect_callback);

		espconn_set_opt(cmd.child_socket, ESPCONN_REUSEADDR | ESPCONN_NODELAY);
	}
}

irom static void background_task_update_clocks(void)
{
	// uptime clock

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

	// realtime clock

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
}

irom static bool_t background_task_update_uart(void)
{
	// send data in the uart receive fifo to tcp

	if(!queue_empty(&data_receive_queue) && !data.send_busy && string_space(data.send_buffer))
	{
		// data available and can be sent now

		while(!queue_empty(&data_receive_queue) && string_space(data.send_buffer))
			string_append(data.send_buffer, queue_pop(&data_receive_queue));

		if(string_length(data.send_buffer) > 0)
			data.send_busy = espconn_send(data.child_socket, string_to_ptr(data.send_buffer), string_length(data.send_buffer)) == 0;

		return(true);
	}

	// if there is still data in uart receive fifo that can't be
	// sent to tcp yet, tcp_sent_callback will call us when it can

	return(false);
}

irom static bool_t background_task_longop_handler(void)
{
	if(bg_action.disconnect)
	{
		espconn_disconnect(cmd.child_socket);
		bg_action.disconnect = 0;
		return(true);
	}

	if(bg_action.init_i2c_sensors)
	{
		uint32_t now = system_get_time();
		i2c_sensor_init();
		bg_action.init_i2c_sensors = 0;
		stat_i2c_init_time_us = system_get_time() - now;
		return(true);
	}

	if(bg_action.init_displays)
	{
		uint32_t now = system_get_time();
		display_init();
		bg_action.init_displays = 0;
		stat_display_init_time_us = system_get_time() - now;
		return(true);
	}

	return(false);
}

irom static bool_t background_task_command_handler(void)
{
	string_clear(cmd.send_buffer);

	if(cmd.receive_ready)
	{
		switch(application_content(&cmd.receive_buffer, cmd.send_buffer))
		{
			case(app_action_normal):
			case(app_action_error):
			case(app_action_http_ok):
			{
				/* no special action for now */
				break;
			}
			case(app_action_empty):
			{
				string_copy(cmd.send_buffer, "> empty command\n");
				break;
			}
			case(app_action_disconnect):
			{
				string_copy(cmd.send_buffer, "> disconnect\n");
				bg_action.disconnect = 1;
				break;
			}
			case(app_action_reset):
			{
				string_copy(cmd.send_buffer, "> reset\n");
				bg_action.disconnect = 1;
				bg_action.reset = 1;
				break;
			}
#if IMAGE_OTA == 1
			case(app_action_ota_commit):
			{
				rboot_config rcfg = rboot_get_config();
				string_format(cmd.send_buffer, "OTA commit slot %d\n", rcfg.current_rom);
				bg_action.disconnect = 1;
				bg_action.reset = 1;

				break;
			}
#endif
		}

		cmd.receive_ready = false;
	}

	if(string_length(cmd.send_buffer) > 0)
	{
		cmd.send_busy =
				espconn_send(cmd.child_socket, string_to_ptr(cmd.send_buffer), string_length(cmd.send_buffer)) == 0;
		return(true);
	}

	return(false);
}

irom static void background_task(os_event_t *events) // posted every ~100 ms = ~10 Hz
{
	stat_slow_timer++;

	background_task_update_clocks();

	if(background_task_update_uart())
	{
		stat_update_uart++;
		system_os_post(background_task_id, 0, 0);
		return;
	}

	if(background_task_longop_handler())
	{
		stat_update_longop++;
		system_os_post(background_task_id, 0, 0);
		return;
	}

	if(background_task_command_handler())
	{
		stat_update_command++;
		system_os_post(background_task_id, 0, 0);
		return;
	}

	if(ntp_periodic())
	{
		stat_update_ntp++;
		system_os_post(background_task_id, 0, 0);
		return;
	}

	if(display_periodic())
	{
		stat_update_display++;
		system_os_post(background_task_id, 0, 0);
		return;
	}

	// fallback to config-ap-mode when not connected or no ip within 30 seconds

	if((config.wlan_mode == config_wlan_mode_client) &&
			(wifi_station_get_connect_status() != STATION_GOT_IP) &&
			(stat_update_idle == 300))
	{
		config.wlan_mode = config_wlan_mode_ap;
		wlan_init();
	}

	stat_update_idle++;
}

iram static void fast_timer_callback(void *arg)
{
	(void)arg;

	stat_fast_timer++;

	// timer runs every 10 ms = 100 Hz

	io_periodic();
}

irom static void slow_timer_callback(void *arg)
{
	(void)arg;

	// run background task every ~100 ms = ~10 Hz

	system_os_post(background_task_id, 0, 0);
}

irom void user_init(void);

irom void user_init(void)
{
	static char data_send_queue_buffer[1024];
	static char data_receive_queue_buffer[1024];

	queue_new(&data_send_queue, sizeof(data_send_queue_buffer), data_send_queue_buffer);
	queue_new(&data_receive_queue, sizeof(data_receive_queue_buffer), data_receive_queue_buffer);

	bg_action.reset = 0;
	bg_action.disconnect = 0;
	bg_action.init_i2c_sensors = 1;
	bg_action.init_displays = 1;

	config_read(&config);
	uart_init(&config.uart);
	system_set_os_print(config_get_flag(config_flag_print_debug));

	wifi_station_set_auto_connect(0);

	if(config_get_flag(config_flag_wlan_power_save))
		wifi_set_sleep_type(MODEM_SLEEP_T);
	else
		wifi_set_sleep_type(NONE_SLEEP_T);

	system_init_done_cb(user_init2);
}

irom static void wlan_event_handler(System_Event_t *event)
{
	int io;
	int pin;

	io = config.assoc_trigger.io;
	pin = config.assoc_trigger.pin;

	switch(event->event)
	{
		case(EVENT_STAMODE_GOT_IP):
		case(EVENT_SOFTAPMODE_STACONNECTED):
		{
			if((io >= 0) && (pin >= 0))
				io_write_pin((string_t *)0, io, pin, -1);

			break;
		}

		case(EVENT_STAMODE_DISCONNECTED):
		case(EVENT_SOFTAPMODE_STADISCONNECTED):
		{
			if((io >= 0) && (pin >= 0))
				io_write_pin((string_t *)0, io, pin, 0);

			break;
		}
	}
}

irom static void user_init2(void)
{
	string_new(static, data_send_buffer, 1024);
	string_new(static, cmd_send_buffer, 4096 + 4); // need a few extra bytes to make up exactly 4096 bytes for OTA

	wifi_set_event_handler_cb(wlan_event_handler);

	wlan_init();
	ntp_init();
	io_init();

	tcp_accept(&data,	&data_send_buffer,	config.bridge.port, 	config.bridge.timeout,	tcp_data_connect_callback);
	tcp_accept(&cmd,	&cmd_send_buffer,	config.command.port,	config.command.timeout,	tcp_cmd_connect_callback);

	system_os_task(background_task, background_task_id, background_task_queue, background_task_queue_length);

	if(config_get_flag(config_flag_cpu_high_speed))
		system_update_cpu_freq(160);
	else
		system_update_cpu_freq(80);

	os_timer_setfn(&slow_timer, slow_timer_callback, (void *)0);
	os_timer_arm(&slow_timer, 100, 1); // slow system timer / 10 Hz / 100 ms

	os_timer_setfn(&fast_timer, fast_timer_callback, (void *)0);
	os_timer_arm(&fast_timer, 10, 1); // fast system timer / 100 Hz / 10 ms
}

irom bool_t wlan_init(void)
{
	switch(config.wlan_mode)
	{
		case(config_wlan_mode_client):
		{
			struct station_config cconf;

			ets_memset(&cconf, 0, sizeof(cconf));
			strlcpy(cconf.ssid, config.client_wlan.ssid, sizeof(cconf.ssid));
			strlcpy(cconf.password, config.client_wlan.passwd, sizeof(cconf.password));
			cconf.bssid_set = 0;

			if(config_get_flag(config_flag_print_debug))
				dprintf("* set wlan mode to client, ssid=\"%s\", passwd=\"%s\"\r\n", cconf.ssid, cconf.password);

			wifi_station_disconnect();
			wifi_set_opmode_current(STATION_MODE);
			wifi_station_set_config_current(&cconf);
			wifi_station_connect();

			break;
		}

		case(config_wlan_mode_ap):
		{
			struct softap_config saconf;

			ets_memset(&saconf, 0, sizeof(saconf));
			strlcpy(saconf.ssid, config.ap_wlan.ssid, sizeof(saconf.ssid));
			strlcpy(saconf.password, config.ap_wlan.passwd, sizeof(saconf.password));
			saconf.ssid_len = ets_strlen(config.ap_wlan.ssid);
			saconf.channel = config.ap_wlan.channel;
			saconf.authmode = AUTH_WPA_WPA2_PSK;
			saconf.ssid_hidden = 0;
			saconf.max_connection = 1;
			saconf.beacon_interval = 100;

			if(config_get_flag(config_flag_print_debug))
				dprintf("* set wlan mode to ap, ssid=\"%s\", passwd=\"%s\", channel=%d\r\n",
						saconf.ssid, saconf.password, saconf.channel);

			wifi_station_disconnect();
			wifi_set_opmode_current(SOFTAP_MODE);
			wifi_softap_set_config_current(&saconf);

			break;
		}

		default:
		{
			return(false);
		}
	}

	return(true);
}
