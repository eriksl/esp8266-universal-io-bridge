#include "wlan.h"

#include "util.h"
#include "config.h"
#include "stats.h"
#include "sys_time.h"
#include "string.h"
#include "lwip-interface.h"

#include <stdbool.h>

roflash static const char *const phy[] = {
	"unknown",
	"802.11b",
	"802.11g",
	"802.11n",
	"unknown"
};

roflash static const char *const slp[] =
{
	"none",
	"light",
	"modem",
	"unknown"
};

roflash static const char status_msg[][16] =
{
	"OK",
	"FAIL",
	"PENDING",
	"BUSY",
	"CANCEL"
};

static unsigned int association_state_time;

static struct attr_packed
{
	unsigned int bootstrap_mode:1;
	unsigned int recovery_mode:1;
	unsigned int associated:1;
} flags;

assert_size(flags, 1);

typedef struct
{
	mac_addr_t mac;
	int channel;
	int rssi;
} access_point_t;

assert_size(access_point_t, 16);

enum
{
	access_points_size = 5,
};

typedef struct
{
	int entries;
	int selected;
	int scanning;
	char ssid[32];
	char password[32];
	access_point_t ap[access_points_size];
} access_points_t;

static access_points_t access_points;

assert_size(access_points_t, 156);

roflash const char help_description_stats_wlan[] =			"statistics from the wlan subsystem";
roflash const char help_description_wlan_ap_config[] =		"configure access point mode wlan params, supply ssid, passwd and channel";
roflash const char help_description_wlan_client_config[] =	"configure client mode wlan params, supply ssid and passwd";
roflash const char help_description_wlan_mode[] =			"set wlan mode: client or ap";
roflash const char help_description_wlan_scan[] =			"scan wlan, see log to retrieve the results";

static void wlan_event_handler(System_Event_t *event)
{
	switch(event->event)
	{
		case(EVENT_STAMODE_CONNECTED):
		{
			if(stat_init_associate_time_us == 0)
				stat_init_associate_time_us = time_get_us();

			break;
		}
		case(EVENT_STAMODE_GOT_IP):
		{
			if(stat_init_ip_time_us == 0)
				stat_init_ip_time_us = time_get_us();

			wlan_multicast_init_groups();
			time_sntp_start();

			flags.recovery_mode = 0;
			flags.associated = 1;
			association_state_time = 0;

			[[fallthrough]];
		}
		case(EVENT_SOFTAPMODE_STACONNECTED):
		{
			dispatch_post_task(task_prio_medium, task_alert_association, 0, 0, 0);
			break;
		}
		case(EVENT_STAMODE_DISCONNECTED):
		{
			if(flags.associated)
			{
				log("[wlan] deassociate\n");
				association_state_time = 0;
				flags.associated = 0;
			}
			else
				log("[wlan] deassociate from unassociated state\n");

			[[fallthrough]];
		}
		case(EVENT_SOFTAPMODE_STADISCONNECTED):
		{
			dispatch_post_task(task_prio_high, task_alert_disassociation, 0, 0, 0);
			break;
		}
	}
}

void wlan_init(void)
{
	access_points.entries = 0;
	access_points.selected = 0;
	access_points.scanning = 0;
	flags.bootstrap_mode = 1;
	flags.recovery_mode = 0;
	flags.associated = 0;
	association_state_time = 0;
	power_save_enable(config_flags_match(flag_wlan_power_save));
	wifi_station_ap_number_set(0);
	wifi_station_set_auto_connect(true);
	wifi_station_set_reconnect_policy(true);
	wifi_set_phy_mode(PHY_MODE_11N);
	wifi_set_event_handler_cb(wlan_event_handler);
}

