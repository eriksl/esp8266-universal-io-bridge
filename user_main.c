#include "user_main.h"

#include "util.h"
#include "application.h"
#include "io.h"
#include "stats.h"
#include "i2c.h"
#include "display.h"
#include "time.h"
#include "i2c_sensor.h"
#include "socket.h"

#if IMAGE_OTA == 1
#include <rboot-api.h>
#endif

typedef enum
{
	ts_copy,
	ts_dodont,
	ts_data,
} telnet_strip_state_t;

typedef enum
{
	reset_state_inactive,
	reset_state_send_reply,
	reset_state_request_tcp_disconnect,
	reset_state_wait_tcp_disconnect,
	reset_state_wait,
	reset_state_go,
	reset_state_waiting
} reset_state_t;

_Static_assert(sizeof(telnet_strip_state_t) == 4, "sizeof(telnet_strip_state) != 4");

os_event_t background_task_queue[background_task_queue_length];

static bool_t	uart_bridge_active = false;

static socket_t socket_cmd;
static socket_t socket_uart;

static ETSTimer fast_timer;
static ETSTimer slow_timer;

static reset_state_t reset_state = reset_state_inactive;
static struct
{
	unsigned int disconnect:1;
	unsigned int init_i2c_sensors:1;
	unsigned int init_displays:1;
} bg_action =
{
	.disconnect = 0,
	.init_i2c_sensors = 0,
	.init_displays = 0,
};

queue_t uart_send_queue;
queue_t uart_receive_queue;

attr_const void user_spi_flash_dio_to_qio_pre_init(void);
iram attr_const void user_spi_flash_dio_to_qio_pre_init(void)
{
}

attr_const uint32_t user_rf_cal_sector_set(void);
iram attr_const uint32_t user_rf_cal_sector_set(void)
{
	stat_called.user_rf_cal_sector_set = 1;
	return(RFCAL_ADDRESS / 0x1000);
}

void user_rf_pre_init(void);
iram void user_rf_pre_init(void)
{
	system_phy_set_powerup_option(3); // do full calibration
	system_phy_freq_trace_enable(1);

	stat_called.user_rf_pre_init = 1;
}

static void user_init2(void);

iram static bool_t background_task_bridge_uart(void)
{
	// send data in the uart receive fifo to tcp
	// if there is still data in uart receive fifo that can't be
	// sent to tcp yet, callback_sent will call us when it can

	while(!queue_empty(&uart_receive_queue) && string_space(socket_uart.send_buffer))
		string_append(socket_uart.send_buffer, queue_pop(&uart_receive_queue));

	if(string_length(socket_uart.send_buffer) > 0)
	{
		socket_send(&socket_uart, string_to_ptr(socket_uart.send_buffer), string_length(socket_uart.send_buffer));
		return(true);
	}

	return(false);
}

iram static bool_t background_task_longop_handler(void)
{
	if(bg_action.disconnect)
	{
		if(socket_cmd.tcp.child_socket != (struct espconn *)0)
			espconn_disconnect(socket_cmd.tcp.child_socket);
		bg_action.disconnect = 0;
		return(true);
	}

	if(bg_action.init_i2c_sensors)
	{
		uint32_t now = system_get_time();
		i2c_sensor_init_all();
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

iram static bool_t background_task_command_handler(void)
{
	if(!socket_cmd.receive_ready)
		return(false);

	socket_cmd.receive_ready = false;

	switch(application_content(&socket_cmd.receive_buffer, socket_cmd.send_buffer))
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
			string_clear(&socket_cmd.send_buffer);
			string_append(&socket_cmd.send_buffer, "> empty command\n");
			break;
		}
		case(app_action_disconnect):
		{
			string_clear(&socket_cmd.send_buffer);
			string_append(&socket_cmd.send_buffer, "> disconnect\n");
			bg_action.disconnect = 1;
			break;
		}
		case(app_action_reset):
		{
			string_clear(&socket_cmd.send_buffer);
			string_append(&socket_cmd.send_buffer, "> reset\n");
			reset_state = reset_state_send_reply;
			break;
		}
		case(app_action_ota_commit):
		{
#if IMAGE_OTA == 1
			rboot_config rcfg = rboot_get_config();
			string_format(socket_cmd.send_buffer, "OTA commit slot %d\n", rcfg.current_rom);
			reset_state = reset_state_send_reply;
#endif
			break;
		}
	}

	socket_send(&socket_cmd, string_to_ptr(socket_cmd.send_buffer), string_length(socket_cmd.send_buffer));

	return(true);
}

