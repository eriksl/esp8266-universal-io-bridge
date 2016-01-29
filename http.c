#include "http.h"
#include "util.h"
#include "gpios.h"
#include "config.h"

#include <string.h>

irom static http_action_t root_handler(const string_t *src, string_t *dst);

static const http_handler_t handlers[] =
{
	{
		"/",
		root_handler
	},
};

irom http_action_t root_handler(const string_t *src, string_t *dst)
{
	gpios_dump_html(dst, &config.gpios);

	return(http_action_normal);
}

irom http_action_t http_process_request(const string_t *src, string_t *dst)
{
	char current;
	string_new(static, location, 32);
	int ix;
	const http_handler_t *handler;
	http_action_t action;

	if((string_length(src) < 5) || !string_nmatch(src, "GET /", 5))
	{
		string_cat(dst, "400 Bad request\r\n");
		return(http_action_error);
	}

	for(ix = 4; ix < string_length(src); ix++)
	{
		current = string_index(src, ix);

		if(current > ' ')
			string_append(&location, current);
		else
			break;
	}

	for(ix = 0; ix < (int)((sizeof(handlers) / sizeof(*handlers))); ix++)
	{
		handler = &handlers[ix];

		if(string_match(&location, handler->location))
			break;
	}

	if(ix >= (int)(sizeof(handlers) / sizeof(*handlers)))
	{
		string_format(dst, "404 Not found: %s\r\n", string_to_ptr(&location));
		return(http_action_error);
	}

	string_cat(dst,
		"<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Strict//EN\" \"http://www.w3c.org/TR/xhtml1/DTD/xhtml1-strict.dtd\">\r\n"
		"<html>\r\n"
		"<head>\r\n"
		"<meta http-equiv=\"Content-type\" content=\"text/html; charset=UTF-8\"/>\r\n"
		"<title>Universal I/O bridge</title>\r\n"
		"</head>\r\n"
		"<body>\r\n");

	action = handler->handler(dst, &location);

	string_cat(dst, 
		"</body>\r\n"
		"</html>\r\n");

	return(action);
}