void wlan_periodic(void)
{
	// called once per second

	association_state_time++;

	if(flags.associated)
	{
		if(flags.bootstrap_mode && (association_state_time > 60))
		{
			flags.bootstrap_mode = 0;
			dispatch_post_task(task_prio_high, task_wlan_reconnect, 0, 0, 0);
		}
	}
	else
	{
		// fallback to config-ap-mode when not connected or no ip within 60 seconds, reset after 5 minutes.

		if(!flags.recovery_mode && (association_state_time > 60))
			dispatch_post_task(task_prio_high, task_wlan_recovery, 0, 0, 0);
		else
			if(association_state_time > 300)
				dispatch_post_task(task_prio_high, task_reset, 0, 0, 0);
	}
}

static void wlan_associate_callback(void *arg, STATUS status)
{
	struct bss_info *bss;
	char status_string[32];
	struct station_config cconf;
	access_point_t *ap;
	int ix;

	if(access_points.scanning > 0)
		access_points.scanning--;

	flash_to_dram(true, status <= CANCEL ? status_msg[status] : "<invalid>", status_string, sizeof(status_string));
	log("[wlan] scan callback result: %s\n", status_string);

	for(bss = arg; bss; bss = bss->next.stqe_next)
	{
		ap = &access_points.ap[access_points.entries];

		ap->channel = bss->channel;
		ap->rssi = bss->rssi;
		ap->mac[0] = bss->bssid[0];
		ap->mac[1] = bss->bssid[1];
		ap->mac[2] = bss->bssid[2];
		ap->mac[3] = bss->bssid[3];
		ap->mac[4] = bss->bssid[4];
		ap->mac[5] = bss->bssid[5];

		if(bss->rssi > access_points.ap[access_points.selected].rssi)
			access_points.selected = access_points.entries;

		access_points.entries++;
	}

	if(access_points.entries == 0)
	{
		log("[wlan] no access points found, giving up\n");
		return;
	}

	log("[wlan] access points found: \n");

	for(ix = 0; ix < access_points.entries; ix++)
	{
		string_new(, line, 64);

		ap = &access_points.ap[ix];

		string_format(&line, "%c ch: %2d, rssi: %3d, bssid: %02x:%02x:%02x:%02x:%02x:%02x\n",
				ix == access_points.selected ? '*' : ' ',
				ap->channel,
				ap->rssi,
				ap->mac[0],
				ap->mac[1],
				ap->mac[2],
				ap->mac[3],
				ap->mac[4],
				ap->mac[5]);

		log("%s", string_to_cstr(&line));
	}

	ap = &access_points.ap[access_points.selected];

	if(flags.associated && (wifi_get_channel() == ap->channel))
		log("[wlan] already associated to optimal access point\n");
	else
	{
		log("[wlan] reconnecting to optimal access point\n");

		memset(&cconf, 0, sizeof(cconf));
		strecpy((char *)cconf.ssid, access_points.ssid, sizeof(cconf.ssid));
		strecpy((char *)cconf.password, access_points.password, sizeof(cconf.password));
		cconf.bssid_set = 1;
		cconf.bssid[0] = ap->mac[0];
		cconf.bssid[1] = ap->mac[1];
		cconf.bssid[2] = ap->mac[2];
		cconf.bssid[3] = ap->mac[3];
		cconf.bssid[4] = ap->mac[4];
		cconf.bssid[5] = ap->mac[5];
		cconf.channel = ap->channel;
		cconf.all_channel_scan = 0;
		cconf.threshold.rssi = 0;
		cconf.threshold.authmode = AUTH_OPEN;
		wifi_station_set_config(&cconf);
		wifi_station_connect();
	}
}

