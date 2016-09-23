#include "i2c_sensor.h"

#include "util.h"
#include "config.h"

typedef struct
{
	double raw;
	double cooked;
} value_t;

typedef struct attr_packed
{
	uint32_t detected;
} device_data_t;

assert_size(device_data_t, 4);

typedef struct device_table_entry_T
{
	i2c_sensor_t id;
	uint8_t address;
	const char *name;
	const char *type;
	const char *unity;
	uint8_t precision;
	i2c_error_t (* const init_fn)(int bus, const struct device_table_entry_T *);
	i2c_error_t (* const read_fn)(int bus, const struct device_table_entry_T *, value_t *);
} device_table_entry_t;

device_data_t device_data[i2c_sensor_size];

irom static i2c_error_t sensor_digipicco_temp_init(int bus, const device_table_entry_t *entry)
{
	i2c_error_t error;
	uint8_t	i2cbuffer[4] = { 0, 0, 0, 0 };

	if((error = i2c_receive(entry->address, 4, i2cbuffer)) != i2c_error_ok)
		return(error);

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_digipicco_temp_read(int bus, const device_table_entry_t *entry, value_t *value)
{
	i2c_error_t error;
	uint8_t	i2cbuffer[4];

	if((error = i2c_receive(entry->address, 4, i2cbuffer)) != i2c_error_ok)
		return(error);

	value->raw = ((uint16_t)i2cbuffer[2] << 8) | (uint16_t)i2cbuffer[3];
	value->cooked = ((value->raw * 165.0) / 32767.0) - 40.5;

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_digipicco_hum_init(int bus, const device_table_entry_t *entry)
{
	if(!i2c_sensor_detected(bus, i2c_sensor_digipicco_temperature))
		return(i2c_error_address_nak);

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_digipicco_hum_read(int bus, const device_table_entry_t *entry, value_t *value)
{
	i2c_error_t error;
	uint8_t	i2cbuffer[4];

	if((error = i2c_receive(entry->address, 4, i2cbuffer)) != i2c_error_ok)
		return(error);

	value->raw = ((uint16_t)i2cbuffer[0] << 8) | (uint16_t)i2cbuffer[1];
	value->cooked = (value->raw * 100.0) / 32768.0;

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_ds1631_init(int bus, const device_table_entry_t *entry)
{
	i2c_error_t error;

	//	0xac	select config register
	//	0x0c	r0=r1=1, max resolution, other bits zero

	if((error = i2c_send_2(entry->address, 0xac, 0x0c)) != i2c_error_ok)
		return(error);

	// start conversions

	if((error = i2c_send_1(entry->address, 0x51)) != i2c_error_ok)
		return(error);

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_ds1631_read(int bus, const device_table_entry_t *entry, value_t *value)
{
	uint8_t i2cbuffer[2];
	i2c_error_t error;
	int raw;

	// read temperature

	if((error = i2c_send_1(entry->address, 0xaa)) != i2c_error_ok)
		return(error);

	if((error = i2c_receive(entry->address, 2, i2cbuffer)) != i2c_error_ok)
		return(error);

	value->raw = raw = ((unsigned int)(i2cbuffer[0] << 8)) | i2cbuffer[1];

	if(raw & 0x8000)
	{
		raw &= ~(uint32_t)0x8000;
		value->cooked = (double)raw / -256;
	}
	else
		value->cooked = (double)raw / 256;

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_lm75_init(int bus, const device_table_entry_t *entry)
{
	uint8_t i2cbuffer[4];
	i2c_error_t error;

	// 0x01		select config register
	// 0x60		set all defaults, operation is not shutdown
	// 			specific for tmp275 variant, select high-res operation

	if((error = i2c_send_2(entry->address, 0x01, 0x60)) != i2c_error_ok)
		return(error);

	if((error = i2c_receive(entry->address, 1, i2cbuffer)) != i2c_error_ok)
		return(error);

	if((i2cbuffer[0] != 0x60 /* most */) && (i2cbuffer[0] != 0x00 /* lm75bd */))
		return(i2c_error_device_error_1);

	// 0x03	select overtemperature register

	if((error = i2c_send_3(entry->address, 0x03, 0xff, 0xff)) != i2c_error_ok)
		return(error);

	if((error = i2c_receive(entry->address, 2, i2cbuffer)) != i2c_error_ok)
		return(error);

	if((i2cbuffer[0] != 0xff) || ((i2cbuffer[1] & 0x0f) != 0x00))
		return(i2c_error_device_error_2);

	// 0x03	select overtemperature register

	if((error = i2c_send_3(entry->address, 0x03, 0x00, 0x00)) != i2c_error_ok)
		return(error);

	if((error = i2c_receive(entry->address, 2, i2cbuffer)) != i2c_error_ok)
		return(error);

	if((i2cbuffer[0] != 0x00) || (i2cbuffer[1] != 0x00))
		return(i2c_error_device_error_3);

	// select temperature register

	if((error = i2c_send_1(entry->address, 0x00)) != i2c_error_ok)
		return(error);

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_lm75_read(int bus, const device_table_entry_t *entry, value_t *value)
{
	uint8_t i2cbuffer[2];
	i2c_error_t error;
	int raw;

	i2cbuffer[0] = 0;

	if((error = i2c_send(entry->address, 1, i2cbuffer)) != i2c_error_ok)
		return(error);

	if((error = i2c_receive(entry->address, 2, i2cbuffer)) != i2c_error_ok)
		return(error);

	value->raw = raw = (i2cbuffer[0] << 8) | i2cbuffer[1];

	if(raw & 0x8000)
	{
		raw &= ~0x8000;
		value->cooked = (double)raw / -256;
	}
	else
		value->cooked = (double)raw / 256;

	return(i2c_error_ok);
}

irom static i2c_error_t bmp085_write(int address, int reg, int value)
{
	i2c_error_t error;

	if((error = i2c_send_2(address, reg, value)) != i2c_error_ok)
		return(error);

	return(0);
}

irom static i2c_error_t bmp085_read(int address, int reg, uint16_t *value)
{
	i2c_error_t error;
	uint8_t i2cbuffer[2];

	if((error = i2c_send_1(address, reg)) != i2c_error_ok)
		return(error);

	if((error = i2c_receive(address, 2, i2cbuffer)) != i2c_error_ok)
		return(error);

	*value = ((uint16_t)(i2cbuffer[0] << 8)) | (uint16_t)i2cbuffer[1];

	return(0);
}

irom static i2c_error_t bmp085_read_long(int address, int reg, uint32_t *value)
{
	i2c_error_t error;
	uint8_t i2cbuffer[4];

	if((error = i2c_send_1(address, reg)) != i2c_error_ok)
		return(error);

	if((error = i2c_receive(address, 3, i2cbuffer)) != i2c_error_ok)
		return(error);

	*value = ((uint32_t)i2cbuffer[0] << 16) | ((uint32_t)i2cbuffer[1] << 8) | (uint32_t)i2cbuffer[2];

	return(0);
}

irom static i2c_error_t sensor_read_bmp085(int address, double *temp, double *temp_raw, double *pressure, double *pressure_raw)
{
	int16_t		ac1, ac2, ac3;
	uint16_t	ac4, ac5, ac6;
	int16_t		b1, b2;
	int16_t		mc, md;
	uint16_t	ut;
	uint32_t	up;
	int32_t		b3, b4, b5, b6;
	uint32_t	b7;
	int32_t		x1, x2, x3, p;
	uint8_t		oss = 3;
	i2c_error_t	error;

	if((error = bmp085_read(address, 0xaa, (uint16_t *)&ac1)) != i2c_error_ok)
		return(error);

	if((error = bmp085_read(address, 0xac, (uint16_t *)&ac2)) != i2c_error_ok)
		return(error);

	if((error = bmp085_read(address, 0xae, (uint16_t *)&ac3)) != i2c_error_ok)
		return(error);

	if((error = bmp085_read(address, 0xb0, &ac4)) != i2c_error_ok)
		return(error);

	if((error = bmp085_read(address, 0xb2, &ac5)) != i2c_error_ok)
		return(error);

	if((error = bmp085_read(address, 0xb4, &ac6)) != i2c_error_ok)
		return(error);

	if((error = bmp085_read(address, 0xb6, (uint16_t *)&b1)) != i2c_error_ok)
		return(error);

	if((error = bmp085_read(address, 0xb8, (uint16_t *)&b2)) != i2c_error_ok)
		return(error);

	if((error = bmp085_read(address, 0xbc, (uint16_t *)&mc)) != i2c_error_ok)
		return(error);

	if((error = bmp085_read(address, 0xbe, (uint16_t *)&md)) != i2c_error_ok)
		return(error);

	if((error = bmp085_write(address, 0xf4, 0x2e)) != i2c_error_ok) // set cmd = 0x2e = start temperature measurement
		return(error);

	msleep(5);

	if((error = bmp085_read(address, 0xf6, &ut)) != i2c_error_ok) // select result 0xf6+0xf7
		return(error);

#if 0
			ac1	= 408;
			ac2	= -72;
			ac3	= -14383;
			ac4	= 32741;
			ac5 = 32757;
			ac6 = 23153;
			b1	= 6190;
			b2	= 4;
			mc	= -8711;
			md	= 2868;

			ut = 27898;
#endif

	x1 = (((uint32_t)ut - (uint32_t)ac6) * (uint32_t)ac5) >> 15;

	if((x1 + (int32_t)md) == 0)
		return(i2c_error_device_error_1);

	x2 = ((int32_t)mc << 11) / (x1 + (int32_t)md);

	b5 = x1 + x2;

	*temp_raw	= ut;
	*temp		= ((((double)b5 + 8) / 16) / 10);

	if((error = bmp085_write(address, 0xf4, 0x34 | (oss << 6))) != i2c_error_ok) // set cmd = 0x34 = start air pressure measurement
		return(error);

	msleep(20);

	up = 0;

	if((error = bmp085_read_long(address, 0xf6, &up)) != i2c_error_ok) // select result 0xf6+0xf7+f8
		return(error);

	//up	= 23843;
	up = up >> (8 - oss);

	b6	= b5 - 4000;
	x1	= ((int32_t)b2 * ((b6 * b6) >> 12)) >> 11;
	x2	= ((int32_t)ac2 * b6) >> 11;
	x3	= x1 + x2;
	b3	= ((((int32_t)ac1 * 4 + x3) << oss) + 2) / 4;
	x1	= ((int32_t)ac3 * b6) >> 13;
	x2	= ((int32_t)b1 * ((b6 * b6) >> 12)) >> 16;
	x3	= (x1 + x2 + 2) >> 2;
	b4	= ((uint32_t)ac4 * (uint32_t)(x3 + 32768)) >> 15;
	b7	= (uint32_t)(((uint32_t)up - b3) * (50000 >> oss));

	if(b4 == 0)
		return(i2c_error_device_error_2);

	if(b7 & 0x80000000)
		p = (b7 / b4) << 1;
	else
		p = (b7 << 1) / b4;

	x1	= (p >> 8) * (p >> 8);
	x1	= (x1 * 3038UL) >> 16;
	x2	= (p * -7357) >> 16;
	p	= p + ((x1 + x2 + 3791L) >> 4);

	*pressure_raw	= up;
	*pressure		= p / 100.0;

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_bmp085_init_temp(int bus, const device_table_entry_t *entry)
{
	double temp, temp_raw, pressure, pressure_raw;
	i2c_error_t error;

	if((error = sensor_read_bmp085(entry->address, &temp, &temp_raw, &pressure, &pressure_raw)) != i2c_error_ok)
		return(error);

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_bmp085_init_pressure(int bus, const device_table_entry_t *entry)
{
	if(!i2c_sensor_detected(bus, i2c_sensor_bmp085_temperature))
		return(i2c_error_address_nak);

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_bmp085_read_temp(int bus, const device_table_entry_t *entry, value_t *value)
{
	double temp, temp_raw, pressure, pressure_raw;
	i2c_error_t error;

	if((error = sensor_read_bmp085(entry->address, &temp, &temp_raw, &pressure, &pressure_raw)) != i2c_error_ok)
		return(error);

	value->raw = temp_raw;
	value->cooked = temp;

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_bmp085_read_pressure(int bus, const device_table_entry_t *entry, value_t *value)
{
	double temp, temp_raw, pressure, pressure_raw;
	i2c_error_t error;

	if((error = sensor_read_bmp085(entry->address, &temp, &temp_raw, &pressure, &pressure_raw)) != i2c_error_ok)
		return(error);

	value->raw = pressure_raw;
	value->cooked = pressure;

	return(i2c_error_ok);
}

typedef struct
{
	const double ratio_top;
	const double ch0_factor;
	const double ch1_factor;
} tsl2560_lookup_t;

static const tsl2560_lookup_t tsl2560_lookup[] =
{
	{ 0.125, 0.03040, 0.02720 },
	{ 0.250, 0.03250, 0.04400 },
	{ 0.375, 0.03510, 0.05440 },
	{ 0.500, 0.03810, 0.06240 },
	{ 0.610, 0.02240, 0.03100 },
	{ 0.800, 0.01280, 0.01530 },
	{ 1.300, 0.00146, 0.00112 },
	{ 0.000, 0.00000, 0.00000 }
};

irom static i2c_error_t tsl2560_write(int address, int reg, int value)
{
	i2c_error_t error;

	// 0xc0	write byte

	if((error = i2c_send_2(address, 0xc0 | reg, value)) != i2c_error_ok)
		return(error);

	return(i2c_error_ok);
}

irom static i2c_error_t tsl2560_read(int address, int reg, uint8_t *byte)
{
	i2c_error_t error;

	// 0xc0	read byte

	if((error = i2c_send_1(address, 0xc0 | reg)) != i2c_error_ok)
		return(error);

	if((error = i2c_receive(address, 1, byte)) != i2c_error_ok)
		return(error);

	return(i2c_error_ok);
}

irom static i2c_error_t tsl2560_write_check(int address, int reg, int value)
{
	i2c_error_t error;
	uint8_t rv;

	if((error = tsl2560_write(address, reg, value)) != i2c_error_ok)
		return(error);

	if((error = tsl2560_read(address, reg, &rv)) != i2c_error_ok)
		return(error);

	if(value != rv)
		return(i2c_error_device_error_1);

	return(i2c_error_ok);
}

irom static i2c_error_t tsl2560_read_block(int address, int reg, uint8_t *values)
{
	i2c_error_t error;

	// 0xd0	read block

	if((error = i2c_send_1(address, 0xd0 | reg)) != i2c_error_ok)
		return(error);

	if((error = i2c_receive(address , 4, values)) != i2c_error_ok)
		return(error);

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_tsl2560_init(int bus, const device_table_entry_t *entry)
{
	i2c_error_t error;
	uint8_t regval;

	if(i2c_sensor_detected(bus, i2c_sensor_tsl2550))
		return(i2c_error_device_error_1);

	if((error = tsl2560_write_check(entry->address, 0x00, 0x00)) != i2c_error_ok) // power down
		return(error);

	if((error = tsl2560_write(entry->address, 0x00, 0x03)) != i2c_error_ok)	// power up
		return(error);

	if((error = tsl2560_read(entry->address, 0x00, &regval)) != i2c_error_ok)
		return(error);

	if((regval & 0x0f) != 0x03)
		return(i2c_error_device_error_2);

	if((error = tsl2560_write_check(entry->address, 0x06, 0x00)) != i2c_error_ok)	// disable interrupts
		return(error);

	if((error = tsl2560_write(entry->address, 0x0a, 0x00)) != i2c_error_ok)	// id register 1
		return(error);

	if((error = tsl2560_read(entry->address, 0x0a, &regval)) != i2c_error_ok) // read id register 1
		return(error);

	if(regval != 0x50)
		return(i2c_error_device_error_3);

	if((error = tsl2560_write(entry->address, 0x0b, 0x00)) != i2c_error_ok)	// id register 2
		return(error);

	if((error = tsl2560_read(entry->address, 0x0b, &regval)) != i2c_error_ok) // read id register 2
		return(error);

	if(regval != 0x04)
		return(i2c_error_device_error_3);


	if(config_flags_get().flag.tsl_high_sens)
		regval = 0b00010010; // 400 ms sampling window, gain is high, 16x
	else
		regval = 0b00000010; // 400 ms sampling window, gain is high, 1x

	if((error = tsl2560_write_check(entry->address, 0x01, regval)) != i2c_error_ok)	// start continuous sampling
		return(error);

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_tsl2560_read(int bus, const device_table_entry_t *entry, value_t *value)
{
	uint8_t	i2cbuffer[4];
	i2c_error_t	error;
	unsigned int ch0r, ch1r;
	double ratio, ch0, ch1;
	const tsl2560_lookup_t *tsl2560_entry;
	int current;

	if(i2c_sensor_detected(bus, i2c_sensor_tsl2550))
		return(i2c_error_device_error_1);

	if((i2c_receive(0x39, 1, i2cbuffer) == i2c_error_ok) &&
			(i2c_receive(0x38, 1, i2cbuffer) == i2c_error_ok))	// try to detect veml6070
		return(i2c_error_device_error_2);						// which uses both 0x38 and 0x39 addresses

	if((error = tsl2560_read_block(entry->address, 0x0c, i2cbuffer)) != i2c_error_ok)
		return(error);

	ch0r = i2cbuffer[0] | (i2cbuffer[1] << 8);
	ch1r = i2cbuffer[2] | (i2cbuffer[3] << 8);

	value->raw = ((double)ch1r * 1000000) + (double)ch0r;

	if((ch0r == 65535) || (ch1r == 65535))
	{
		value->cooked = -1;
		return(i2c_error_ok);
	}

	if(config_flags_get().flag.tsl_high_sens)
	{
		// high sensitivity = 400 ms integration time, scaling factor = 1
		// analogue amplification = 16x, scaling factor = 1

		ch0 = (double)ch0r * 1.0;
		ch1 = (double)ch1r * 1.0;
	}
	else
	{
		// low  sensitivity =  400 ms integration time, scaling factor = 1
		// analogue amplification = 1x, scaling factor = 16

		ch0 = (double)ch0r * 1.0 * 16.0;
		ch1 = (double)ch1r * 1.0 * 16.0;
	}

	if((unsigned int)ch0 != 0)
		ratio = ch1 / ch0;
	else
		ratio = 0;

	for(current = 0;; current++)
	{
		tsl2560_entry = &tsl2560_lookup[current];

		if(((unsigned int)tsl2560_entry->ratio_top == 0) || ((unsigned int)tsl2560_entry->ch0_factor == 0) || ((unsigned int)tsl2560_entry->ch1_factor == 0))
			break;

		if(ratio <= tsl2560_entry->ratio_top)
			break;
	}

	value->cooked = (ch0 * tsl2560_entry->ch0_factor) - (ch1 * tsl2560_entry->ch1_factor);

	if(value->cooked < 0)
		value->cooked = 0;

	return(i2c_error_ok);
}

static const uint16_t tsl2550_count[128] =
{
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 18, 20, 22, 24, 26,
	28, 30, 32, 34, 36, 38, 40, 42, 44, 46, 49, 53, 57, 61, 65, 69, 73, 77, 81,
	85, 89, 93, 97, 101, 105, 109, 115, 123, 131, 139, 147, 155, 163, 171, 179,
	187, 195, 203, 211, 219, 227, 235, 247, 263, 279, 295, 311, 327, 343, 359,
	375, 391, 407, 423, 439, 455, 471, 487, 511, 543, 575, 607, 639, 671, 703,
	735, 767, 799, 831, 863, 895, 927, 959, 991,
	1039,1103,1167,1231,1295,1359,1423,1487,
	1551,1615,1679,1743,1807,1871,1935,1999,
	2095,2223,2351,2479,2607,2735,2863,2991,
	3119,3247,3375,3503,3631,3759,3887,4015
};

static const uint8_t tsl2550_ratio[129] =
{
	100,100,100,100,100,100,100,100,
	100,100,100,100,100,100,99,99,
	99,99,99,99,99,99,99,99,
	99,99,99,98,98,98,98,98,
	98,98,97,97,97,97,97,96,
	96,96,96,95,95,95,94,94,
	93,93,93,92,92,91,91,90,
	89,89,88,87,87,86,85,84,
	83,82,81,80,79,78,77,75,
	74,73,71,69,68,66,64,62,
	60,58,56,54,52,49,47,44,
	42,41,40,40,39,39,38,38,
	37,37,37,36,36,36,35,35,
	35,35,34,34,34,34,33,33,
	33,33,32,32,32,32,32,31,
	31,31,31,31,30,30,30,30,
	30
};

irom static i2c_error_t sensor_tsl2550_rw(int address, int in, uint8_t *out)
{
	i2c_error_t error;

	if((error = i2c_send_1(address, in)) != i2c_error_ok)
		return(error);

	if((error = i2c_receive(address, 1, out)) != i2c_error_ok)
		return(error);

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_tsl2550_write_check(int address, int in, int compare)
{
	i2c_error_t error;
	uint8_t out;

	if((error = sensor_tsl2550_rw(address, in, &out)) != i2c_error_ok)
		return(error);

	if(out != compare)
		return(i2c_error_device_error_2);

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_tsl2550_init(int bus, const device_table_entry_t *entry)
{
	i2c_error_t error;
	int sens_command;
	uint8_t	i2cbuffer[2];

	if(i2c_sensor_detected(bus, i2c_sensor_tsl2560))
		return(i2c_error_device_error_1);

	// tsl2550 power up

	if((i2c_receive(0x39, 1, i2cbuffer) == i2c_error_ok) &&
			(i2c_receive(0x38, 1, i2cbuffer) == i2c_error_ok))	// try to detect veml6070
		return(i2c_error_device_error_2);						// which uses both 0x38 and 0x39 addresses

	if((error = sensor_tsl2550_write_check(entry->address, 0x03, 0x03)) != i2c_error_ok)
		return(error);

	if(config_flags_get().flag.tsl_high_sens)
		sens_command = 0x18;	// standard range mode
	else
		sens_command = 0x1d;	// extended range mode

	if((error = sensor_tsl2550_write_check(entry->address, sens_command, 0x1b)) != i2c_error_ok)
		return(error);

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_tsl2550_read(int bus, const device_table_entry_t *entry, value_t *value)
{
	i2c_error_t	error;
	uint8_t		ch0, ch1;
	int			attempt, ratio;

	if(i2c_sensor_detected(bus, i2c_sensor_tsl2560))
		return(i2c_error_device_error_1);

	error = i2c_error_ok;

	for(attempt = 16; attempt > 0; attempt--)
	{
		// read from channel 0

		if((error = sensor_tsl2550_rw(entry->address, 0x43, &ch0)) != i2c_error_ok)
			goto error;

		// read from channel 1

		if((error = sensor_tsl2550_rw(entry->address, 0x83, &ch1)) != i2c_error_ok)
			goto error;

		if((ch0 & 0x80) && (ch1 & 0x80))
			break;
error:
		msleep(10);
	}

	if(error != i2c_error_ok)
		return(error);

	ch0 &= 0x7f;
	ch1 &= 0x7f;

	value->raw = (ch0 * 10000.0) + ch1;

	if((tsl2550_count[ch1] <= tsl2550_count[ch0]) && (tsl2550_count[ch0] > 0))
		ratio = (tsl2550_count[ch1] * 128) / tsl2550_count[ch0];
	else
		ratio = 128;

	if(ratio > 128)
		ratio = 128;

	value->cooked = ((tsl2550_count[ch0] - tsl2550_count[ch1]) * tsl2550_ratio[ratio]) / 2560.0;

	if(value->cooked < 0)
		value->cooked = 0;

	if(!config_flags_get().flag.tsl_high_sens)
		value->cooked *= 5;

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_bh1750_init(int bus, const device_table_entry_t *entry)
{
	i2c_error_t error;
	int timing;
	uint8_t regval[2];

	// there is no "read register" command on this device, so assume
	// a device at 0x23 is actually a bh1750, there is no way to be sure.

	// power on

	if((error = i2c_send_1(entry->address, 0b00000001)) != i2c_error_ok)
		return(error);

	// reset

	if((error = i2c_send_1(entry->address, 0b00000111)) != i2c_error_ok)
		return(error);

	// set sensitivity
	// "window" can be set between 31 and 254
	// lux-per-count is 0.93 for low sensibility mode (window = 31)
	// lux-per-count is 0.11 for high sensibility mode (window = 254)

	if(config_flags_get().flag.bh_high_sens)
		timing = 254;
	else
		timing = 31;

	regval[0] = 0b01000000 | ((timing >> 5) & 0b00000111);
	regval[1] = 0b01100000 | ((timing >> 0) & 0b00011111);

	if((error = i2c_send_1(entry->address, regval[0])) != i2c_error_ok)
		return(error);

	if((error = i2c_send_1(entry->address, regval[1])) != i2c_error_ok)
		return(error);

	// start continuous sampling every 120 ms, high resolution = 0.42 Lx

	if((error = i2c_send_1(entry->address, 0b00010001)) != i2c_error_ok)
		return(error);

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_bh1750_read(int bus, const device_table_entry_t *entry, value_t *value)
{
	i2c_error_t error;
	uint8_t	i2cbuffer[2];
	double luxpercount;

	if((error = i2c_receive(entry->address, 2, i2cbuffer)) != i2c_error_ok)
		return(error);

	if(config_flags_get().flag.bh_high_sens)
		luxpercount = 0.11;
	else
		luxpercount = 0.93;

	value->raw		= (double)((i2cbuffer[0] << 8) | i2cbuffer[1]);
	value->cooked	= value->raw * luxpercount;

	return(i2c_error_ok);
}

irom attr_pure static uint8_t htu21_crc(int length, const uint8_t *data)
{
	uint8_t outer, inner, testbit, crc;

	crc = 0;

	for(outer = 0; (int)outer < length; outer++)
	{
		crc ^= data[outer];

		for(inner = 0; inner < 8; inner++)
		{
			testbit = !!(crc & 0x80);
			crc <<= 1;
			if(testbit)
				crc ^= 0x31;
		}
	}

	return(crc);
}

irom static i2c_error_t sensor_htu21_read(const device_table_entry_t *entry, uint8_t command, uint16_t *result)
{
	i2c_error_t error;
	uint8_t	i2cbuffer[4];
	uint8_t crc1, crc2;

	if((error = i2c_send_1(entry->address, command)) != i2c_error_ok)
		return(error);

	if((error = i2c_receive(entry->address, sizeof(i2cbuffer), i2cbuffer)) != i2c_error_ok)
		return(error);

	crc1 = i2cbuffer[2];
	crc2 = htu21_crc(2, &i2cbuffer[0]);

	if(crc1 != crc2)
		return(i2c_error_device_error_1);

	*result = ((uint16_t)i2cbuffer[0] << 8) | ((uint16_t)i2cbuffer[1] << 0);
	*result &= 0xfffc; // mask out status bits in the 2 LSB

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_htu21_temp_read(int bus, const device_table_entry_t *entry, value_t *value)
{
	i2c_error_t error;
	uint16_t result;

	// temperature measurement "hold master" mode -> 0xe3

	if((error = sensor_htu21_read(entry, 0xe3, &result)) != i2c_error_ok)
		return(error);

	value->raw = result;
	value->cooked = ((value->raw * 175.72) / 65536) - 46.85;

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_htu21_hum_read(int bus, const device_table_entry_t *entry, value_t *value)
{
	i2c_error_t error;
	uint16_t result;

	// humidity measurement "hold master" mode -> 0xe5

	if((error = sensor_htu21_read(entry, 0xe5, &result)) != i2c_error_ok)
		return(error);

	value->raw = result;
	value->cooked = ((value->raw * 125) / 65536) - 6;

	if(value->cooked < 0)
		value->cooked = 0;

	if(value->cooked > 100)
		value->cooked = 100;

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_htu21_temp_init(int bus, const device_table_entry_t *entry)
{
	value_t value;
	i2c_error_t error;

	if((error = sensor_htu21_temp_read(bus, entry, &value)) != i2c_error_ok)
		return(error);

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_htu21_hum_init(int bus, const device_table_entry_t *entry)
{
	if(!i2c_sensor_detected(bus, i2c_sensor_htu21_temperature))
		return(i2c_error_address_nak);

	return(i2c_error_ok);
}

irom attr_pure static uint16_t am2321_crc(int length, const uint8_t *data)
{
	uint8_t outer, inner, testbit;
	uint16_t crc;

	crc = 0xffff;

	for(outer = 0; outer < length; outer++)
	{
		crc ^= data[outer];

		for(inner = 0; inner < 8; inner++)
		{
			testbit = !!(crc & 0x01);
			crc >>= 1;
			if(testbit)
				crc ^= 0xa001;
		}
	}

	return(crc);
}

irom static i2c_error_t sensor_am2321_read_registers(int address, int offset, int length, uint8_t *values)
{
	i2c_error_t	error;
	uint8_t		i2cbuffer[32];
	uint16_t	crc1, crc2;

	// wake the device

	i2c_send_1(address, 0);

	msleep(1);

	if((error = i2c_send_3(address, 0x03, offset, length)) != i2c_error_ok)
		return(error);

	msleep(1);

	if((error = i2c_receive(address, length + 4, i2cbuffer)) != i2c_error_ok)
		return(error);

	if((i2cbuffer[0] != 0x03) || (i2cbuffer[1] != length))
		return(i2c_error_device_error_2);

	crc1 = i2cbuffer[length + 2] | (i2cbuffer[length + 3] << 8);
	crc2 = am2321_crc(length + 2, i2cbuffer);

	if(crc1 != crc2)
		return(i2c_error_device_error_3);

	memcpy(values, &i2cbuffer[2], length);

	return(i2c_error_ok);
}

static value_t sensor_am2321_cached_temperature;
static value_t sensor_am2321_cached_humidity;

irom static i2c_error_t sensor_am2321_read(int address, value_t *value, bool_t request_humidity)
{
	i2c_error_t	error;
	uint8_t		values[4];

	//	0x00	start address: humidity (16 bits), temperature (16 bits)
	//	0x04	length

	if((error = sensor_am2321_read_registers(address, 0x00, 0x04, values)) == i2c_error_ok)
	{
		sensor_am2321_cached_humidity.raw = (values[0] << 8) | values[1];
		sensor_am2321_cached_humidity.cooked = sensor_am2321_cached_humidity.raw / 10.0;

		sensor_am2321_cached_temperature.raw = (values[2] << 8) | values[3];
		sensor_am2321_cached_temperature.cooked = sensor_am2321_cached_temperature.raw / 10.0;
	}

	if(request_humidity)
		*value = sensor_am2321_cached_humidity;
	else
		*value = sensor_am2321_cached_temperature;

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_am2321_temp_read(int bus, const device_table_entry_t *entry, value_t *value)
{
	return(sensor_am2321_read(entry->address, value, false));
}

irom static i2c_error_t sensor_am2321_hum_read(int bus, const device_table_entry_t *entry, value_t *value)
{
	return(sensor_am2321_read(entry->address, value, true));
}

irom static i2c_error_t sensor_am2321_temp_init(int bus, const device_table_entry_t *entry)
{
	i2c_error_t	error;
	uint8_t		values[2];

	//	0x08	start address: device id
	//	0x02	length

	if((error = sensor_am2321_read_registers(entry->address, 0x08, 0x02, values)) != i2c_error_ok)
		return(error);

	// doesn't work on all models
	//if((values[0] != 0x32) || (values[1] != 0x31))
		//return(i2c_error_address_nak);

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_am2321_hum_init(int bus, const device_table_entry_t *entry)
{
	if(!i2c_sensor_detected(bus, i2c_sensor_am2321_temperature))
		return(i2c_error_address_nak);

	return(i2c_error_ok);
}

irom static i2c_error_t veml6070_read(unsigned int *rv)
{
	i2c_error_t error;
	uint8_t i2cbuffer[2];

	if((error = i2c_receive(0x39, 1, &i2cbuffer[0])) != i2c_error_ok)
		return(error);

	if((error = i2c_receive(0x38, 1, &i2cbuffer[1])) != i2c_error_ok)
		return(error);

	*rv = (i2cbuffer[0] << 8) | (i2cbuffer[1]);

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_veml6070_init(int bus, const device_table_entry_t *entry)
{
	i2c_error_t error;
	unsigned int rv;

	if(i2c_sensor_detected(bus, i2c_sensor_tsl2550)) // 0x39
		return(i2c_error_device_error_1);

	if(i2c_sensor_detected(bus, i2c_sensor_tsl2560)) // 0x39
		return(i2c_error_device_error_1);

	if((error = veml6070_read(&rv)) != i2c_error_ok)
		return(error);

	if((error = i2c_send_1(0x38, 0x0e)) != i2c_error_ok)
		return(error);

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_veml6070_read(int bus, const device_table_entry_t *entry, value_t *value)
{
	unsigned int rv;
	i2c_error_t error;

	if((error = veml6070_read(&rv)) != i2c_error_ok)
		return(error);

	value->raw = rv;
	value->cooked = rv * 10.0 / 2000.0; // FIXME

	return(i2c_error_ok);
}

enum
{
	si114x_part_id = 0x00,
	si114x_rev_id = 0x01,
	si114x_seq_id = 0x02,
	si114x_int_cfg = 0x03,
	si114x_irq_enable = 0x04,
	si114x_hw_key = 0x07,
	si114x_meas_rate_low = 0x08,
	si114x_meas_rate_high = 0x09,
	si114x_ucoef_0 = 0x13,
	si114x_ucoef_1 = 0x14,
	si114x_ucoef_2 = 0x15,
	si114x_ucoef_3 = 0x16,
	si114x_param_wr = 0x17,
	si114x_command = 0x18,
	si114x_response = 0x20,
	si114x_irq_status = 0x21,
	si114x_als_vis_data_low = 0x22,
	si114x_als_vis_data_high = 0x23,
	si114x_als_ir_data_low = 0x24,
	si114x_als_ir_data_high = 0x25,
	si114x_ps1_data_low = 0x26,
	si114x_ps1_data_high = 0x27,
	si114x_ps2_data_low = 0x28,
	si114x_ps2_data_high = 0x29,
	si114x_ps3_data_low = 0x2a,
	si114x_ps3_data_high = 0x2b,
	si114x_aux_data_low = 0x2c,
	si114x_aux_data_high = 0x2d,
	si114x_param_rd = 0x2e,
	si114x_chip_stat = 0x30,
	si114x_ana_in_key0 = 0x3b,
	si114x_ana_in_key1 = 0x3c,
	si114x_ana_in_key2 = 0x3d,
	si114x_ana_in_key3 = 0x3e,

	si114x_chlist = 0x01,
	si114x_psled12_select = 0x02,
	si114x_psled3_select = 0x03,
	si114x_ps_encoding = 0x05,
	si114x_als_encoding = 0x06,
	si114x_ps1_adcmux = 0x07,
	si114x_ps2_adcmux = 0x08,
	si114x_ps3_adcmux = 0x09,
	si114x_ps_adc_counter = 0x0a,
	si114x_ps_adc_gain = 0x0b,
	si114x_ps_adc_misc = 0x0c,
	si114x_als_ir_adcmux = 0x0e,
	si114x_aux_adcmux = 0x0f,
	si114x_als_vis_adc_counter = 0x10,
	si114x_als_vis_adc_gain = 0x11,
	si114x_als_vis_adc_misc = 0x12,
	si114x_als_ir_adc_counter = 0x1d,
	si114x_als_ir_adc_gain = 0x1e,
	si114x_als_ir_adc_misc = 0x1f,

	si114x_command_nop = 0x00,
	si114x_command_reset = 0x01,
	si114x_command_busaddr = 0x02,
	si114x_command_psforce = 0x05,
	si114x_command_alsforce = 0x06,
	si114x_command_psalsforce = 0x07,
	si114x_command_pspause = 0x09,
	si114x_command_alspause = 0x0a,
	si114x_command_psalspause = 0x0b,
	si114x_command_psauto = 0x0d,
	si114x_command_alsauto = 0x0e,
	si114x_command_psalsauto = 0x0f,
	si114x_command_get_cal = 0x12,
	si114x_command_param_query = 0x80,
	si114x_command_param_set = 0xa0,

	si114x_attempt_count = 16,

	si114x_measure_delay = 16384,
};

irom static i2c_error_t si114x_read_register(unsigned int reg, unsigned int *value)
{
	i2c_error_t error;
	uint8_t i2c_buffer[1];

	if((error = i2c_send_1(0x60, reg)) != i2c_error_ok)
		return(error);

	if((error = i2c_receive(0x60, 1, i2c_buffer)) != i2c_error_ok)
		return(error);

	*value = i2c_buffer[0];

	return(i2c_error_ok);
}

irom static i2c_error_t si114x_write_register(unsigned int reg, unsigned int value)
{
	i2c_error_t error;

	if((error = i2c_send_2(0x60, (uint8_t)reg, (uint8_t)value)) != i2c_error_ok)
		return(error);

	return(i2c_error_ok);
}

irom static i2c_error_t si114x_wait_idle(void)
{
	unsigned int attempt, value;
	i2c_error_t error;

	for(attempt = si114x_attempt_count; attempt > 0; attempt--)
	{
		if((error = si114x_read_register(si114x_chip_stat, &value)) != i2c_error_ok)
			return(error);

		if(value == 1)
			return(i2c_error_ok);

		msleep(1);
	}

	return(i2c_error_device_error_1);
}

irom static i2c_error_t si114x_sendcmd(unsigned int command, unsigned int *response)
{
	unsigned int first_response, attempt;
	i2c_error_t error;

	if((error = si114x_read_register(si114x_response, &first_response)) != i2c_error_ok)
		return(error);

	for(attempt = si114x_attempt_count; attempt > 0; attempt--)
	{
		if((error = si114x_wait_idle()) != i2c_error_ok)
			return(error);

		if(command == si114x_command_nop)
			break;

		if((error = si114x_read_register(si114x_response, response)) != i2c_error_ok)
			return(error);

		if(*response == first_response)
			break;

		first_response = *response;

		msleep(1);
	}

	if(attempt == 0)
		return(i2c_error_device_error_1);

	if((error = si114x_write_register(si114x_command, command)) != i2c_error_ok)
		return(error);

	for(attempt = si114x_attempt_count; attempt > 0; attempt--)
	{
		if(command == si114x_command_nop)
			break;

		if((error = si114x_read_register(si114x_response, response)) != i2c_error_ok)
			return(error);

		if(*response != first_response)
			break;

		msleep(1);
	}

	if(attempt == 0)
		return(i2c_error_device_error_2);

	return(i2c_error_ok);
}

irom static i2c_error_t si114x_reset(void)
{
	i2c_error_t error;

	msleep(25);

	if((error = si114x_write_register(si114x_command, si114x_command_reset)) != i2c_error_ok)
		return(error);

	msleep(20);

	if((error = si114x_write_register(si114x_hw_key, 0x17)) != i2c_error_ok)
		return(error);

	return(i2c_error_ok);
}

#if 0 // unused
irom static i2c_error_t si114x_get_param(unsigned int param, unsigned int *value)
{
	i2c_error_t error;
	unsigned int response;

	if((error = si114x_sendcmd(si114x_command_param_query | (param & 0x1f), &response)) != i2c_error_ok)
		return(error);

	if((error = si114x_read_register(si114x_param_rd, value)) != i2c_error_ok)
		return(error);

	return(i2c_error_ok);
}
#endif

irom static i2c_error_t si114x_set_param(unsigned int param, unsigned int value)
{
	i2c_error_t error;
	unsigned int first_response, response;

	if((error = si114x_wait_idle()) != i2c_error_ok)
		return(error);

	if((error = si114x_read_register(si114x_response, &first_response)) != i2c_error_ok)
		return(error);

	if((error = si114x_write_register(si114x_param_wr, value)) != i2c_error_ok)
		return(error);

	if((error = si114x_sendcmd(si114x_command_param_set | (param & 0x1f), &response)) != i2c_error_ok)
		return(error);

	if((response & 0xf0) == 0b10000000) // invalid setting
	{
		si114x_sendcmd(si114x_command_nop, /*dummy*/&response);
		return(i2c_error_device_error_1);
	}

	if((response & 0xf0)) // other error (overflow)
		si114x_sendcmd(si114x_command_nop, /*dummy*/&response);

	return(i2c_error_ok);
}

irom static i2c_error_t si114x_startstop(bool_t startstop)
{
	i2c_error_t error;
	unsigned int attempt1, attempt2, value, response;

	for(attempt1 = si114x_attempt_count; attempt1 > 0; attempt1--)
	{
		for(attempt2 = si114x_attempt_count; attempt2 > 0; attempt2--)
		{
			if((error = si114x_read_register(si114x_response, &value)) != i2c_error_ok)
				return(error);

			if(value == 0)
				break;

			if((error = si114x_sendcmd(si114x_command_nop, /*dummy*/&response)) != i2c_error_ok)
				return(error);

			msleep(1);
		}

		if(attempt2 == 0)
			return(i2c_error_device_error_5);

		if((error = si114x_sendcmd(startstop ? si114x_command_psalsauto : si114x_command_psalspause, /*dummy*/&response)) != i2c_error_ok)
			return(error);

		for(attempt2 = si114x_attempt_count; attempt2 > 0; attempt2--)
		{
			if((error = si114x_read_register(si114x_response, &value)) != i2c_error_ok)
				return(error);

			if(value != 0)
				break;

			msleep(1);
		}

		if(attempt2 == 0)
			return(i2c_error_device_error_5);

		if(value == 1)
			break;
	}

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_si114x_visible_light_init(int bus, const device_table_entry_t *entry)
{
	i2c_error_t error;
	unsigned int value;

	if((error = si114x_read_register(si114x_part_id, &value)) != i2c_error_ok)
		return(error);

	if((value != 0x45) && (value != 0x46) && (value != 0x47))
		return(i2c_error_device_error_1);

	if((error = si114x_startstop(false)) != i2c_error_ok)
		return(error);

	if((error = si114x_reset()) != i2c_error_ok)
		return(error);

	if((error = si114x_write_register(si114x_int_cfg, 0x00)) != i2c_error_ok)
		return(error);

	if((error = si114x_write_register(si114x_irq_enable, 0x00)) != i2c_error_ok)
		return(error);

	if((error = si114x_write_register(si114x_hw_key, 0x17)) != i2c_error_ok)
		return(error);

	if((error = si114x_write_register(si114x_meas_rate_low, (si114x_measure_delay & 0x00ff) >> 0)) != i2c_error_ok)
		return(error);

	if((error = si114x_write_register(si114x_meas_rate_high, (si114x_measure_delay & 0xff00) >> 8)) != i2c_error_ok)
		return(error);

	// default UCOEF values for UV measurements

	if((error = si114x_write_register(si114x_ucoef_0, 0x7b)) != i2c_error_ok)
		return(error);

	if((error = si114x_write_register(si114x_ucoef_1, 0x6b)) != i2c_error_ok)
		return(error);

	if((error = si114x_write_register(si114x_ucoef_2, 0x01)) != i2c_error_ok)
		return(error);

	if((error = si114x_write_register(si114x_ucoef_3, 0x00)) != i2c_error_ok)
		return(error);

	// sequencer parameters

	if((error = si114x_set_param(si114x_psled12_select, 0x00)) != i2c_error_ok) // leds PS1 and PS2 OFF
		return(error);

	if((error = si114x_set_param(si114x_psled3_select, 0x00)) != i2c_error_ok) // leds PS3 OFF
		return(error);

	if((error = si114x_set_param(si114x_als_encoding, 0x00)) != i2c_error_ok) // ADC low sensitivity visible and IR
		return(error);

	if((error = si114x_set_param(si114x_als_ir_adcmux, 0x00)) != i2c_error_ok) // select small IR photodiode for IR measurements
		return(error);

	if((error = si114x_set_param(si114x_aux_adcmux, 0x65)) != i2c_error_ok) // set AUX ADC to GND
		return(error);

	if((error = si114x_set_param(si114x_chlist, 0b10110000)) != i2c_error_ok) // start automatic measurements
		return(error);

	if((error = si114x_startstop(true)) != i2c_error_ok)
		return(error);

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_si114x_visible_light_read(int bus, const device_table_entry_t *entry, value_t *value)
{
	i2c_error_t error;
	unsigned int low, high;

	if((error = si114x_read_register(si114x_als_vis_data_low, &low)) != i2c_error_ok)
		return(error);

	if((error = si114x_read_register(si114x_als_vis_data_high, &high)) != i2c_error_ok)
		return(error);

	value->raw = value->cooked = (high << 8) | low;
	value->cooked /= 0.282; // lx per ADC count for sunlight

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_si114x_infrared_init(int bus, const device_table_entry_t *entry)
{
	if(i2c_sensor_detected(bus, i2c_sensor_si114x_visible_light))
		return(i2c_error_ok);

	return(i2c_error_address_nak);
}

irom static i2c_error_t sensor_si114x_infrared_read(int bus, const device_table_entry_t *entry, value_t *value)
{
	i2c_error_t error;
	unsigned int low, high;

	if((error = si114x_read_register(si114x_als_ir_data_low, &low)) != i2c_error_ok)
		return(error);

	if((error = si114x_read_register(si114x_als_ir_data_high, &high)) != i2c_error_ok)
		return(error);

	value->raw = value->cooked = (high << 8) | low;
	value->cooked /= 2.44; // lx per ADC count for sunlight

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_si114x_ultraviolet_init(int bus, const device_table_entry_t *entry)
{
	if(i2c_sensor_detected(bus, i2c_sensor_si114x_visible_light))
		return(i2c_error_ok);

	return(i2c_error_address_nak);
}

irom static i2c_error_t sensor_si114x_ultraviolet_read(int bus, const device_table_entry_t *entry, value_t *value)
{
	i2c_error_t error;
	unsigned int low, high;

	if((error = si114x_read_register(si114x_aux_data_low, &low)) != i2c_error_ok)
		return(error);

	if((error = si114x_read_register(si114x_aux_data_high, &high)) != i2c_error_ok)
		return(error);

	value->raw = value->cooked = (high << 8) | low;
	value->cooked *= 0.01;

	return(i2c_error_ok);
}

static const device_table_entry_t device_table[] =
{
	{
		i2c_sensor_digipicco_temperature, 0x78,
		"digipicco", "temperature", "C", 1,
		sensor_digipicco_temp_init,
		sensor_digipicco_temp_read
	},
	{
		i2c_sensor_digipicco_humidity, 0x78,
		"digipicco", "humidity", "%", 0,
		sensor_digipicco_hum_init,
		sensor_digipicco_hum_read
	},
	{
		i2c_sensor_lm75_0, 0x48,
		"lm75 compatible@0x48", "temperature", "C", 2,
		sensor_lm75_init,
		sensor_lm75_read
	},
	{
		i2c_sensor_lm75_1, 0x49,
		"lm75 compatible@0x49", "temperature", "C", 2,
		sensor_lm75_init,
		sensor_lm75_read
	},
	{
		i2c_sensor_lm75_2, 0x4a,
		"lm75 compatible@0x4a", "temperature", "C", 2,
		sensor_lm75_init,
		sensor_lm75_read
	},
	{
		i2c_sensor_lm75_3, 0x4b,
		"lm75 compatible@0x4b", "temperature", "C", 2,
		sensor_lm75_init,
		sensor_lm75_read
	},
	{
		i2c_sensor_ds1631_6, 0x4e,
		"ds1621/ds1631/ds1731@0x4e", "temperature", "C", 2,
		sensor_ds1631_init,
		sensor_ds1631_read
	},
	{
		i2c_sensor_lm75_7, 0x4f,
		"lm75 compatible@0x4f", "temperature", "C", 2,
		sensor_lm75_init,
		sensor_lm75_read
	},
	{
		i2c_sensor_bmp085_temperature, 0x77,
		"bmp085", "temperature", "C", 1,
		sensor_bmp085_init_temp,
		sensor_bmp085_read_temp
	},
	{
		i2c_sensor_bmp085_airpressure, 0x77,
		"bmp085", "pressure", "hPa", 0,
		sensor_bmp085_init_pressure,
		sensor_bmp085_read_pressure
	},
	{
		i2c_sensor_tsl2560, 0x39,
		"tsl2560/tsl2561", "light", "", 2,
		sensor_tsl2560_init,
		sensor_tsl2560_read,
	},
	{
		i2c_sensor_tsl2550, 0x39,
		"tsl2550", "light", "", 2,
		sensor_tsl2550_init,
		sensor_tsl2550_read
	},
	{
		i2c_sensor_bh1750, 0x23,
		"bh1750", "light", "", 2,
		sensor_bh1750_init,
		sensor_bh1750_read
	},
	{
		i2c_sensor_htu21_temperature, 0x40,
		"htu21", "temperature", "C", 1,
		sensor_htu21_temp_init,
		sensor_htu21_temp_read
	},
	{
		i2c_sensor_htu21_humidity, 0x40,
		"htu21", "humidity", "%", 0,
		sensor_htu21_hum_init,
		sensor_htu21_hum_read
	},
	{
		i2c_sensor_am2321_temperature, 0x5c,
		"am2321", "temperature", "C", 1,
		sensor_am2321_temp_init,
		sensor_am2321_temp_read
	},
	{
		i2c_sensor_am2321_humidity, 0x5c,
		"am2321", "humidity", "%", 0,
		sensor_am2321_hum_init,
		sensor_am2321_hum_read
	},
	{
		i2c_sensor_veml6070, 0x38,
		"veml6070", "UV", "", 0,
		sensor_veml6070_init,
		sensor_veml6070_read
	},
	{
		i2c_sensor_si114x_visible_light, 0x60,
		"si114x", "light", "", 1,
		sensor_si114x_visible_light_init,
		sensor_si114x_visible_light_read,
	},
	{
		i2c_sensor_si114x_infrared, 0x60,
		"si114x", "ir", "", 1,
		sensor_si114x_infrared_init,
		sensor_si114x_infrared_read,
	},
	{
		i2c_sensor_si114x_ultraviolet, 0x60,
		"si114x", "uv", "", 1,
		sensor_si114x_ultraviolet_init,
		sensor_si114x_ultraviolet_read,
	},
};

irom i2c_error_t i2c_sensor_init(int bus, i2c_sensor_t sensor)
{
	const device_table_entry_t *entry;
	i2c_error_t error;

	if(sensor >= i2c_sensor_size)
		return(i2c_error_device_error_4);

	entry = &device_table[sensor];

	if(!entry->init_fn)
	{
		device_data[entry->id].detected &= ~(1 << bus);
		return(i2c_error_device_error_5);
	}

	if((error = i2c_select_bus(bus)) != i2c_error_ok)
	{
		device_data[entry->id].detected &= ~(1 << bus);
		i2c_select_bus(0);
		return(error);
	}

	if((error = entry->init_fn(bus, entry)) != i2c_error_ok)
	{
		device_data[entry->id].detected &= ~(1 << bus);
		i2c_select_bus(0);
		return(error);
	}

	device_data[entry->id].detected |= 1 << bus;
	i2c_select_bus(0);
	return(i2c_error_ok);
}

irom void i2c_sensor_init_all(void)
{
	int bus;
	i2c_sensor_t current;

	for(bus = 0; bus < i2c_busses; bus++)
		for(current = 0; current < i2c_sensor_size; current++)
			if((bus == 0) || !(device_data[current].detected & (1 << 0)))
				i2c_sensor_init(bus, current);
}

irom bool_t i2c_sensor_read(string_t *dst, int bus, i2c_sensor_t sensor, bool_t verbose)
{
	const device_table_entry_t *entry;
	i2c_error_t error;
	value_t value;
	int current;
	int int_factor, int_offset;
	double extracooked;

	for(current = 0; current < i2c_sensor_size; current++)
	{
		entry = &device_table[current];

		if(sensor == entry->id)
			break;
	}

	if(current >= i2c_sensor_size)
	{
		string_format(dst, "i2c sensor read: sensor #%u unknown\n", sensor);
		return(false);
	}

	if((error = i2c_select_bus(bus)) != i2c_error_ok)
	{
		string_format(dst, "i2c sensor read: select bus #%u error", bus);
		i2c_error_format_string(dst, error);
		i2c_select_bus(0);
		return(false);
	}

	error = i2c_error_ok;

	string_format(dst, "%s sensor %u/%02u: %s, %s: ", device_data[sensor].detected ? "+" : " ", bus, sensor, entry->name, entry->type);

	if((error = entry->read_fn(bus, entry, &value)) == i2c_error_ok)
	{
		if(!config_get_int("i2s.%u.%u.factor", bus, sensor, &int_factor))
			int_factor = 1000;

		if(!config_get_int("i2s.%u.%u.offset", bus, sensor, &int_offset))
			int_offset = 0;

		extracooked = (value.cooked * int_factor / 1000.0) + (int_offset / 1000.0);

		string_cat(dst, "[");
		string_double(dst, extracooked, entry->precision, 1e10);
		string_cat(dst, "]");

		string_format(dst, " %s", entry->unity);

		if(verbose)
		{
			string_cat(dst, " (uncalibrated: ");
			string_double(dst, value.cooked, entry->precision, 1e10);
			string_cat(dst, ", raw: ");
			string_double(dst, value.raw, 0, 1e10);
			string_cat(dst, ")");
		}
	}
	else
	{
		if(verbose)
		{
			string_cat(dst, "error");
			i2c_error_format_string(dst, error);
		}
		else
			string_cat(dst, "error");
	}

	if(verbose)
	{
		if(!config_get_int("i2s.%u.%u.factor", bus, sensor, &int_factor))
			int_factor = 1000;

		if(!config_get_int("i2s.%u.%u.offset", bus, sensor, &int_offset))
			int_offset = 0;

		string_cat(dst, ", calibration: factor=");
		string_double(dst, int_factor / 1000.0, 4, 1e10);
		string_cat(dst, ", offset=");
		string_double(dst, int_offset / 1000.0, 4, 1e10);
	}

	i2c_select_bus(0);
	return(true);
}

irom attr_pure bool_t i2c_sensor_detected(int bus, i2c_sensor_t sensor)
{
	if(sensor > i2c_sensor_size)
		return(false);

	return(!!(device_data[sensor].detected & (1 << bus)));
}