iram static void background_task(os_event_t *events) // posted every ~100 ms = ~10 Hz
{
	stat_slow_timer++;
	config_wlan_mode_t wlan_mode;
	int wlan_mode_int;
	string_init(varname_wlan_mode, "wlan.mode");

	switch(reset_state)
	{
		case(reset_state_request_tcp_disconnect):
		{
			if(socket_cmd.tcp.child_socket != (struct espconn *)0)
				espconn_disconnect(socket_cmd.tcp.child_socket);
			reset_state = reset_state_wait_tcp_disconnect;
			break;
		}
		case(reset_state_wait):
		{
			reset_state = reset_state_go;
			msleep(100);
			break;
		}
		case(reset_state_go):
		{
			reset_state = reset_state_waiting;
			reset();
			return;
		}
		default: break;
	}

	if(uart_bridge_active && background_task_bridge_uart())
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
		if(socket_cmd.remote.proto == proto_tcp)
			stat_update_command_tcp++;
		else
			stat_update_command_udp++;

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

	if((wifi_station_get_connect_status() != STATION_GOT_IP) && (stat_update_idle == 300))
	{
		if(config_get_int(&varname_wlan_mode, -1, -1, &wlan_mode_int))
			wlan_mode = (config_wlan_mode_t)wlan_mode_int;
		else
			wlan_mode = config_wlan_mode_client;

		if(wlan_mode == config_wlan_mode_client)
		{
			wlan_mode_int = (int)config_wlan_mode_ap;
			config_set_int(&varname_wlan_mode, -1, -1, wlan_mode_int);
			config_get_int(&varname_wlan_mode, -1, -1, &wlan_mode_int);
			wlan_init();
		}
	}

	stat_update_idle++;
}

iram static void fast_timer_callback(void *arg)
{
	stat_fast_timer++;

	// timer runs every 10 ms = 100 Hz

	io_periodic();
}

iram static void slow_timer_callback(void *arg)
{
	// run background task every ~100 ms = ~10 Hz

	time_periodic();

	system_os_post(background_task_id, 0, 0);
}

void user_init(void);
irom void user_init(void)
{
	uint32_t *paint;
	volatile uint32_t sp;

	stat_stack_sp_initial = &sp;

	for(paint = (uint32_t *)sysram_top; paint < &sp; paint++)
	{
		*paint = stack_paint_magic;
		stat_stack_painted += 4;
	}

	static char uart_send_queue_buffer[1024];
	static char uart_receive_queue_buffer[1024];

	int uart_baud, uart_data, uart_stop, uart_parity_int;
	uart_parity_t uart_parity;

	string_init(varname_uart_baud, "uart.baud");
	string_init(varname_uart_data, "uart.data");
	string_init(varname_uart_stop, "uart.stop");
	string_init(varname_uart_parity, "uart.parity");

	system_set_os_print(0);

	queue_new(&uart_send_queue, sizeof(uart_send_queue_buffer), uart_send_queue_buffer);
	queue_new(&uart_receive_queue, sizeof(uart_receive_queue_buffer), uart_receive_queue_buffer);

	bg_action.disconnect = 0;
	bg_action.init_i2c_sensors = 1;
	bg_action.init_displays = 1;

	config_read();

	if(!config_get_int(&varname_uart_baud, -1, -1, &uart_baud))
		uart_baud = 115200;

	if(!config_get_int(&varname_uart_data, -1, -1, &uart_data))
		uart_data = 8;

	if(!config_get_int(&varname_uart_stop, -1, -1, &uart_stop))
		uart_stop = 1;

	if(config_get_int(&varname_uart_parity, -1, -1, &uart_parity_int))
		uart_parity = (uart_parity_t)uart_parity_int;
	else
		uart_parity = parity_none;

	uart_init(uart_baud, uart_data, uart_stop, uart_parity);

	os_install_putc1(&logchar);
	system_set_os_print(1);

	wifi_station_set_auto_connect(0);

	if(config_flags_get().flag.wlan_power_save)
		wifi_set_sleep_type(MODEM_SLEEP_T);
	else
		wifi_set_sleep_type(NONE_SLEEP_T);

	system_init_done_cb(user_init2);
}