static bool wlan_associate(string_t *ssid, string_t *password)
{
	struct scan_config sc =
	{
		.ssid = (char *)0,
		.bssid = (char *)0,
		.channel = 0,
		.show_hidden = 0,
		.scan_type = WIFI_SCAN_TYPE_ACTIVE,
		.scan_time.active.min = 500,
		.scan_time.active.max = 2000,
	};

	access_points.scanning++;

	if(access_points.scanning > 1)
	{
		access_points.scanning--;
		log("[wlan] duplicate association request\n");
		return(true);
	}

	if(!wifi_set_opmode_current(STATION_MODE))
		return(false);

	access_points.entries = 0;
	access_points.selected = 0;
	strecpy(access_points.ssid, string_to_cstr(ssid), sizeof(access_points.ssid));
	strecpy(access_points.password, string_to_cstr(password), sizeof(access_points.password));

	string_to_cstr(ssid);
	sc.ssid = string_buffer_nonconst(ssid);

	log("[wlan] start scan for %s\n", access_points.ssid);

	if(!wifi_station_scan(&sc, wlan_associate_callback))
		return(false);

	return(true);
}

static bool wlan_access_point(string_t *ssid, string_t *password, unsigned int channel)
{
	struct softap_config saconf;

	memset(&saconf, 0, sizeof(saconf));
	strecpy(saconf.ssid, string_to_cstr(ssid), sizeof(saconf.ssid));
	strecpy(saconf.password, string_to_cstr(password), sizeof(saconf.password));

	saconf.ssid_len = strlen(saconf.ssid);
	saconf.channel = channel;
	saconf.authmode = AUTH_WPA_WPA2_PSK;
	saconf.ssid_hidden = 0;
	saconf.max_connection = 1;
	saconf.beacon_interval = 100;

	wifi_station_disconnect();

	if(!wifi_set_opmode_current(SOFTAP_MODE))
		return(false);

	if(!wifi_softap_set_config_current(&saconf))
		return(false);

	return(true);
}

bool wlan_reconnect(void)
{
	unsigned int mode_int;
	config_wlan_mode_t mode;
	string_new(, ssid, 64);
	string_new(, password, 64);
	unsigned int channel;

	log("[wlan] start reconnect\n");

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

			flags.recovery_mode = 0;
			return(wlan_associate(&ssid, &password));
		}

		case(config_wlan_mode_ap):
		{
			if(!config_get_string("wlan.ap.ssid", &ssid, -1, -1) ||
					!config_get_string("wlan.ap.passwd", &password, -1, -1) ||
					!config_get_uint("wlan.ap.channel", &channel, -1, -1))
				return(false);

			flags.recovery_mode = 0;
			return(wlan_access_point(&ssid, &password, channel));
		}
	}

	return(false);
}

void wlan_start_recovery(void)
{
	string_init(, wlan_default_ssid, "esp");
	string_init(, wlan_default_password, "espespesp");

	config_flag_change_nosave(flag_log_to_uart, true);
	config_flag_change_nosave(flag_log_to_buffer, true);
	config_flag_change_nosave(flag_cmd_from_uart, true);

	uart_invert(0, uart_dir_tx, false);
	uart_invert(0, uart_dir_rx, false);
	uart_loopback(0, false);

	flags.bootstrap_mode = 0;
	flags.recovery_mode = 1;
	flags.associated = 0;
	association_state_time = 0;

	wlan_access_point(&wlan_default_ssid, &wlan_default_password, 1);

	log("* WLAN CAN'T CONNECT, entering recovery mode. *\n");
	msleep(10);
	log("  configure wlan parameters\n");
	msleep(10);
	log("  - EITHER associate to SSID \"%s\" using passwd \"%s\"\n", string_to_cstr(&wlan_default_ssid), string_to_cstr(&wlan_default_password));
	msleep(10);
	log("    and then connect to 192.168.4.1:22 using telnet or browser\n");
	msleep(10);
	log("  - OR connect to UART\n");
	msleep(10);
	log("  - THEN issue these commands:\n");
	msleep(10);
	log("    wcc <ssid> <passwd>\n");
	msleep(10);
	log("    wm client\n");
	msleep(10);
	log("  after that, issue a reset command to restore temporarily changed flags.\n");
}

void wlan_multicast_init_groups(void)
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
					log("[wlan] join mc group failed\n");
		}
	}
}

