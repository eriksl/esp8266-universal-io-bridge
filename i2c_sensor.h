#ifndef i2c_sensor_h
#define i2c_sensor_h

#include "config.h"
#include "i2c.h"
#include "util.h"

#include <stdint.h>

typedef enum attr_packed
{
	i2c_sensor_digipicco_temperature,
	i2c_sensor_digipicco_humidity,
	i2c_sensor_lm75_0,
	i2c_sensor_lm75_1,
	i2c_sensor_lm75_2,
	i2c_sensor_lm75_3,
	i2c_sensor_ds1631_6,
	i2c_sensor_lm75_7,
	i2c_sensor_bmp085_temperature,
	i2c_sensor_bmp085_airpressure,
	i2c_sensor_tsl2560,
	i2c_sensor_tsl2550,
	i2c_sensor_bh1750,
	i2c_sensor_htu21_temperature,
	i2c_sensor_htu21_humidity,
	i2c_sensor_am2321_temperature,
	i2c_sensor_am2321_humidity,
	i2c_sensor_veml6070,
	i2c_sensor_si114x_visible_light,
	i2c_sensor_si114x_infrared,
	i2c_sensor_si114x_ultraviolet,
	i2c_sensor_bme280_temperature,
	i2c_sensor_bme280_humidity,
	i2c_sensor_bme280_airpressure,
	i2c_sensor_error,
	i2c_sensor_size = i2c_sensor_error
} i2c_sensor_t;

assert_size(i2c_sensor_t, 1);

i2c_error_t	i2c_sensor_init(int bus, i2c_sensor_t);
void		i2c_sensor_init_all(void);
bool_t		i2c_sensor_read(string_t *, int bus, i2c_sensor_t, bool_t verbose);
bool_t		i2c_sensor_detected(int bus, i2c_sensor_t);

#endif
