#ifndef _wlan_h_
#define _wlan_h_

#include "util.h"
#include "dispatch.h"

#include <stdbool.h>

typedef enum
{
	config_wlan_mode_client,
	config_wlan_mode_ap
} config_wlan_mode_t;

extern roflash const char help_description_stats_wlan[];
extern roflash const char help_description_wlan_ap_config[];
extern roflash const char help_description_wlan_client_config[];
extern roflash const char help_description_wlan_mode[];
extern roflash const char help_description_wlan_scan[];

void wlan_init(void);
bool wlan_start(void);
void wlan_start_recovery(void);
void wlan_multicast_init_groups(void);
void stats_wlan(string_t *dst);

app_action_t application_function_wlan_scan(app_params_t *parameters);
app_action_t application_function_wlan_ap_configure(app_params_t *parameters);
app_action_t application_function_wlan_client_configure(app_params_t *parameters);
app_action_t application_function_stats_wlan(app_params_t *parameters);
app_action_t application_function_wlan_mode(app_params_t *parameters);
app_action_t application_function_wlan_reset(app_params_t *parameters);

#endif
