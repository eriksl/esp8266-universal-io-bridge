#ifndef i2c_sensor_config_h
#define i2c_sensor_config_h

typedef enum
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
	i2c_sensor_error,
	i2c_sensor_size = i2c_sensor_error
} i2c_sensor_t;

typedef struct
{
	struct
	{
		float factor;
		float offset;
	} calibration;
} i2c_sensor_config_t;

typedef struct
{
	i2c_sensor_config_t sensor[i2c_sensor_size];
} i2c_sensors_config_t;

_Static_assert(sizeof(i2c_sensor_t) == 4, "sizeof(i2c_sensor_t) != 4");

#endif
