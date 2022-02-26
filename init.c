#include "init.h"

#include "util.h"
#include "sys_string.h"
#include "io.h"
#include "stats.h"
#include "sys_time.h"
#include "dispatch.h"
#include "sequencer.h"
#include "io_gpio.h"
#include "lwip-interface.h"

#include <stdint.h>
#include <stdbool.h>

static void user_init2(void);
void user_init(void);

static const partition_item_t partition_items[] =
{
	{	SYSTEM_PARTITION_RF_CAL, 				RFCAL_OFFSET,				RFCAL_SIZE,				},
	{	SYSTEM_PARTITION_PHY_DATA,				PHYDATA_OFFSET,				PHYDATA_SIZE,			},
	{	SYSTEM_PARTITION_SYSTEM_PARAMETER,		SYSTEM_CONFIG_OFFSET,		SYSTEM_CONFIG_SIZE,		},
	{	SYSTEM_PARTITION_CUSTOMER_BEGIN + 0,	USER_CONFIG_OFFSET,			USER_CONFIG_SIZE,		},
	{	SYSTEM_PARTITION_CUSTOMER_BEGIN + 1,	OFFSET_BOOT,				SIZE_BOOT,				},
	{	SYSTEM_PARTITION_CUSTOMER_BEGIN + 2,	OFFSET_RBOOT_CFG,			SIZE_RBOOT_CFG,			},
	{	SYSTEM_PARTITION_CUSTOMER_BEGIN + 3,	OFFSET_IMG_0,				SIZE_IMG,				},
	{	SYSTEM_PARTITION_CUSTOMER_BEGIN + 4,	OFFSET_IMG_1,				SIZE_IMG,				},
	{	SYSTEM_PARTITION_CUSTOMER_BEGIN + 5,	SEQUENCER_FLASH_OFFSET_0,	SEQUENCER_FLASH_SIZE,	},
	{	SYSTEM_PARTITION_CUSTOMER_BEGIN + 6,	SEQUENCER_FLASH_OFFSET_1,	SEQUENCER_FLASH_SIZE,	},
	{	SYSTEM_PARTITION_CUSTOMER_BEGIN + 7,	PICTURE_FLASH_OFFSET_0,		PICTURE_FLASH_SIZE,		},
	{	SYSTEM_PARTITION_CUSTOMER_BEGIN + 8,	PICTURE_FLASH_OFFSET_1,		PICTURE_FLASH_SIZE,		},
	{	SYSTEM_PARTITION_CUSTOMER_BEGIN + 9,	FONT_FLASH_OFFSET_0,		FONT_FLASH_SIZE,		},
	{	SYSTEM_PARTITION_CUSTOMER_BEGIN + 10,	FONT_FLASH_OFFSET_1,		FONT_FLASH_SIZE,		},
	{	SYSTEM_PARTITION_CUSTOMER_BEGIN + 11,	MISC_FLASH_OFFSET,			MISC_FLASH_SIZE,		},
};

void user_spi_flash_dio_to_qio_pre_init(void);
iram void user_spi_flash_dio_to_qio_pre_init(void)
{
}

		volatile uint32_t	*stack_stack_sp_initial;
		int					stack_stack_painted;
static	volatile uint32_t	*stack_stack_paint_ptr; // this cannot be on the stack

iram void stack_paint_stack(void)
{
	// don't declare stack variables here, they will get overwritten

	volatile uint32_t sp;
	stack_stack_sp_initial = &sp;

	for(stack_stack_paint_ptr = (uint32_t *)stack_top; (stack_stack_paint_ptr < (uint32_t *)stack_bottom) && (stack_stack_paint_ptr < (volatile uint32_t *)stack_stack_sp_initial); stack_stack_paint_ptr++)
	{
		*stack_stack_paint_ptr = stack_paint_magic;
		stack_stack_painted += 4;
	}
}