irom static void wlan_event_handler(System_Event_t *event)
{
	int trigger_io, trigger_pin;
	io_trigger_t trigger = io_trigger_none;
	struct ip_info info;
	ip_addr_to_bytes_t local_ip;
	ip_addr_to_bytes_t mc_ip;
	string_init(varname_assoc_io, "trigger.assoc.io");
	string_init(varname_assoc_pin, "trigger.assoc.pin");

	switch(event->event)
	{
		case(EVENT_STAMODE_GOT_IP):
		{
			wifi_get_ip_info(STATION_IF, &info);
			local_ip.ip_addr = info.ip;
			mc_ip.byte[0] = 239;
			mc_ip.byte[1] = 255;
			mc_ip.byte[2] = 255;
			mc_ip.byte[3] = 254;
			espconn_igmp_join(&local_ip.ip_addr, &mc_ip.ip_addr);

			log("* join mc from %d.%d.%d.%d", local_ip.byte[0], local_ip.byte[1], local_ip.byte[2], local_ip.byte[3]);

			// fall through
		}
		case(EVENT_SOFTAPMODE_STACONNECTED):
		{
			trigger = io_trigger_on;
			break;
		}

		case(EVENT_STAMODE_DISCONNECTED):
		{
			// fall through
		}
		case(EVENT_SOFTAPMODE_STADISCONNECTED):
		{
			trigger = io_trigger_off;
			break;
		}
	}

	if((trigger != io_trigger_none) &&
			(config_get_int(&varname_assoc_io, -1, -1, &trigger_io) &&
			config_get_int(&varname_assoc_pin, -1, -1, &trigger_pin) &&
			(trigger_io >= 0) && (trigger_pin >= 0)))
		io_trigger_pin((string_t *)0, trigger_io, trigger_pin, trigger);
}

irom static void callback_received_cmd(socket_t *socket, int length, char *buffer)
{
	if(socket->receive_ready)
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
		stat_cmd_receive_buffer_overflow++;
		return;
	}

	buffer[length] = '\0';
	string_set(&socket->receive_buffer, buffer, length, length);
	socket->receive_ready = true;

	system_os_post(background_task_id, 0, 0);
}

