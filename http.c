#include "http.h"
#include "util.h"
#include "config.h"
#include "io.h"
#include "stats.h"

#include <sntp.h>

typedef struct
{
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
	"Connection: close\r\n"
	"\r\n"
};

roflash static const char html_header[] =
{
	"<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Strict//EN\" \"http://www.w3c.org/TR/xhtml1/DTD/xhtml1-strict.dtd\">\n"
	"<html>\n"
	"<head>\n"
	"<meta http-equiv=\"Content-type\" content=\"text/html; charset=UTF-8\"/>\n"
	"<title>Universal I/O bridge</title>\n"
	"</head>\n"
	"<body>\n"
};

roflash static const char html_footer[] =
{
	"</body>\n"
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

irom static app_action_t http_error(string_t *dst, const char *error_string, const char *info)
{
	string_cat_ptr(dst, http_header_pre);
	string_cat_strptr(dst, error_string);
	string_cat_ptr(dst, http_eol);
	string_cat_ptr(dst, http_header_error);
	string_cat_ptr(dst, html_header);
	string_cat_strptr(dst, error_string);

	if(info)
	{
		string_cat(dst, ": ");
		string_cat_strptr(dst, info);
	}

	string_cat_ptr(dst, "\n");
	string_cat_ptr(dst, html_footer);

	return(app_action_error);
}

irom app_action_t application_function_http_get(const string_t *src, string_t *dst)
{
	string_new(static, location, 32);
	int ix, length;
	const http_handler_t *handler;
	app_action_t action;

	string_clear(&location);

	if((parse_string(1, src, &location)) != parse_ok)
		return(http_error(dst, "400 Bad Request", 0));

	for(handler = &handlers[0]; handler->location && handler->handler; handler++)
		if(string_match(&location, handler->location))
			break;

	if(!handler->location || !handler->handler)
		return(http_error(dst, "404 Not Found", string_to_const_ptr(&location)));

	string_clear(dst);
	string_cat_ptr(dst, http_header_pre);
	string_cat_ptr(dst, http_header_ok);
	string_cat_ptr(dst, html_header);

	if((action = handler->handler(&location, dst)) == app_action_http_ok)
	{
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

		action = app_action_normal;
	}

	return(action);
}

irom static app_action_t handler_root(const string_t *src, string_t *dst)
{
	string_cat_ptr(dst, html_table_start);
	string_cat(dst, "<tr><th colspan=\"1\">ESP8266 Universal I/O bridge</th></tr>\n");
	string_cat(dst, "<tr><td><a href=\"info_fw\">Information about the firmware</a></td></tr>\n");
	string_cat(dst, "<tr><td><a href=\"info_i2c\">Information about i2c</a></td></tr>\n");
	string_cat(dst, "<tr><td><a href=\"info_time\">Information about time keeping</a></td></tr>\n");
	string_cat(dst, "<tr><td><a href=\"info_wlan\">Information about wlan</a></td></tr>\n");
	string_cat(dst, "<tr><td><a href=\"info_stats\">Statistics</a></td></tr>\n");
	string_cat(dst, "<tr><td><a href=\"io\">List all I/O's</a></td></tr>\n");
	string_cat_ptr(dst, html_table_end);

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

static const http_handler_t handlers[] =
{
	{
		"/",
		handler_root
	},
	{
		"/info_fw",
		handler_info_fw
	},
	{
		"/info_i2c",
		handler_info_i2c
	},
	{
		"/info_stats",
		handler_info_stats
	},
	{
		"/info_time",
		handler_info_time
	},
	{
		"/info_wlan",
		handler_info_wlan
	},
	{
		"/io",
		handler_io
	},
	{
		(const char *)0,
		(app_action_t (*)(const string_t *, string_t *))0
	}
};