void user_pre_init(void);
iram void user_pre_init(void)
{
	stat_flags.user_pre_init_called = 1;
	stat_flags.user_pre_init_success = system_partition_table_regist(partition_items, sizeof(partition_items) / sizeof(*partition_items), FLASH_SIZE_SDK);
	system_phy_set_powerup_option(3); /* request full calibration */
}

uint32_t user_iram_memory_is_enabled(void);
iram attr_const uint32_t user_iram_memory_is_enabled(void)
{
	return(0);
}

void user_init(void)
{
	stack_paint_stack();

	system_set_os_print(0);
	dispatch_init1();
	config_init();
	uart_init();
	uart_set_initial(0);
	uart_set_initial(1);
	os_install_putc1(&logchar);
	system_set_os_print(1);
	power_save_enable(config_flags_match(flag_wlan_power_save));
	system_init_done_cb(user_init2);
}

static void user_init2(void)
{
	stat_heap_min = stat_heap_max = xPortGetFreeHeapSize();

	dispatch_init2();

	if(config_flags_match(flag_cpu_high_speed))
		system_update_cpu_freq(160);
	else
		system_update_cpu_freq(80);

	if(!wlan_init_from_config())
		wlan_init_start_recovery();
	application_init();
	time_init();
	io_init();

	log("* boot done\n");

	if(config_flags_match(flag_auto_sequencer))
		sequencer_start(0, 1);
}

static void wlan_init(config_wlan_mode_t wlan_mode, const string_t *ssid, const string_t *password, unsigned int channel)
{
	struct station_config cconf;
	struct softap_config saconf;

	if(!wifi_station_ap_number_set(1))
		log("wifi_station_ap_number_set failed\n");

	switch(wlan_mode)
	{
		case(config_wlan_mode_client):
		{
			if((wifi_get_opmode() != STATION_MODE) ||
					!wifi_station_get_config(&cconf) ||
					!wifi_station_get_auto_connect() ||
					!string_match_cstr(ssid, (const char *)cconf.ssid) ||
					!string_match_cstr(password, (const char *)cconf.password))
			{
				memset(&cconf, 0, sizeof(cconf));
				strecpy((char *)cconf.ssid, string_buffer(ssid), sizeof(cconf.ssid));
				strecpy((char *)cconf.password, string_buffer(password), sizeof(cconf.password));
				cconf.bssid_set = 0;
				cconf.channel = 0;

				wifi_station_disconnect();
				wifi_set_opmode(STATION_MODE);
				wifi_station_set_config(&cconf);
				wifi_station_connect();
				wifi_station_set_auto_connect(1);
			}

			break;
		}

		default:
		{
			memset(&saconf, 0, sizeof(saconf));

			strecpy(saconf.ssid, string_buffer(ssid), sizeof(saconf.ssid));
			strecpy(saconf.password, string_buffer(password), sizeof(saconf.password));

			saconf.ssid_len = strlen(saconf.ssid);
			saconf.channel = channel;
			saconf.authmode = AUTH_WPA_WPA2_PSK;
			saconf.ssid_hidden = 0;
			saconf.max_connection = 1;
			saconf.beacon_interval = 100;

			wifi_station_disconnect();
			wifi_set_opmode_current(SOFTAP_MODE);
			wifi_softap_set_config_current(&saconf);

			break;
		}
	}
}

bool wlan_init_from_config(void)
{
	unsigned int mode_int;
	config_wlan_mode_t mode;
	string_new(, ssid, 64);
	string_new(, password, 64);
	unsigned int channel;

	if(config_get_uint("wlan.mode", &mode_int, -1, -1))
		mode = (config_wlan_mode_t)mode_int;
	else
		mode = config_wlan_mode_client;

	channel = -1;

	switch(mode)
	{
		case(config_wlan_mode_client):
		{
			if(!config_get_string("wlan.client.ssid", &ssid, -1, -1) ||
					!config_get_string("wlan.client.passwd", &password, -1, -1))
				return(false);

			break;
		}

		case(config_wlan_mode_ap):
		{
			if(!config_get_string("wlan.ap.ssid", &ssid, -1, -1) ||
					!config_get_string("wlan.ap.passwd", &password, -1, -1) ||
					!config_get_uint("wlan.ap.channel", &channel, -1, -1))
				return(false);

			break;
		}

		default:
		{
			return(false);
		}
	}

	stat_flags.wlan_recovery_mode_active = 0;
	wlan_init(mode, &ssid, &password, channel);
	return(true);
}

