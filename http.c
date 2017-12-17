#include "http.h"
#include "util.h"
#include "config.h"
#include "io.h"
#include "stats.h"
#include "i2c_sensor.h"

#include <sntp.h>

typedef struct
{
	const char *description;
	const char *action;
	app_action_t (*handler)(const string_t *src, string_t *dst);
} http_handler_t;

static const http_handler_t handlers[];

roflash static const char http_header_pre[] =
{
	"HTTP/1.0 "
};

roflash static const char http_eol[] =
{
	"\r\n"
};

roflash static const char http_header_ok[] =
{
	"200 OK\r\n"
	"Content-Type: text/html; charset=UTF-8\r\n"
	"Content-Length: @@@@\r\n"
	"Connection: close\r\n"
	"\r\n"
};

roflash static const char http_header_error[] =
{
	"Content-Type: text/html; charset=UTF-8\r\n"
	"Content-Length: %d\r\n"
	"Connection: close\r\n"
	"\r\n"
};

roflash static const char html_header[] =
{
	"<!DOCTYPE html>\n"
	"<html>\n"
	"	<head>\n"
	"		<title>Universal I/O bridge</title>\n"
	"		<style>\n"
	"			.range {\n"
	"				transform: scale(8,6);\n"
	"				transform-origin: left top;\n"
	"				width: 12.2%;\n"
	"				margin: 0px 0px 110px 0px;\n"
	"				border: solid 0px white;\n"
	"			}\n"
	"			.form\n"
	"			{\n"
	"				border: solid 4px blue;\n"
	"				margin: 0px 0px 20px 0px;\n"
	"			}\n"
	"			.div {\n"
	"				font-size: 31pt;\n"
	"			}\n"
	"		</style>\n"
	"	</head>\n"
	"	<body>\n"
};

roflash static const char html_p1[] =
{
	"<p>"
};

roflash static const char html_p2[] =
{
	"</p>"
};

roflash static const char html_eol[] =
{
	"\n"
};

roflash static const char html_link_home[] =
{
	"<p>\n"
	"	<a href=\"/\">Home</a>\n"
	"</p>\n"
};

roflash static const char html_footer[] =
{
	"	</body>\n"
	"</html>\n"
};

roflash static const char html_table_start[] =
{
	"<table cellpadding=\"1\" cellspacing=\"1\" border=\"1\">\n"
};

roflash static const char html_table_end[] =
{
	"</table>\n"
};

irom static void http_range_form(string_t *dst, int io, int pin, int low, int high, int step, int current)
{
	string_new(, id, 32);
	int pwm_period;

	string_format(&id, "range_%d_%d", io, pin);

	if(!config_get_int("pwm.period", -1, -1, &pwm_period))
		pwm_period = 65536;

	string_format(dst,	"<form id=\"form_%s\" class=\"form\" method=\"get\" action=\"%s\">\n", string_to_const_ptr(&id), "set");
	string_cat(dst,		"	<div class=\"div\">\n");
	string_format(dst,	"		%d/%d range: %d-%d/%d current: %d\n", io, pin, low, high, step, current);
	string_cat(dst,		"	</div>\n");
	string_format(dst,	"	<input name=\"io\" type=\"hidden\" value=\"%d\" />\n", io);
	string_format(dst,	"	<input name=\"pin\" type=\"hidden\" value=\"%d\" />\n", pin);
	string_format(dst,	"	<input name=\"value\" type=\"range\" class=\"range\" min=\"%d\" max=\"%d\" value=\"%d\" onchange=\"changed_%s(this.value);\" />\n", 0, pwm_period, current, string_to_const_ptr(&id));
	string_cat(dst,		"	<script type=\"text/javascript\">\n");
	string_format(dst,	"	function changed_%s(value)\n", string_to_const_ptr(&id));
	string_cat(dst,		"	{\n");
	string_format(dst,	"		document.getElementById(\"form_%s\").submit();\n", string_to_const_ptr(&id));
	string_cat(dst,		"	}\n");
	string_cat(dst,		"	</script>\n");
	string_cat(dst,		"</form>\n");
}

