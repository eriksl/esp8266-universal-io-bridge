#include "wlan.h"

#include "util.h"
#include "config.h"
#include "stats.h"
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

typedef struct
{
	mac_addr_t mac;
	int channel;
	int rssi;
} ap_cache_t;

assert_size(ap_cache_t, 16);

enum
{
	ap_cache_entries = 5,
};

typedef struct
{
	int entries;
	char ssid[32];
	ap_cache_t ap[ap_cache_entries];
} ap_caches_t;

static ap_caches_t ap_cache;

assert_size(ap_caches_t, 116);

roflash const char help_description_stats_wlan[] =			"statistics from the wlan subsystem";
roflash const char help_description_wlan_ap_config[] =		"configure access point mode wlan params, supply ssid, passwd and channel";
roflash const char help_description_wlan_client_config[] =	"configure client mode wlan params, supply ssid and passwd";
roflash const char help_description_wlan_mode[] =			"set wlan mode: client or ap";
roflash const char help_description_wlan_scan[] =			"scan wlan, see log to retrieve the results";
roflash const char help_description_wlan_ap_switch[] =		"switch client to new access point, supply BSSID";

void wlan_init(config_wlan_mode_t wlan_mode, const string_t *ssid, const string_t *password, unsigned int channel)
{
	struct station_config cconf;
	struct softap_config saconf;

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

static bool wlan_ap_switch(const mac_addr_t mac, int channel)
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
	cconf.channel = channel;
	cconf.all_channel_scan = channel == 0;
	cconf.threshold.rssi = -70;
	cconf.threshold.authmode = AUTH_OPEN;

	wifi_station_disconnect();
	wifi_set_opmode(STATION_MODE);
	wifi_station_set_config(&cconf);

	if(!wifi_station_connect())
	{
		log("wlan_channel_switch: set config current failed\n");
		return(false);
	}

	return(true);
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
					log("join mc group failed\n");
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

	string_format(dst, "> ssid flash: \"%s\", passwd: \"%s\"\n",
			config.ssid, config.password);

	wifi_station_get_config(&config);
	wc.cc[2] = '\0';

	string_format(dst, "> current ssid: \"%s\", passwd: \"%s\"\n",
			config.ssid, config.password);

	string_format(dst,
			"> channel: %u\n"
			"> autoconnect: %s\n"
			"> phy mode: %s\n"
			"> sleep mode: %s\n"
			"> max sleep level: %s\n"
			"> listen interval: %d\n"
			"> signal strength: %d dB\n"
			"> country: %s [%d - %d]\n"
			">\n",
				wifi_get_channel(),
				onoff(wifi_station_get_auto_connect()),
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
	string_append(dst, "\n\n");

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
}

app_action_t application_function_stats_wlan(app_params_t *parameters)
{
	stats_wlan(parameters->dst);
	return(app_action_normal);
}

static void wlan_scan_done_callback(void *arg, STATUS status)
{
	roflash static const char status_msg[][16] =
	{
		"OK",
		"FAIL",
		"PENDING",
		"BUSY",
		"CANCEL"
	};

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
	int ix;

	flash_to_dram(true, status <= CANCEL ? status_msg[status] : "<invalid>", status_string, sizeof(status_string));

	logbuffer_clear(); // make sure as much room is available as is possible

	log("wlan scan result: %s\n", status_string);
	log_from_flash_n(fmt_string_1, "SSID", "CHN", "RSSI", "AUTH", "PAIR", "GROUP", "OFFS", "BSSID");

	ap_cache.entries = 0;

	for(bss = arg; bss; bss = bss->next.stqe_next)
	{
		if(strcmp(ap_cache.ssid, "") && !strncmp((const char *)bss->ssid, ap_cache.ssid, sizeof(ap_cache.ssid)))
		{
			selected = true;

			ap_cache.ap[ap_cache.entries].mac[0] = bss->bssid[0];
			ap_cache.ap[ap_cache.entries].mac[1] = bss->bssid[1];
			ap_cache.ap[ap_cache.entries].mac[2] = bss->bssid[2];
			ap_cache.ap[ap_cache.entries].mac[3] = bss->bssid[3];
			ap_cache.ap[ap_cache.entries].mac[4] = bss->bssid[4];
			ap_cache.ap[ap_cache.entries].mac[5] = bss->bssid[5];
			ap_cache.ap[ap_cache.entries].rssi = bss->rssi;
			ap_cache.ap[ap_cache.entries].channel = bss->channel;

			ap_cache.entries++;
		}
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

	log("> access point cache\n");

	for(ix = 0; ix < ap_cache.entries; ix++)
	{
		string_new(, line, 64);

		string_format(&line, "> ch: %2d, rssi: %3d, bssid: %02x:%02x:%02x:%02x:%02x:%02x",
				ap_cache.ap[ix].channel,
				ap_cache.ap[ix].rssi,
				ap_cache.ap[ix].mac[0],
				ap_cache.ap[ix].mac[1],
				ap_cache.ap[ix].mac[2],
				ap_cache.ap[ix].mac[3],
				ap_cache.ap[ix].mac[4],
				ap_cache.ap[ix].mac[5]);

		log("%s\n", string_to_cstr(&line));
	}
}

app_action_t application_function_wlan_scan(app_params_t *parameters)
{
	int ix;

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

	strcpy(ap_cache.ssid, "");

	if(config_get_string("wlan.client.ssid", &ssid, -1, -1))
		strlcpy(ap_cache.ssid, string_to_cstr(&ssid), sizeof(ap_cache.ssid));

	string_clear(&ssid);

	if(parse_string(1, parameters->src, &ssid, ' ') == parse_ok)
	{
		if(string_match_cstr(&ssid, "-"))
			sc.ssid = ap_cache.ssid;
		else
		{
			string_to_cstr(&ssid); // ensure \0
			sc.ssid = string_buffer_nonconst(&ssid);
		}
	}

	ap_cache.entries = 0;

	for(ix = 0; ix < ap_cache_entries; ix++)
	{
		ap_cache.ap[ix].mac[0] = 0xff;
		ap_cache.ap[ix].mac[1] = 0xff;
		ap_cache.ap[ix].mac[2] = 0xff;
		ap_cache.ap[ix].mac[3] = 0xff;
		ap_cache.ap[ix].mac[4] = 0xff;
		ap_cache.ap[ix].mac[5] = 0xff;
		ap_cache.ap[ix].rssi = 0;
		ap_cache.ap[ix].channel = 0;
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

app_action_t application_function_wlan_ap_switch(app_params_t *parameters)
{
	string_new(, mac_string, 32);
	int max, ix;
	const ap_cache_t *max_entry;

	if(ap_cache.entries == 0)
	{
		string_format(parameters->dst, "ap cache empty, run wireless scan first\n");
		return(app_action_error);
	}

	max = 0;
	max_entry = &ap_cache.ap[0];

	for(ix = 0; ix < ap_cache.entries; ix++)
	{
		if(ap_cache.ap[ix].rssi > max_entry->rssi)
		{
			max = ix;
			max_entry = &ap_cache.ap[ix];
		}
	}

	mac_to_string(&mac_string, max_entry->mac);

	string_format(parameters->dst, "switch to ap with highest rssi %d, channel %d in entry %d\n", max_entry->rssi, max_entry->channel, max);

	if(!wlan_ap_switch(max_entry->mac, max_entry->channel))
	{
		string_format(parameters->dst, "> wlan-ap-switch to %s[%d] failed\n", string_to_cstr(&mac_string), max_entry->channel);
		return(app_action_error);
	}

	string_format(parameters->dst, "> wlan-ap-switch to %s[%d] OK\n", string_to_cstr(&mac_string), max_entry->channel);

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

			wlan_init_from_config();

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

			wlan_init_from_config();

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