void wlan_init_start_recovery(void)
{
	config_flag_change_nosave(flag_log_to_uart, true);
	config_flag_change_nosave(flag_log_to_buffer, true);
	config_flag_change_nosave(flag_cmd_from_uart, true);

	uart_invert(0, uart_dir_tx, false);
	uart_invert(0, uart_dir_rx, false);
	uart_loopback(0, false);

	string_init(static, wlan_default_ssid, "esp");
	string_init(static, wlan_default_password, "espespesp");

	stat_flags.wlan_recovery_mode_active = 1;

	wlan_init(config_wlan_mode_ap, &wlan_default_ssid, &wlan_default_password, 1);

	log("* WLAN CAN'T CONNECT, entering recovery mode. *\n"
				"  now, to configure wlan parameters\n"
				"  - EITHER associate to SSID \"esp\" using passwd \"espespesp\"\n"
				"      and then connect to 192.168.4.1:22 using telnet or browser\n"
				"  - OR connect to UART\n"
				"  - THEN issue these commands:\n"
				"      wcc <ssid> <passwd>\n"
				"      wm client\n"
				"  after that, issue a reset command to restore temporarily changed flags.\n");
}

bool wlan_ap_switch(const mac_addr_t mac)
{
	unsigned int mode_int;
	config_wlan_mode_t mode;
	string_new(, ssid, 64);
	string_new(, password, 64);
	struct station_config cconf;

	if(config_get_uint("wlan.mode", &mode_int, -1, -1))
		mode = (config_wlan_mode_t)mode_int;
	else
		mode = config_wlan_mode_client;

	if(mode != config_wlan_mode_client)
		return(false);

	if(!config_get_string("wlan.client.ssid", &ssid, -1, -1) ||
			!config_get_string("wlan.client.passwd", &password, -1, -1))
		return(false);

	memset(&cconf, 0, sizeof(cconf));
	strecpy((char *)cconf.ssid, string_buffer(&ssid), sizeof(cconf.ssid));
	strecpy((char *)cconf.password, string_buffer(&password), sizeof(cconf.password));
	cconf.bssid[0] = mac[0];
	cconf.bssid[1] = mac[1];
	cconf.bssid[2] = mac[2];
	cconf.bssid[3] = mac[3];
	cconf.bssid[4] = mac[4];
	cconf.bssid[5] = mac[5];
	cconf.bssid_set = 1;
	cconf.channel = 0;
	cconf.all_channel_scan = 1;

	wifi_station_disconnect();
	wifi_set_opmode(STATION_MODE);
	wifi_station_set_config_current(&cconf);

	if(!wifi_station_connect())
	{
		log("wlan_channel_switch: set config current failed\n");
		return(false);
	}

	return(true);
}

void multicast_init_groups(void)
{
	string_new(, ip, 32);
	unsigned int entry;
	ip_addr_to_bytes_t addr;

	for(entry = 0; entry < 8; entry++)
	{
		string_clear(&ip);

		if(config_get_string("multicast-group.%u", &ip, entry, -1))
		{
			addr.ip_addr = ip_addr(string_to_cstr(&ip));

			if((addr.byte[0] > 0) &&
					(addr.byte[1] > 0) &&
					(addr.byte[2] > 0) &&
					(addr.byte[3] > 0))
				if(!lwip_if_join_mc(addr.ip_addr))
					log("join mc group failed\n");
		}
	}
}