irom static app_action_t http_error(string_t *dst, const char *error_string, const char *info)
{
	static const char delim[] = ": ";
	int content_length = sizeof(html_header) - 1 + sizeof(html_p1) - 1 +
			strlen(error_string) +
			sizeof(html_p2) - 1 + sizeof(html_eol) - 1 + sizeof(html_footer) - 1;

	if(info)
		content_length += sizeof(delim) - 1 + strlen(info) + sizeof(html_eol) - 1;

	string_cat_ptr(dst, http_header_pre);
	string_cat_strptr(dst, error_string);
	string_cat_ptr(dst, http_eol);
	string_format_ptr(dst, http_header_error, content_length);

	string_cat_ptr(dst, html_header);
	string_cat_ptr(dst, html_p1);
	string_cat_strptr(dst, error_string);

	if(info)
	{
		string_cat_strptr(dst, delim);
		string_cat_strptr(dst, info);
		string_cat_ptr(dst, html_eol);
	}

	string_cat_ptr(dst, html_eol);
	string_cat_ptr(dst, html_p2);
	string_cat_ptr(dst, html_footer);

	return(app_action_error);
}

irom app_action_t application_function_http_get(const string_t *src, string_t *dst)
{
	string_new(, url, 64);
	string_new(, afterslash, 64);
	string_new(, action, 64);
	int ix, length;
	const http_handler_t *handler;
	app_action_t error;

	if((parse_string(1, src, &url, ' ')) != parse_ok)
		return(http_error(dst, "400 Bad Request 1", "no url"));

	if(string_index(&url, 0) != '/')
		return(http_error(dst, "400 Bad Request 2", string_to_const_ptr(&url)));

	if(!string_match(&url, "/") && (parse_string(1, &url, &afterslash, '/') != parse_ok))
		return(http_error(dst, "400 Bad Request 3", string_to_const_ptr(&afterslash)));

	if((parse_string(0, &afterslash, &action, '?')) != parse_ok)
	{
		string_clear(&action);
		string_copy_string(&action, &afterslash);
	}

	for(handler = &handlers[0]; handler->action && handler->handler; handler++)
		if(string_match(&action, handler->action))
			break;

	if(!handler->action || !handler->handler)
		return(http_error(dst, "404 Not Found", string_to_const_ptr(&action)));

	string_clear(dst);
	string_cat_ptr(dst, http_header_pre);
	string_cat_ptr(dst, http_header_ok);
	string_cat_ptr(dst, html_header);

	error = handler->handler(&afterslash, dst);

	string_cat_ptr(dst, html_link_home);
	string_cat_ptr(dst, html_footer);

	if((length = string_length(dst) - (sizeof(http_header_pre) - 1) - (sizeof(http_header_ok) - 1)) <= 0)
		return(http_error(dst, "500 Internal Server Error", 0));

	if((ix = string_find(dst, 0, '@')) <= 0)
		return(http_error(dst, "501 Not Implemented", 0));

	string_replace(dst, ix + 0, (length / 1000) + '0');
	length %= 1000;
	string_replace(dst, ix + 1, (length / 100) + '0');
	length %= 100;
	string_replace(dst, ix + 2, (length / 10) + '0');
	length %= 10;
	string_replace(dst, ix + 3, (length / 1) + '0');

	return(error);
}

irom static app_action_t handler_root(const string_t *src, string_t *dst)
{
	const http_handler_t *handler;

	string_cat_ptr(dst, html_table_start);
	string_cat(dst, "<tr><th colspan=\"2\">ESP8266 Universal I/O bridge</th></tr>\n");

	for(handler = &handlers[0]; handler->action && handler->handler; handler++)
		if(handler->description)
			string_format(dst, "<tr><td>%s</td><td><a href=\"/%s\">/%s</a></td></tr>\n", handler->description, handler->action, handler->action);

	string_cat_ptr(dst, html_table_end);

	return(app_action_http_ok);
}

irom static app_action_t handler_controls(const string_t *src, string_t *dst)
{
	int				io, pin;
	int				low, high, step, current;
	io_pin_mode_t	mode;

	for(io = 0; io < io_id_size; io++)
		for(pin = 0; pin < max_pins_per_io; pin++)
			if(io_traits(0, io, pin, &mode, &low, &high, &step, &current) == io_ok)
				if(high > 0)
					http_range_form(dst, io, pin, low, high, step, current);

	return(app_action_http_ok);
}

