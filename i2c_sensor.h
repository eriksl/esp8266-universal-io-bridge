#ifndef i2c_sensor_h
#define i2c_sensor_h

#include "i2c.h"
#include "util.h"

#include <stdint.h>

typedef enum __attribute__ ((__packed__))
{
	i2c_sensor_digipicco_temperature,
	i2c_sensor_digipicco_humidity,
	i2c_sensor_ds1631,
	i2c_sensor_lm75,
	i2c_sensor_bmp085_temperature,
	i2c_sensor_bmp085_airpressure,
	i2c_sensor_tsl2560,
	i2c_sensor_tsl2550,
	i2c_sensor_bh1750,
	i2c_sensor_htu21_temperature,
	i2c_sensor_htu21_humidity,
	i2c_sensor_am2321_temperature,
	i2c_sensor_am2321_humidity,
	i2c_sensor_error,
	i2c_sensor_size = i2c_sensor_error
} i2c_sensor_t;

_Static_assert(sizeof(i2c_sensor_t) == 1, "sizeof(i2c_sensor_t) != 1");

typedef struct
{
	struct
	{
		struct
		{
			float factor;
			float offset;
		} calibration;
	} sensor[i2c_sensor_size];
} i2c_sensor_config_t;

void			i2c_sensor_init(void);
void			i2c_sensor_config_init(i2c_sensor_config_t *);
unsigned int	i2c_sensor_read(i2c_sensor_t sensor, bool_t verbose, unsigned int size, char *dst);
bool_t			i2c_sensor_detected(i2c_sensor_t);
bool_t			i2c_sensor_getcal(i2c_sensor_t sensor, float *factor, float *offset);
bool_t			i2c_sensor_setcal(i2c_sensor_t sensor, float factor, float offset);

#endif
