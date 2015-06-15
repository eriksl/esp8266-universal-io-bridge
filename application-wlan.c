#include "application-wlan.h"

#include "util.h"

#include <user_interface.h>
#include <c_types.h>

ICACHE_FLASH_ATTR uint8_t application_function_wlan_dump(application_parameters_t ap)
{
	static const char *phy[] = { "", "802.11b", "802.11g", "802.11n", "" };
	static const char *slp[] = { "none", "light", "modem", "" };

	struct station_config sc;
	char tmp[128];

	wifi_station_get_config_default(&sc);
	snprintf(tmp, sizeof(tmp), "> default: ssid: %s, passwd: %s\n", sc.ssid, sc.password);
	strlcpy(ap.dst, tmp, ap.size);

	wifi_station_get_config(&sc);
	snprintf(tmp, sizeof(tmp), "> current: ssid: %s, passwd: %s\n", sc.ssid, sc.password);
	strlcat(ap.dst, tmp, ap.size);

	snprintf(tmp, sizeof(tmp), "> channel: %d\n", wifi_get_channel());
	strlcat(ap.dst, tmp, ap.size);

	snprintf(tmp, sizeof(tmp), "> phy mode: %s\n", phy[wifi_get_phy_mode()]);
	strlcat(ap.dst, tmp, ap.size);

	snprintf(tmp, sizeof(tmp), "> sleep mode: %s\n", slp[wifi_get_sleep_type()]);
	strlcat(ap.dst, tmp, ap.size);

	return(1);
}