irom static app_action_t handler_set(const string_t *src, string_t *dst)
{
	string_new(, getparam, 32);
	string_new(, param1, 16);
	string_new(, param2, 16);
	string_new(, param3, 16);

	int io, pin, value;
	io_error_t error;

	if(parse_string(1, src, &getparam, '?') != parse_ok)
		goto error;

	if(parse_string(0, &getparam, &param1, '&') != parse_ok)
		goto error;

	if(parse_string(1, &getparam, &param2, '&') != parse_ok)
		goto error;

	if(parse_string(2, &getparam, &param3, '&') != parse_ok)
		goto error;

	if(!string_nmatch(&param1, "io=", 3))
		goto error;

	if(!string_nmatch(&param2, "pin=", 4))
		goto error;

	if(!string_nmatch(&param3, "value=", 6))
		goto error;

	if(parse_int(1, &param1, &io, 10, '=') != parse_ok)
		goto error;

	if(parse_int(1, &param2, &pin, 10, '=') != parse_ok)
		goto error;

	if(parse_int(1, &param3, &value, 10, '=') != parse_ok)
		goto error;

	error = io_write_pin(dst, io, pin, value);

	if(error == io_ok)
		string_cat(dst, "<script>location.replace(\"/controls\");</script>\n");
	else
	{
		string_format(dst, "<tr><td>%s: io=%d pin=%d value=%d</td></tr>\n<tr><td>",
				string_to_const_ptr(&getparam), io, pin, value);
		string_cat(dst, "</td></tr>\n");
	}

	return(app_action_http_ok);

error:
	string_cat_ptr(dst, html_table_start);
	string_cat(dst, "<tr><th>parameter error</h1></th></tr>\n");
	string_cat_ptr(dst, html_table_end);
	return(app_action_http_ok);
}

irom static app_action_t handler_favicon(const string_t *src, string_t *dst)
{
	return(app_action_http_ok);
}

irom static app_action_t handler_info_fw(const string_t *src, string_t *dst)
{
	string_cat_ptr(dst, html_table_start);
	string_cat(dst, "<tr><td><pre>");
	stats_firmware(dst);
	string_cat(dst, "</pre></td></tr>");
	string_cat_ptr(dst, html_table_end);

	return(app_action_http_ok);
}

irom static app_action_t handler_info_i2c(const string_t *src, string_t *dst)
{
	string_cat_ptr(dst, html_table_start);
	string_cat(dst, "<tr><td><pre>");
	stats_i2c(dst);
	string_cat(dst, "</pre></td></tr>");
	string_cat_ptr(dst, html_table_end);

	return(app_action_http_ok);
}

irom static app_action_t handler_info_time(const string_t *src, string_t *dst)
{
	string_cat_ptr(dst, html_table_start);
	string_cat(dst, "<tr><td><pre>");
	stats_time(dst);
	string_cat(dst, "</pre></td></tr>");
	string_cat_ptr(dst, html_table_end);

	return(app_action_http_ok);
}

irom static app_action_t handler_info_stats(const string_t *src, string_t *dst)
{
	string_cat_ptr(dst, html_table_start);
	string_cat(dst, "<tr><td><pre>");
	stats_counters(dst);
	string_cat(dst, "</pre></td></tr>");
	string_cat_ptr(dst, html_table_end);

	return(app_action_http_ok);
}

irom static app_action_t handler_info_wlan(const string_t *src, string_t *dst)
{
	string_cat_ptr(dst, html_table_start);
	string_cat(dst, "<tr><td><pre>");
	stats_wlan(dst);
	string_cat(dst, "</pre></td></tr>");
	string_cat_ptr(dst, html_table_end);

	return(app_action_http_ok);
}

irom static app_action_t handler_io(const string_t *src, string_t *dst)
{
	io_config_dump(dst, -1, -1, true);

	return(app_action_http_ok);
}

irom static app_action_t handler_sensors(const string_t *src, string_t *dst)
{
	i2c_sensor_t sensor;
	int bus;
	int detected = 0;

	string_cat_ptr(dst, html_table_start);
	string_cat(dst, "<tr><th>bus</th><th>sensor</th><th>address</th><th>name</th><th>type</th><th>value</th></tr>\n");

	for(bus = 0; bus < i2c_busses; bus++)
		for(sensor = 0; sensor < i2c_sensor_size; sensor++)
			if(i2c_sensor_detected(bus, sensor))
			{
				string_cat(dst, "<tr><td>");
				i2c_sensor_read(dst, bus, sensor, false, true);
				string_cat(dst, "</td></tr>\n");
				detected++;
			}

	if(detected < 1)
		string_cat(dst, "<tr><td colspan=\"6\">no sensors detected</td></tr>\n");

	string_cat_ptr(dst, html_table_end);

	return(app_action_http_ok);
}

