#ifndef i2c_sensor_h
#define i2c_sensor_h

#include "config.h"
#include "i2c.h"
#include "util.h"

#include <stdint.h>
#include <stdbool.h>

typedef enum
{
	i2c_sensor_opt3001 = 0,
	i2c_sensor_veml6075_uvindex,
	i2c_sensor_tmd2771,
	i2c_sensor_apds9930,
	i2c_sensor_apds9960,
	i2c_sensor_si114x_ultraviolet,
	i2c_sensor_si114x_visible_light,
	i2c_sensor_si114x_infrared,
	i2c_sensor_si114x_temperature,
	i2c_sensor_max44009,
	i2c_sensor_veml6070_38,
	i2c_sensor_veml6070_39,
	i2c_sensor_tsl2561_39,
	i2c_sensor_tsl2561_29,
	i2c_sensor_tsl2550,
	i2c_sensor_bh1750,
	i2c_sensor_tmp75_48,
	i2c_sensor_tmp75_49,
	i2c_sensor_tmp75_4a,
	i2c_sensor_tmp75_4f,
	i2c_sensor_ds7505_48,
	i2c_sensor_ds7505_49,
	i2c_sensor_ds7505_4a,
	i2c_sensor_ds7505_4f,
	i2c_sensor_ds1631_48,
	i2c_sensor_ds1631_49,
	i2c_sensor_ds1631_4a,
	i2c_sensor_ds1631_4f,
	i2c_sensor_ds1621_48,
	i2c_sensor_ds1621_49,
	i2c_sensor_ds1621_4a,
	i2c_sensor_ds1621_4f,
	i2c_sensor_tmp102_48,
	i2c_sensor_tmp102_49,
	i2c_sensor_lm75_48,
	i2c_sensor_lm75_49,
	i2c_sensor_lm75_4a,
	i2c_sensor_lm75_4f,
	i2c_sensor_mpl3115a2_temperature,
	i2c_sensor_mpl3115a2_airpressure,
	i2c_sensor_ccs811,
	i2c_sensor_sht30_temperature,
	i2c_sensor_sht30_humidity,
	i2c_sensor_mcp9808_temperature,
	i2c_sensor_hdc1080_temperature,
	i2c_sensor_hdc1080_humidity,
	i2c_sensor_htu21_temperature,
	i2c_sensor_htu21_humidity,
	i2c_sensor_bme680_temperature,
	i2c_sensor_bme680_humidity,
	i2c_sensor_bme680_airpressure,
	i2c_sensor_bme280_temperature,
	i2c_sensor_bme280_airpressure,
	i2c_sensor_bme280_humidity,
	i2c_sensor_bmp280_temperature,
	i2c_sensor_bmp280_airpressure,
	i2c_sensor_bmp085_temperature,
	i2c_sensor_bmp085_airpressure,
	i2c_sensor_am2320_temperature,
	i2c_sensor_am2320_humidity,
	i2c_sensor_hih6130_humidity,
	i2c_sensor_hih6130_temperature,
	i2c_sensor_digipicco_temperature,
	i2c_sensor_digipicco_humidity,
	i2c_sensor_tsl2591_29,
	i2c_sensor_tsl2591_28,
	i2c_sensor_aht10_temperature,
	i2c_sensor_aht10_humidity,
	i2c_sensor_veml6040,
	i2c_sensor_veml7700,
	i2c_sensor_error,
	i2c_sensor_none = i2c_sensor_error,
	i2c_sensor_size = i2c_sensor_error
} i2c_sensor_t;

assert_size(i2c_sensor_t, 4);

typedef struct
{
	unsigned int	periodic_called;

	uint64_t		detect_started_us;
	uint64_t		detect_finished_us;
	unsigned int	detect_started:1;
	unsigned int	detect_finished:1;
	unsigned int	detect_called;
	unsigned int	detect_failed;
	unsigned int	detect_succeeded;
	unsigned int	detect_bus_select_failed;
	unsigned int	detect_skip_disabled;
	unsigned int	detect_skip_secondary;
	unsigned int	detect_skip_found_on_bus_0;
	unsigned int	detect_skip_duplicate_address;
	unsigned int	detect_current_bus;
	i2c_sensor_t	detect_current_sensor;

	uint64_t		init_started_us;
	uint64_t		init_finished_us;
	unsigned int	init_started:1;
	unsigned int	init_finished:1;
	unsigned int	init_called;
	unsigned int	init_bus_select_failed;
	unsigned int	init_succeeded;
	unsigned int	init_failed;
	unsigned int	init_skipped;
	unsigned int	init_current_sensor;

	unsigned int	background_called;
	unsigned int	background_succeeded;
	unsigned int	background_failed;
	unsigned int	background_bus_select_failed;
	unsigned int	background_sensor_called;
	unsigned int	background_wrapped;
	unsigned int	background_current_sensor;
	unsigned int	background_finished;
} i2c_sensor_info_t;

assert_size(i2c_sensor_info_t, 152);

void i2c_sensor_get_info(i2c_sensor_info_t *);
void i2c_sensors_periodic(void);
bool i2c_sensor_read(string_t *, int bus, i2c_sensor_t, bool verbose, bool html);
bool i2c_sensor_registered(int bus, i2c_sensor_t);
void i2c_sensor_dump(bool verbose, string_t *dst);

#endif