void stats_wlan(string_t *dst)
{
	sdk_mac_addr_t mac_addr;
	struct ip_info ip_addr_info;
	struct station_config config;
	struct station_config sc[5], *scp;
	unsigned int scn, scni, scnc;
	wifi_country_t wc;
	int ix;

	roflash static const char auth_mode[][20] =
	{
		"OTHER",
		"WEP",
		"WPA PSK",
		"WPA2 PSK",
		"WPA PSK + WPA2 PSK"
	};

	wifi_station_get_config_default(&config);
	wifi_get_country(&wc);

	string_append(dst, "> current wlan state: ");

	if(flags.recovery_mode)
		string_append(dst, "in recovery mode");
	else
		if(flags.associated)
			string_append(dst, "associated");
		else
			string_append(dst, "not associated");

	if(flags.bootstrap_mode)
		string_append(dst, ", in bootstrap mode");

	string_format(dst, " for %u sec\n", association_state_time);

	string_format(dst, "> ssid in flash: \"%s\", passwd: \"%s\"\n",
			config.ssid, config.password);

	wifi_station_get_config(&config);
	wc.cc[2] = '\0';

	string_format(dst, "> ssid active: \"%s\", passwd: \"%s\"\n",
			config.ssid, config.password);

	string_format(dst,
			"> channel: %u\n"
			"> phy mode: %s\n"
			"> sleep mode: %s\n"
			"> max sleep level: %s\n"
			"> listen interval: %d\n"
			"> signal strength: %d dB\n"
			"> country: %s [%d - %d]\n"
			">\n",
				wifi_get_channel(),
				phy[wifi_get_phy_mode()],
				slp[wifi_get_sleep_type()],
				onoff(wifi_get_sleep_level()),
				wifi_get_listen_interval(),
				wifi_station_get_rssi(),
				wc.cc, wc.schan, wc.schan + wc.nchan);

	wifi_get_ip_info(SOFTAP_IF, &ip_addr_info);

	string_append(dst, "> ap mac address: ");
	wifi_get_macaddr(SOFTAP_IF, mac_addr);
	mac_to_string(dst, mac_addr);
	string_append(dst, "\n");

	string_append(dst, "> ap ip address: ");
	string_ip(dst, ip_addr_info.ip);
	string_append(dst, "\n");

	string_append(dst, "> ap gateway: ");
	string_ip(dst, ip_addr_info.gw);
	string_append(dst, "\n");

	string_append(dst, "> ap ip netmask: ");
	string_ip(dst, ip_addr_info.netmask);
	string_append(dst, "\n");

	string_append(dst, ">\n");

	wifi_get_ip_info(STATION_IF, &ip_addr_info);

	string_append(dst, "> station mac address: ");
	wifi_get_macaddr(STATION_IF, mac_addr);
	mac_to_string(dst, mac_addr);
	string_append(dst, "\n");

	string_append(dst, "> station ip address: ");
	string_ip(dst, ip_addr_info.ip);
	string_append(dst, "\n");

	string_append(dst, "> station gateway: ");
	string_ip(dst, ip_addr_info.gw);
	string_append(dst, "\n");

	string_append(dst, "> station ip netmask: ");
	string_ip(dst, ip_addr_info.netmask);
	string_append(dst, "\n>\n");

	memset(sc, 0, sizeof(sc));

	if((scn = wifi_station_get_ap_info(sc)) < 1)
		string_append(dst, "> no ap info\n");
	else
	{
		scnc = wifi_station_get_current_ap_id();

		for(scni = 0; scni < scn; scni++)
		{
			scp = &sc[scni];

			string_format(dst, "> ap %c#%u: %s/%s@%d, %02x:%02x:%02x:%02x:%02x:%02x %d, %d, %d, %d, ",
				scnc == scni ? '*' : ' ',
				scni,
				scp->ssid, scp->password,
				scp->channel,
				scp->bssid[0], scp->bssid[1], scp->bssid[2],
				scp->bssid[3], scp->bssid[4], scp->bssid[5],
				scp->threshold.rssi,
				scp->bssid_set,
				scp->open_and_wep_mode_disable,
				scp->all_channel_scan);
			string_append_cstr_flash(dst, auth_mode[scp->threshold.authmode]);
			string_format(dst, "\n");
		}
	}

	string_append(dst, ">\n> access points selection\n>\n");

	for(ix = 0; ix < access_points.entries; ix++)
	{
		const access_point_t *ap = &access_points.ap[ix];

		string_format(dst, "> %c ch: %2d, rssi: %3d, bssid: %02x:%02x:%02x:%02x:%02x:%02x\n",
				(ix == access_points.selected) ? '*' : ' ',
				ap->channel,
				ap->rssi,
				ap->mac[0],
				ap->mac[1],
				ap->mac[2],
				ap->mac[3],
				ap->mac[4],
				ap->mac[5]);
	}
}

