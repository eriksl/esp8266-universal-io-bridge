#ifndef application_h
#define application_h

#include <stdint.h>
#include <dispatch.h>

void			application_init(void);
app_action_t	application_content(app_params_t *);
#endif
