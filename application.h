#ifndef application_h
#define application_h

#include "util.h"
#include "config.h"

#include <stdint.h>

typedef enum
{
	app_action_normal,
	app_action_error,
	app_action_empty,
	app_action_disconnect,
	app_action_reset,
#if IMAGE_OTA == 1
	app_action_ota_commit,
#endif
	app_action_http_ok,
} app_action_t;

_Static_assert(sizeof(app_action_t) == 4, "sizeof(app_action_t) != 4");

app_action_t application_content(const string_t *src, string_t *dst);

bool wlan_scan_active(void);

#endif