app_action_t application_function_stats_wlan(app_params_t *parameters)
{
	stats_wlan(parameters->dst);
	return(app_action_normal);
}

static void wlan_scan_done_callback(void *arg, STATUS status)
{
	roflash static const char auth_mode_msg[][16] =
	{
		"OTHER",
		"WEP",
		"WPA1-PSK",
		"WPA2-PSK",
		"WPA1/2-PSK"
	};

	roflash static const char cipher_type[][16] =
	{
		"NONE",
		"WEP40",
		"WEP104",
		"TKIP",
		"AES",
		"TKIP/AES",
		"UNKNOWN",
	};

	roflash static const char fmt_string_1[] = ">  %-16s %-3s %-4s %-10s %-9s %-9s %-4s %s\n";
	roflash static const char fmt_string_2[] = "> %c%-16s %3u %4d %-10s %-9s %-9s %4d %02x:%02x:%02x:%02x:%02x:%02x\n";

	struct bss_info *bss;
	const char *ssid;
	char status_string[32];
	char auth_mode_string[32];
	char pairwise_cipher_string[32];
	char groupwise_cipher_string[32];
	bool selected;

	flash_to_dram(true, status <= CANCEL ? status_msg[status] : "<invalid>", status_string, sizeof(status_string));

	logbuffer_clear(); // make sure as much room is available as is possible

	log("wlan scan result: %s\n", status_string);
	log_from_flash_n(fmt_string_1, "SSID", "CHN", "RSSI", "AUTH", "PAIR", "GROUP", "OFFS", "BSSID");

	for(bss = arg; bss; bss = bss->next.stqe_next)
	{
		if(strcmp(access_points.ssid, "") && !strncmp((const char *)bss->ssid, access_points.ssid, sizeof(access_points.ssid)))
			selected = true;
		else
			selected = false;

		if(!strcmp((const char *)bss->ssid, ""))
			ssid = "<hidden>";
		else
			ssid = (const char *)bss->ssid;

		flash_to_dram(true, bss->authmode < AUTH_MAX ? auth_mode_msg[bss->authmode] : "<invalid>", auth_mode_string, sizeof(auth_mode_string));
		flash_to_dram(true, cipher_type[bss->pairwise_cipher], pairwise_cipher_string, sizeof(pairwise_cipher_string));
		flash_to_dram(true, cipher_type[bss->group_cipher], groupwise_cipher_string, sizeof(groupwise_cipher_string));

		log_from_flash_n(fmt_string_2,
				selected ? '*' : ' ',
				ssid,
				bss->channel,
				bss->rssi,
				auth_mode_string,
				pairwise_cipher_string, groupwise_cipher_string,
				bss->freq_offset,
				bss->bssid[0], bss->bssid[1], bss->bssid[2], bss->bssid[3], bss->bssid[4], bss->bssid[5]);
	}
}