iram static void callback_received_uart(socket_t *socket, const string_t *buffer, void *userdata)
{
	int current, byte;
	bool_t strip_telnet;
	telnet_strip_state_t telnet_strip_state;

	strip_telnet = config_flags_get().flag.strip_telnet;
	telnet_strip_state = ts_copy;

	for(current = 0; (current < length) && !queue_full(&uart_send_queue); current++)
	{
		byte = string_at(buffer, current);

		switch(telnet_strip_state)
		{
			case(ts_copy):
			{
				if(strip_telnet && (byte == 0xff))
					telnet_strip_state = ts_dodont;
				else
				{
					if(queue_full(&uart_send_queue))
						stat_uart_receive_buffer_overflow++;
					else
						queue_push(&uart_send_queue, byte);
				}

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

	uart_start_transmit(!queue_empty(&uart_send_queue));
}

irom static void callback_sent_cmd(socket_t *socket)
{
	if(reset_state == reset_state_send_reply)
	{
		if(socket->remote.proto == proto_udp)
			reset_state = reset_state_wait;
		else
			reset_state = reset_state_request_tcp_disconnect;
	}

	socket->receive_ready = false;
}

iram static void callback_sent_uart(socket_t *socket, void *userdata)
{
	if(!queue_empty(&uart_receive_queue))
		system_os_post(background_task_id, 0, 0); // retry to send data still in the fifo
}

// error

irom static void callback_error_cmd(socket_t *socket, int error, void *userdata)
{
	if(reset_state != reset_state_inactive)
		reset_state = reset_state_go;

	socket->receive_ready = false;
}

irom static void callback_disconnect_cmd(socket_t *socket)
{
	if((reset_state == reset_state_request_tcp_disconnect) || (reset_state == reset_state_wait_tcp_disconnect))
		reset_state = reset_state_wait;

	socket->receive_ready = false;
}

irom static void callback_accept_uart(socket_t *socket)
{
	queue_flush(&uart_send_queue);
	queue_flush(&uart_receive_queue);
}

irom static void user_init2(void)
{
	string_new(static, send_buffer_uart, 1024);
	string_new(static, send_buffer_cmd, 4096 + 8); // need a few extra bytes to make up exactly 4096 bytes for OTA

	int uart_port, uart_timeout;
	int cmd_port, cmd_timeout;

	string_init(varname_bridge_port, "bridge.port");
	string_init(varname_bridge_timeout, "bridge.timeout");
	string_init(varname_cmd_port, "cmd.port");
	string_init(varname_cmd_timeout, "cmd.timeout");

	if(!config_get_int(&varname_bridge_port, -1, -1, &uart_port))
		uart_port = 0;

	if(!config_get_int(&varname_bridge_timeout, -1, -1, &uart_timeout))
		uart_timeout = 90;

	if(!config_get_int(&varname_cmd_port, -1, -1, &cmd_port))
		cmd_port = 24;

	if(!config_get_int(&varname_cmd_timeout, -1, -1, &cmd_timeout))
		cmd_timeout = 90;

	if(config_flags_get().flag.cpu_high_speed)
		system_update_cpu_freq(160);
	else
		system_update_cpu_freq(80);

	wifi_set_event_handler_cb(wlan_event_handler);

	wlan_init();
	time_init();
	io_init();

	socket_create(true, true, &socket_cmd, &send_buffer_cmd, cmd_port, cmd_timeout,
			callback_received_cmd, callback_sent_cmd, callback_error_cmd, callback_disconnect_cmd, (void *)0);

	if(uart_port > 0)
	{
		socket_create(true, true, &socket_uart,	&send_buffer_uart,	uart_port,	uart_timeout,
				callback_received_uart, callback_sent_uart, (void *)0, (void *)0, callback_accept_uart);

		uart_bridge_active = true;
	}

	system_os_task(background_task, background_task_id, background_task_queue, background_task_queue_length);

	os_timer_setfn(&slow_timer, slow_timer_callback, (void *)0);
	os_timer_arm(&slow_timer, 100, 1); // slow system timer / 10 Hz / 100 ms

	os_timer_setfn(&fast_timer, fast_timer_callback, (void *)0);
	os_timer_arm(&fast_timer, 10, 1); // fast system timer / 100 Hz / 10 ms
}

irom bool_t wlan_init(void)
{
	int wlan_mode_int;
	config_wlan_mode_t wlan_mode;
	string_new(, config_string, 64);
	int channel;
	struct station_config cconf;
	struct softap_config saconf;
	string_init(varname_wlan_mode, "wlan.mode");
	string_init(varname_wlan_client_ssid, "wlan.client.ssid");
	string_init(varname_wlan_client_passwd, "wlan.client.passwd");
	string_init(varname_wlan_ap_ssid, "wlan.ap.ssid");
	string_init(varname_wlan_ap_passwd, "wlan.ap.passwd");
	string_init(varname_wlan_ap_channel, "wlan.ap.channel");

	if(config_get_int(&varname_wlan_mode, -1, -1, &wlan_mode_int))
		wlan_mode = (config_wlan_mode_t)wlan_mode_int;
	else
		wlan_mode = config_wlan_mode_client;

	switch(wlan_mode)
	{
		case(config_wlan_mode_client):
		{
			memset(&cconf, 0, sizeof(cconf));
			cconf.bssid_set = 0;

			string_clear(&config_string);

			if(config_get_string(&varname_wlan_client_ssid, -1, -1, &config_string))
				strecpy(cconf.ssid, string_to_cstr(&config_string), sizeof(cconf.ssid));
			else
				strecpy(cconf.ssid, "esp", sizeof(cconf.ssid));

			string_clear(&config_string);

			if(config_get_string(&varname_wlan_client_passwd, -1, -1, &config_string))
				strecpy(cconf.password, string_to_cstr(&config_string), sizeof(cconf.password));
			else
				strecpy(cconf.password, "espespesp", sizeof(cconf.password));

			log("* set wlan mode to client, ssid=\"%s\", passwd=\"%s\"\r\n", cconf.ssid, cconf.password);

			wifi_station_disconnect();
			wifi_set_opmode_current(STATION_MODE);
			wifi_station_set_config_current(&cconf);
			wifi_station_connect();

			break;
		}

		case(config_wlan_mode_ap):
		{
			memset(&saconf, 0, sizeof(saconf));

			if(config_get_string(&varname_wlan_ap_ssid, -1, -1, &config_string))
				strecpy(saconf.ssid, string_to_cstr(&config_string), sizeof(saconf.ssid));
			else
				strecpy(saconf.ssid, "esp", sizeof(saconf.ssid));

			if(config_get_string(&varname_wlan_ap_passwd, -1, -1, &config_string))
				strecpy(saconf.password, string_to_cstr(&config_string), sizeof(saconf.password));
			else
				strecpy(saconf.password, "espespesp", sizeof(saconf.password));

			if(!config_get_int(&varname_wlan_ap_channel, -1, -1, &channel))
				channel = 1;

			saconf.ssid_len = strlen(saconf.ssid);
			saconf.channel = channel;
			saconf.authmode = AUTH_WPA_WPA2_PSK;
			saconf.ssid_hidden = 0;
			saconf.max_connection = 1;
			saconf.beacon_interval = 100;

			log("* set wlan mode to ap, ssid=\"%s\", passwd=\"%s\", channel=%d\r\n",
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
