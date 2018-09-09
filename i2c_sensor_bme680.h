#ifndef i2c_sensor_bme280_h
#define i2c_sensor_bme280_h
i2c_error_t sensor_bme680_temperature_init(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_device_data_t *data);
i2c_error_t sensor_bme680_temperature_read(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_value_t *value, i2c_sensor_device_data_t *data);
i2c_error_t sensor_bme680_humidity_init(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_device_data_t *data);
i2c_error_t sensor_bme680_humidity_read(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_value_t *value, i2c_sensor_device_data_t *data);
i2c_error_t sensor_bme680_airpressure_init(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_device_data_t *data);
i2c_error_t sensor_bme680_airpressure_read(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_value_t *value, i2c_sensor_device_data_t *data);
i2c_error_t sensor_bme680_airquality_init(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_device_data_t *data);
i2c_error_t sensor_bme680_airquality_read(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_value_t *value, i2c_sensor_device_data_t *data);
#endif