app_action_t application_function_wlan_scan(app_params_t *parameters)
{
	struct scan_config sc =
	{
		.ssid = (char *)0,
		.bssid = (char *)0,
		.channel = 0,
		.show_hidden = 1,
		.scan_type = WIFI_SCAN_TYPE_ACTIVE,
		.scan_time.active.min = 100,
		.scan_time.active.max = 1000,
	};
	string_new(, ssid, 64);

	string_clear(&ssid);

	if(parse_string(1, parameters->src, &ssid, ' ') == parse_ok)
	{
		if(string_match_cstr(&ssid, "-"))
			sc.ssid = access_points.ssid;
		else
		{
			string_to_cstr(&ssid); // ensure \0
			sc.ssid = string_buffer_nonconst(&ssid);
		}
	}

	wifi_station_scan(&sc, wlan_scan_done_callback);
	string_append(parameters->dst, "wlan scan started, see log to retrieve the results\n");

	return(app_action_normal);
}

app_action_t application_function_wlan_ap_configure(app_params_t *parameters)
{
	unsigned int channel;
	string_new(, ssid, 64);
	string_new(, passwd, 64);

	if((parse_string(1, parameters->src, &ssid, ' ') == parse_ok) && (parse_string(2, parameters->src, &passwd, ' ') == parse_ok) &&
			(parse_uint(3, parameters->src, &channel, 0, ' ') == parse_ok))
	{
		if((channel < 1) || (channel > 13))
		{
			string_format(parameters->dst, "> channel %u out of range (1-13)\n", channel);
			return(app_action_error);
		}

		if(string_length(&passwd) < 8)
		{
			string_format(parameters->dst, "> passwd \"%s\" too short (length must be >= 8)\n",
					string_to_cstr(&passwd));
			return(app_action_error);
		}

		if(!config_open_write())
		{
			string_append(parameters->dst, "> cannot set config (open)\n");
			return(app_action_error);
		}

		if(!config_set_string("wlan.ap.ssid", string_to_cstr(&ssid), -1, -1))
		{
			config_abort_write();
			string_append(parameters->dst, "> cannot set config (set ssid)\n");
			return(app_action_error);
		}

		if(!config_set_string("wlan.ap.passwd", string_to_cstr(&passwd), -1, -1))
		{
			config_abort_write();
			string_append(parameters->dst, "> cannot set config (passwd)\n");
			return(app_action_error);
		}

		if(!config_set_int("wlan.ap.channel", channel, -1, -1))
		{
			config_abort_write();
			string_append(parameters->dst, "> cannot set config (channel)\n");
			return(app_action_error);
		}

		if(!config_open_write())
		{
			string_append(parameters->dst, "> cannot set config (close)\n");
			return(app_action_error);
		}
	}

	string_clear(&ssid);
	string_clear(&passwd);

	if(!config_get_string("wlan.ap.ssid", &ssid, -1, -1))
	{
		string_clear(&ssid);
		string_append(&ssid, "<empty>");
	}

	if(!config_get_string("wlan.ap.passwd", &passwd, -1, -1))
	{
		string_clear(&passwd);
		string_append(&passwd, "<empty>");
	}

	if(!config_get_uint("wlan.ap.channel", &channel, -1, -1))
		channel = 0;

	string_format(parameters->dst, "> ssid: \"%s\", passwd: \"%s\", channel: %u\n",
			string_to_cstr(&ssid), string_to_cstr(&passwd), channel);

	return(app_action_normal);
}

