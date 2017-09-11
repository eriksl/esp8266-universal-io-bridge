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
	"Content-Length: %d\r\n"
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
	string_new(auto, url, 32);
	string_new(auto, afterslash, 32);
	string_new(auto, action, 32);
	int ix, length;
	const http_handler_t *handler;
	app_action_t error;

	string_clear(&url);
	string_clear(&afterslash);

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

	if((error = handler->handler(&afterslash, dst)) == app_action_http_ok)
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

		error = app_action_normal;
	}

	return(error);
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

static const http_handler_t handlers[] =
{
	{
		"",
		handler_root
	},
	{
		"favicon.ico",
		handler_favicon
	},
	{
		"info_fw",
		handler_info_fw
	},
	{
		"info_i2c",
		handler_info_i2c
	},
	{
		"info_stats",
		handler_info_stats
	},
	{
		"info_time",
		handler_info_time
	},
	{
		"info_wlan",
		handler_info_wlan
	},
	{
		"io",
		handler_io
	},
	{
		(const char *)0,
		(app_action_t (*)(const string_t *, string_t *))0
	}
};
