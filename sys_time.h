#ifndef _sys_time_h_
#define _sys_time_h_

#include "util.h"
#include "attribute.h"

#include <stdint.h>
#include <stdbool.h>

void time_init(void);
void time_periodic(void);
void time_sntp_start(void);
void time_stats(string_t *);

void		time_set_hms(unsigned int h, unsigned int m, unsigned int s);
void		time_set_stamp(uint64_t base);
bool		time_set_timezone(int tz, string_t *errormsg);
int			time_get_timezone(void);
const char *time_get(unsigned int *h, unsigned int *m, unsigned int *s,
				unsigned int *Y, unsigned int *M, unsigned int *D);
uint64_t	time_get_us(void);
bool		time_sntp_get_server(ip_addr_t *server);
bool		time_sntp_set_server(string_t *errormsg, ip_addr_t server);

#endif
