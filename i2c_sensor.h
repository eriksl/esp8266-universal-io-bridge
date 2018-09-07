#ifndef i2c_sensor_h
#define i2c_sensor_h

#include "config.h"
#include "i2c.h"
#include "util.h"

#include <stdint.h>

typedef enum attr_packed
{
	i2c_sensor_digipicco_temperature = 0,
	i2c_sensor_digipicco_humidity,
	i2c_sensor_lm75_0,
	i2c_sensor_lm75_1,
	i2c_sensor_lm75_2,
	i2c_sensor_lm75_3,
	i2c_sensor_ds1631_6,
	i2c_sensor_lm75_7,
	i2c_sensor_bmp085_temperature,
	i2c_sensor_bmp085_airpressure,
	i2c_sensor_tsl2561_0,
	i2c_sensor_tsl2550,
	i2c_sensor_bh1750,
	i2c_sensor_htu21_temperature,
	i2c_sensor_htu21_humidity,
	i2c_sensor_am2320_temperature,
	i2c_sensor_am2320_humidity,
	i2c_sensor_veml6070,
	i2c_sensor_si114x_visible_light,
	i2c_sensor_si114x_infrared,
	i2c_sensor_si114x_ultraviolet,
	i2c_sensor_bme280_temperature,
	i2c_sensor_bme280_humidity,
	i2c_sensor_bme280_airpressure,
	i2c_sensor_tsl2561_1,
	i2c_sensor_max44009_0,
	i2c_sensor_veml6075_uvindex,
	i2c_sensor_mpl3115a2_temperature,
	i2c_sensor_mpl3115a2_airpressure,
	i2c_sensor_ccs811_co2,
	i2c_sensor_ccs811_tov,
	i2c_sensor_veml6075_infrared_light,
	i2c_sensor_veml6075_visible_light,
	i2c_sensor_si114x_temperature,
	i2c_sensor_hdc1080_temperature,
	i2c_sensor_hdc1080_humidity,
	i2c_sensor_hih6130_temperature,
	i2c_sensor_hih6130_humidity,
	i2c_sensor_sht30_temperature,
	i2c_sensor_sht30_humidity,
	i2c_sensor_mcp9808_temperature,
	i2c_sensor_bme680_temperature,
	i2c_sensor_bme680_humidity,
	i2c_sensor_bme680_airpressure,
	i2c_sensor_bme680_airquality,
	i2c_sensor_error,
	i2c_sensor_size = i2c_sensor_error
} i2c_sensor_t;

assert_size(i2c_sensor_t, 1);

typedef struct
{
	unsigned int	init_called;
	unsigned int	init_succeeded;
	unsigned int	init_failed;
	unsigned int	init_current_bus;
	i2c_sensor_t	init_current_sensor;
	bool_t			init_finished;
} i2c_sensor_info_t;

typedef struct
{
	double raw;
	double cooked;
} i2c_sensor_value_t;

typedef struct i2c_sensor_device_table_entry_T
{
	i2c_sensor_t id;
	uint8_t address;
	const char *name;
	const char *type;
	const char *unity;
	uint8_t precision;
	i2c_error_t (* const init_fn)(int bus, const struct i2c_sensor_device_table_entry_T *);
	i2c_error_t (* const read_fn)(int bus, const struct i2c_sensor_device_table_entry_T *, i2c_sensor_value_t *);
} i2c_sensor_device_table_entry_t;

void		i2c_sensor_get_info(i2c_sensor_info_t *);
i2c_error_t	i2c_sensor_init(int bus, i2c_sensor_t);
bool_t		i2c_sensors_init(void);
bool_t		i2c_sensor_read(string_t *, int bus, i2c_sensor_t, bool_t verbose, bool_t html);
bool_t		i2c_sensor_detected(int bus, i2c_sensor_t);

#endif
