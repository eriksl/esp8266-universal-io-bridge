#include "application.h"

#include "stats.h"
#include "util.h"

typedef struct
{
	const char	*command;
	uint8_t		required_args;
	uint8_t		(*function)(application_parameters_t);
	const char	*description;
} application_function_table_t;

static uint8_t application_function_help(application_parameters_t ap);
static uint8_t application_function_reset(application_parameters_t ap);
static uint8_t application_function_stats(application_parameters_t ap);

static const application_function_table_t application_function_table[] =
{
	{
		"help",
		0,
		application_function_help,
		"help (command)",
	},
	{
		"?",
		0,
		application_function_help,
		"help (command)",
	},
	{
		"reset",
		0,
		application_function_reset,
		"reset using watchdog timeout",
	},
	{
		"s",
		0,
		application_function_stats,
		"statistics",
	},
	{
		"stats",
		0,
		application_function_stats,
		"statistics",
	},
	{
		"",
		0,
		(void *)0,
		"",
	},
};

void application_init(void)
{
}

void application_periodic(void)
{
	stat_application_periodic++;
}

uint8_t application_content(const char *src, uint16_t size, char *dst)
{
	static const char *error_fmt_unknown = "Command \"%s\" unknown\n";
	static const char *error_fmt_args = "Insufficient arguments: %d (%d required)\n";

	args_t	args;
	uint8_t args_count, arg_current;
	uint8_t src_current = 0;
	uint16_t src_left;
	uint8_t ws_skipped;
	const application_function_table_t *tableptr;

	*dst = '\0';

	if(src[0] == '\0')
		return(1);

	src_left = strlen(src);

	for(args_count = 0; (src_left > 0) && (args_count < application_num_args);)
	{
		ws_skipped = 0;

		for(arg_current = 0;
				(src_left > 0) && (arg_current < (application_length_args - 1));
				src_current++, src_left--)
		{
			if((src[src_current] <= ' ') || (src[src_current] > '~'))
			{
				if(!ws_skipped)
					continue;
				else
					break;
			}

			ws_skipped = 1;

			args[args_count][arg_current++] = src[src_current];
		}

		args[args_count][arg_current] = '\0';

		if(arg_current)
			args_count++;

		while((src_left > 0) && (src[src_current] > ' ') && (src[src_current] <= '~'))
		{
			src_left--;
			src_current++;
		}
	}

	if(args_count == 0)
		return(1);

	for(tableptr = application_function_table; tableptr->function; tableptr++)
		if(!strcmp(args[0], tableptr->command))
			break;

	if(tableptr->function)
	{
		if(args_count < (tableptr->required_args + 1))
		{
			snprintf(dst, size, error_fmt_args, args_count - 1, tableptr->required_args);
			return(1);
		}

		application_parameters_t ap;

		ap.cmdline			= src;
		ap.nargs			= args_count;
		ap.args				= &args;
		ap.size				= size;
		ap.dst				= dst;

		return(tableptr->function(ap));
	}

	snprintf(dst, size, error_fmt_unknown, args[0]);
	return(1);
}

static uint8_t application_function_help(application_parameters_t ap)
{
	static const char *list_header		= "> %s[%d]\n";
	static const char *detail_header	= "> %s[%d]: ";
	static const char *detail_footer	= "\n";
	static const char *detail_error		= "> no help for \"%s\"\n";

	const application_function_table_t *tableptr;
	uint8_t offset;

	if(ap.nargs > 1)
	{
		for(tableptr = application_function_table; tableptr->function; tableptr++)
			if(!strcmp((*ap.args)[1], tableptr->command))
				break;

		if(tableptr->function)
		{
			snprintf(ap.dst, ap.size, detail_header, tableptr->command, tableptr->required_args);
			strlcat(ap.dst, tableptr->description, ap.size);
			strlcat(ap.dst, detail_footer, ap.size);
		}
		else
			snprintf(ap.dst, ap.size, detail_error, (*ap.args)[1]);
	}
	else
	{
		for(tableptr = application_function_table; tableptr->function; tableptr++)
		{
			offset = snprintf(ap.dst, ap.size, list_header, tableptr->command, tableptr->required_args);
			ap.dst	+= offset;
			ap.size	-= offset;
		}
	}

	return(1);
}

static uint8_t application_function_reset(application_parameters_t ap)
{
	reset();

	return(0);
}

static uint8_t application_function_stats(application_parameters_t ap)
{
	stats_generate(ap.size, ap.dst);

	return(1);
}
