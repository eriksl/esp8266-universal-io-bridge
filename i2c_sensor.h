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
	i2c_sensor_bme280_humidity,
	i2c_sensor_bme280_airpressure,
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
	unsigned int	detect_succeeded;
	unsigned int	detect_skip_disabled;
	unsigned int	detect_skip_secondary;
	unsigned int	detect_skip_found_on_bus_0;
	unsigned int	detect_skip_duplicate_address;
	unsigned int	detect_failed;
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
	unsigned int	init_current_bus;
	unsigned int	init_current_sensor;

	unsigned int	background_called;
	unsigned int	background_background_called;
	unsigned int	background_bus_select_failed;
	unsigned int	background_sensor_called;
	unsigned int	background_wrapped;
	unsigned int	background_current_bus;
	unsigned int	background_current_sensor;
	unsigned int	background_finished;
} i2c_sensor_info_t;

assert_size(i2c_sensor_info_t, 144);

typedef struct
{
	double raw;
	double cooked;
} i2c_sensor_value_t;

typedef struct attr_packed
{
	unsigned int registered:7;
	unsigned int high_sensitivity:1;
} i2c_sensor_device_data_t;

assert_size(i2c_sensor_device_data_t, 1);

enum
{
	sdte_secondary = 1 << 0,
};

typedef union
{
	struct
	{
		uint8_t id;
		uint8_t secondary[3];
	} ram;
	uint32_t flash;
} i2c_sensor_device_table_id_t;

assert_size(i2c_sensor_device_table_id_t, 4);

typedef struct i2c_sensor_device_table_entry_T
{
	attr_flash_align	i2c_sensor_device_table_id_t id;
	attr_flash_align	uint32_t address;
	attr_flash_align	uint32_t precision;
	attr_flash_align	uint32_t flags;
	attr_flash_align	const char *name;
	attr_flash_align	const char *type;
	attr_flash_align	const char *unity;
	attr_flash_align	i2c_error_t (* const detect_fn)(const struct i2c_sensor_device_table_entry_T *, i2c_sensor_device_data_t *data);
	attr_flash_align	i2c_error_t (* const init_fn)(const struct i2c_sensor_device_table_entry_T *, i2c_sensor_device_data_t *data);
	attr_flash_align	i2c_error_t (* const read_fn)(const struct i2c_sensor_device_table_entry_T *, i2c_sensor_value_t *, i2c_sensor_device_data_t *data);
	attr_flash_align	void (* const background_fn)(const struct i2c_sensor_device_table_entry_T *, i2c_sensor_device_data_t *data);
} i2c_sensor_device_table_entry_t;

assert_size(i2c_sensor_device_table_entry_t, 44);

void		i2c_sensor_get_info(i2c_sensor_info_t *);
i2c_error_t	i2c_sensor_detect(int bus, i2c_sensor_t);
void		i2c_sensors_periodic(void);
bool		i2c_sensor_read(string_t *, int bus, i2c_sensor_t, bool verbose, bool html);
bool		i2c_sensor_registered(int bus, i2c_sensor_t);

#endif
