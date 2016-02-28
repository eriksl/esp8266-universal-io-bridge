#include "http.h"
#include "util.h"
#include "config.h"
#include "io.h"

#include <sntp.h>

static const http_handler_t handlers[];

roflash static const char http_header_pre[] =
{
	"HTTP/1.0 "
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
	"Content-Length: 0\r\n"
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

irom app_action_t application_function_http_get(const string_t *src, string_t *dst)
{
	string_new(static, location, 32);
	int ix, length;
	const http_handler_t *handler;
	app_action_t action;

	string_clear(&location);

	if((parse_string(1, src, &location)) != parse_ok)
	{
		string_cat_ptr(dst, http_header_pre);
		string_cat(dst, "400 Bad Request\r\n");
		string_cat_ptr(dst, http_header_error);
		return(app_action_error);
	}

	for(handler = &handlers[0]; handler->location && handler->handler; handler++)
		if(string_match(&location, handler->location))
			break;

	if(!handler->location || !handler->handler)
	{
		string_cat_ptr(dst, http_header_pre);
		string_format(dst, "404 Not Found: %s\r\n", string_to_const_ptr(&location));
		string_cat_ptr(dst, http_header_error);
		return(app_action_error);
	}

	string_clear(dst);
	string_cat_ptr(dst, http_header_pre);
	string_cat_ptr(dst, http_header_ok);
	string_cat_ptr(dst, html_header);

	if((action = handler->handler(&location, dst)) == app_action_http_ok)
	{
		string_cat_ptr(dst, html_footer);

		if((length = string_length(dst) - (sizeof(http_header_pre) - 1) - (sizeof(http_header_ok) - 1)) <= 0)
		{
			string_cat_ptr(dst, http_header_pre);
			string_copy(dst, "500 Internal Server Error\r\n");
			string_cat_ptr(dst, http_header_error);
			return(app_action_error);
		}

		if((ix = string_find(dst, 0, '@')) <= 0)
		{
			string_cat_ptr(dst, http_header_pre);
			string_copy(dst, "501 Not Implemented\r\n");
			string_cat_ptr(dst, http_header_error);
			return(app_action_error);
		}

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

irom static app_action_t root_handler(const string_t *src, string_t *dst)
{
	string_cat(dst, "<p>\n");
	string_cat_strptr(dst, sntp_get_real_time(sntp_get_current_timestamp()));
	string_cat(dst, "</p>\n");
	io_config_dump(dst, &config, -1, -1, true);

	return(app_action_http_ok);
}

static const http_handler_t handlers[] =
{
	{
		"/",
		root_handler
	},
	{
		(const char *)0,
		(app_action_t (*)(const string_t *, string_t *))0
	}
};