irom static app_action_t handler_resetwlanscreen(const string_t *src, string_t *dst)
{
	string_cat(dst, "<p>Reset WLAN configuration.</p>\n");
	string_cat(dst, "<p>Type the SSID (network name) and password and click \"set\".</p>\n");
	string_cat(dst, "<form action=\"/resetwlan\" method=\"get\">\n");
	string_cat(dst, "	<input type=\"text\" name=\"ssid\">\n");
	string_cat(dst, "	<input type=\"text\" name=\"password\">\n");
	string_cat(dst, "	<input type=\"submit\" value=\"set\">\n");
	string_cat(dst, "</form>\n");

	return(app_action_http_ok);
}

irom static app_action_t handler_resetwlan(const string_t *src, string_t *dst)
{
	string_new(, getparam, 64);
	string_new(, param1, 32);
	string_new(, param2, 32);
	string_new(, ssid, 32);
	string_new(, passwd, 32);

	if(parse_string(1, src, &getparam, '?') != parse_ok)
		goto parameter_error;

	if(parse_string(0, &getparam, &param1, '&') != parse_ok)
		goto parameter_error;

	if(parse_string(1, &getparam, &param2, '&') != parse_ok)
		goto parameter_error;

	if(!string_nmatch(&param1, "ssid=", 5))
		goto parameter_error;

	if(!string_nmatch(&param2, "password=", 9))
		goto parameter_error;

	if(parse_string(1, &param1, &ssid, '=') != parse_ok)
		goto parameter_error;

	if(parse_string(1, &param2, &passwd, '=') != parse_ok)
		goto parameter_error;

	if((string_length(&ssid) < 4) || (string_length(&passwd) < 8))
		goto parameter_error;

	if(!config_set_string("wlan.client.ssid", -1, -1, &ssid, -1, -1))
		goto config_error;

	if(!config_set_string("wlan.client.passwd", -1, -1, &passwd, -1, -1))
		goto config_error;

	if(!config_set_int("wlan.mode", -1, -1, config_wlan_mode_client))
		goto config_error;

	if(config_write() == 0)
		goto config_error;

	string_cat_ptr(dst, html_table_start);
	string_cat(dst,		"<p>SSID and password set.</p>\n");
	string_cat(dst,		"<tr><th>SSID</th><th>password</th></tr>\n");
	string_format(dst,	"<tr><td>%s</td><td>%s</td></tr>\n", string_to_const_ptr(&ssid), string_to_const_ptr(&passwd));
	string_cat_ptr(dst, html_table_end);
	string_cat(dst,		"<p>Now <a href=\"/reset\">reset</a> to activate WLAN settings.</p>\n");

	return(app_action_http_ok);

parameter_error:
	string_cat(dst, "<h1>Parameter error</h1>\n");
	return(app_action_http_ok);

config_error:
	string_cat(dst, "<h1>Can't write config</h1>\n");
	return(app_action_http_ok);
}

irom static app_action_t handler_reset(const string_t *src, string_t *dst)
{
	return(app_action_reset);
}

static const http_handler_t handlers[] =
{
	{
		"Home",
		"",
		handler_root
	},
	{
		"Information about the firmware",
		"info_fw",
		handler_info_fw
	},
	{
		"Information about the i2c bus",
		"info_i2c",
		handler_info_i2c
	},
	{
		"Information about time keeping",
		"info_time",
		handler_info_time
	},
	{
		"Information about WLAN",
		"info_wlan",
		handler_info_wlan
	},
	{
		"Statistics",
		"info_stats",
		handler_info_stats
	},
	{
		"List all I/O's",
		"io",
		handler_io
	},
	{
		"Control outputs",
		"controls",
		handler_controls
	},
	{
		"List all sensors",
		"sensors",
		handler_sensors,
	},
	{
		"Set an I/O",
		"set",
		handler_set
	},
	{
		"Reset WLAN configuration",
		"resetwlanscreen",
		handler_resetwlanscreen,
	},
	{
		(const char *)0,
		"resetwlan",
		handler_resetwlan
	},
	{
		"Reset",
		"reset",
		handler_reset
	},
	{
		(const char *)0,
		"favicon.ico",
		handler_favicon
	},
	{
		(const char *)0,
		(const char *)0,
		(app_action_t (*)(const string_t *, string_t *))0
	}
};