app_action_t application_function_wlan_client_configure(app_params_t *parameters)
{
	string_new(, ssid, 64);
	string_new(, passwd, 64);
	struct station_config cconf;

	if((parse_string(1, parameters->src, &ssid, ' ') == parse_ok) && (parse_string(2, parameters->src, &passwd, ' ') == parse_ok))
	{
		if(string_length(&passwd) < 8)
		{
			string_format(parameters->dst, "> passwd \"%s\" too short (length must be >= 8)\n", string_to_cstr(&passwd));
			return(app_action_error);
		}

		if(!config_open_write())
		{
			string_append(parameters->dst, "> cannot set config (open)\n");
			return(app_action_error);
		}

		if(!config_set_string("wlan.client.ssid", string_to_cstr(&ssid), -1, -1))
		{
			config_abort_write();
			string_append(parameters->dst, "> cannot set config (write ssid)\n");
			return(app_action_error);
		}

		if(!config_set_string("wlan.client.passwd", string_to_cstr(&passwd), -1, -1))
		{
			config_abort_write();
			string_append(parameters->dst, "> cannot set config (write passwd)\n");
			return(app_action_error);
		}

		if(!config_close_write())
		{
			string_append(parameters->dst, "> cannot set config (close)\n");
			return(app_action_error);
		}

		wifi_station_disconnect();
		memset(&cconf, 0, sizeof(cconf));
		strecpy((char *)cconf.ssid, string_to_cstr(&ssid), sizeof(cconf.ssid));
		strecpy((char *)cconf.password, string_to_cstr(&passwd), sizeof(cconf.password));
		cconf.all_channel_scan = 1;
		wifi_station_set_config(&cconf);
		wifi_station_connect();
	}

	string_clear(&ssid);
	string_clear(&passwd);

	if(!config_get_string("wlan.client.ssid", &ssid, -1, -1))
	{
		string_clear(&ssid);
		string_append(&ssid, "<empty>");
	}

	if(!config_get_string("wlan.client.passwd", &passwd, -1, -1))
	{
		string_clear(&passwd);
		string_append(&passwd, "<empty>");
	}

	string_format(parameters->dst, "> ssid: \"%s\", passwd: \"%s\"\n",
			string_to_cstr(&ssid), string_to_cstr(&passwd));

	return(app_action_normal);
}

app_action_t application_function_wlan_mode(app_params_t *parameters)
{
	unsigned int int_mode;
	config_wlan_mode_t mode;

	if(parse_string(1, parameters->src, parameters->dst, ' ') == parse_ok)
	{
		if(string_match_cstr(parameters->dst, "client"))
		{
			string_clear(parameters->dst);

			if(!config_open_write() ||
					!config_set_int("wlan.mode", config_wlan_mode_client, -1, -1) ||
					!config_close_write())
			{
				config_abort_write();
				string_append(parameters->dst, "> cannot set config\n");
				return(app_action_error);
			}

			wlan_reconnect();

			return(app_action_disconnect);
		}

		if(string_match_cstr(parameters->dst, "ap"))
		{
			string_clear(parameters->dst);

			if(!config_open_write() ||
					!config_set_int("wlan.mode", config_wlan_mode_ap, -1, -1) ||
					!config_close_write())
			{
				config_abort_write();
				string_append(parameters->dst, "> cannot set config\n");
				return(app_action_error);
			}

			wlan_reconnect();

			return(app_action_disconnect);
		}

		string_append(parameters->dst, ": invalid wlan mode\n");
		return(app_action_error);
	}

	string_clear(parameters->dst);
	string_append(parameters->dst, "> current mode: ");

	if(config_get_uint("wlan.mode", &int_mode, -1, -1))
	{
		mode = (config_wlan_mode_t)int_mode;

		switch(mode)
		{
			case(config_wlan_mode_client):
			{
				string_append(parameters->dst, "client mode");
				break;
			}

			case(config_wlan_mode_ap):
			{
				string_append(parameters->dst, "ap mode");
				break;
			}

			default:
			{
				string_append(parameters->dst, "unknown mode");
				break;
			}
		}
	}
	else
		string_append(parameters->dst, "mode unset");

	string_append(parameters->dst, "\n");

	return(app_action_normal);
}

app_action_t application_function_wlan_reset(app_params_t *parameters)
{
	system_restore();
	system_restart();
	msleep(100);

	return(app_action_normal);
}
