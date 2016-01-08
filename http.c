#include "http.h"
#include "util.h"

#include <string.h>

static char location_buffer[64];

irom static http_action_t root_handler(const char *, http_buffer_t *);

static const http_handler_t handlers[] =
{
	{
		"/",
		root_handler
	},
};

static const char *html_header =
	"<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Strict//EN\" \"http://www.w3c.org/TR/xhtml1/DTD/xhtml1-strict.dtd\">\r\n"
	"<html>\r\n"
	"<head>\r\n"
	"<meta http-equiv=\"Content-type\" content=\"text/html; charset=UTF-8\"/>\r\n"
	"<title>Universal I/O bridge</title>\r\n"
    "</head>\r\n"
    "<body>\r\n";

static const char *html_footer =
    "</body>\r\n"
    "</html>\r\n";

irom http_action_t root_handler(const char *location, http_buffer_t *reply)
{
	unsigned int length;

	length = snprintf(reply->string, reply->size, "<h1>test: \"%s\"</h1>\r\n", location);
	reply->string += length;
	reply->size -= length;
	reply->length += length;

	return(http_action_normal);
}

irom http_action_t http_process_request(http_buffer_t request, http_buffer_t *reply)
{
	unsigned int ix, length;
	const http_handler_t *handler;
	http_action_t action;

	if((request.length < 5) || strncmp(request.string, "GET /", 5))
	{
		reply->length += snprintf(reply->string, reply->size, "400 Bad request\r\n");
		return(http_action_error);
	}

	request.string += 4;
	request.length -= 4;

	for(ix = 0; (request.string[ix] > ' ') && ((ix + 1) < sizeof(location_buffer)); ix++)
		location_buffer[ix] = request.string[ix];

	location_buffer[ix] = '\0';

	for(ix = 0; ix < (sizeof(handlers) / sizeof(*handlers)); ix++)
	{
		handler = &handlers[ix];

		if(!strcmp(handler->location, location_buffer))
			break;
	}

	if(ix >= (sizeof(handlers) / sizeof(*handlers)))
	{
		reply->length += snprintf(reply->string, reply->size, "404 Not found: %s\r\n", location_buffer);
		return(http_action_error);
	}

	length = snprintf(reply->string, reply->size, "%s", html_header);
	reply->string += length;
	reply->size -= length;
	reply->length += length;

	action = handler->handler(location_buffer, reply);

	length = snprintf(reply->string, reply->size, "%s", html_footer);
	reply->string += length;
	reply->size -= length;
	reply->length += length;

	return(action);
}
