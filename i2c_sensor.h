#ifndef i2c_sensor_h
#define i2c_sensor_h

#include "i2c.h"
#include "util.h"

#include <stdint.h>

typedef enum __attribute__ ((__packed__))
{
	i2c_sensor_digipicco_temperature,
	i2c_sensor_digipicco_humidity,
	i2c_sensor_lm75,
	i2c_sensor_ds1631,
	i2c_sensor_bmp085_temperature,
	i2c_sensor_bmp085_airpressure,
	i2c_sensor_tsl2560,
	i2c_sensor_bh1750,
	i2c_sensor_htu21_temperature,
	i2c_sensor_htu21_humidity,
	i2c_sensor_am2321_temperature,
	i2c_sensor_am2321_humidity,
	i2c_sensor_error,
	i2c_sensor_size = i2c_sensor_error
} i2c_sensor_t;

_Static_assert(sizeof(i2c_sensor_t) == 1, "sizeof(i2c_sensor_t) != 1");

void i2c_sensor_init(void);
uint16_t i2c_sensor_read(i2c_sensor_t sensor, bool_t verbose, uint16_t size, char *dst);

#endif
