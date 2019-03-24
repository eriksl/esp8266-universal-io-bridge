/*
 * copyright (c) 2010 - 2011 espressif system
 */

#include "stdint.h"

#define uint32_t _x_uint32_t_
#define int32_t _x_int32_t_
#include "ets_sys.h"
#undef uint32_t
#undef int32_t

#include "osapi.h"
#include "os_type.h"

#include "lwip/opt.h"
#include "lwip/sys.h"

#include "eagle_soc.h"
