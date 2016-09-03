#ifndef i2c_sensor_h
#define i2c_sensor_h

#include "config.h"
#include "i2c.h"
#include "util.h"
#include "i2c_sensor_config.h"

#include <stdint.h>

i2c_error_t	i2c_sensor_init(i2c_sensor_t);
void		i2c_sensor_init_all(void);
void		i2c_sensor_config_init(i2c_sensors_config_t *);
void		i2c_sensor_export(const config_t *cfg, string_t *dst);
bool		i2c_sensor_read(string_t *, i2c_sensor_t, bool_t verbose);
bool_t		i2c_sensor_detected(i2c_sensor_t);
bool_t		i2c_sensor_getcal(i2c_sensor_t sensor, float *factor, float *offset);
bool_t		i2c_sensor_setcal(i2c_sensor_t sensor, float factor, float offset);

#endif
