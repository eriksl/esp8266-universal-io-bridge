#include "application-wlan.h"

#include "util.h"

#include <user_interface.h>
#include <c_types.h>

static const char *phy[] = {
	"unknown",
	"802.11b",
	"802.11g",
	"802.11n",
	"unknown"
};

static const char *slp[] =
{
	"none",
	"light",
	"modem",
	"unknown"
};

ICACHE_FLASH_ATTR uint8_t application_function_wlan_dump(application_parameters_t ap)
{
	struct station_config sc_default, sc_current;

	wifi_station_get_config_default(&sc_default);
	wifi_station_get_config(&sc_current);

	snprintf(ap.dst, ap.size,
			"> default ssid: %s, passwd: %s\n"
			"> current ssid: %s, passwd: %s\n"
			">\n"
			"> phy mode: %s\n"
			"> sleep mode: %s\n"
			">\n"
			"> channel: %u\n"
			"> signal strength: %d dB\n",
			sc_default.ssid, sc_default.password,
			sc_current.ssid, sc_current.password,
			phy[wifi_get_phy_mode()],
			slp[wifi_get_sleep_type()],
			wifi_get_channel(),
			wifi_station_get_rssi());

	return(1);
}

