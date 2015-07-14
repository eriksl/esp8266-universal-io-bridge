#include "i2c_sensor.h"

#include "util.h"
#include "config.h"

#include <math.h>

typedef struct
{
	double raw;
	double cooked;
} value_t;

typedef struct
{
	unsigned int detected:1;
} device_data_t;

typedef struct
{
	i2c_sensor_t id;
	const char *name;
	const char *type;
	const char *unity;
	uint8_t precision;
	i2c_error_t (* const init_fn)(void);
	i2c_error_t (* const read_fn)(value_t *);
} device_table_t;

device_data_t device_data[i2c_sensor_size];

ICACHE_FLASH_ATTR static i2c_error_t sensor_digipicco_read_temp(value_t *value)
{
	i2c_error_t error;
	uint8_t	i2cbuffer[4];

	if((error = i2c_receive(0x78, 4, i2cbuffer)) != i2c_error_ok)
		return(error);

	value->raw = ((uint16_t)i2cbuffer[2] << 8) | (uint16_t)i2cbuffer[3];
	value->cooked = ((value->raw * 165.0) / 32767) - 40.5;

	return(i2c_error_ok);
}

ICACHE_FLASH_ATTR static i2c_error_t sensor_digipicco_read_hum(value_t *value)
{
	i2c_error_t error;
	uint8_t	i2cbuffer[4];

	if((error = i2c_receive(0x78, 4, i2cbuffer)) != i2c_error_ok)
		return(error);

	value->raw = ((uint16_t)i2cbuffer[0] << 8) | (uint16_t)i2cbuffer[1];
	value->cooked = (value->raw * 100.0) / 32768.0;

	return(i2c_error_ok);
}

ICACHE_FLASH_ATTR static i2c_error_t sensor_ds1631_read(value_t *value)
{
	uint8_t i2cbuffer[2];
	i2c_error_t error;
	uint32_t raw;

	//	0xac	select config register
	//	0x0c	r0=r1=1, max resolution, other bits zero

	if((error = i2c_send_2(0x48, 0xac, 0x0c)) != i2c_error_ok)
		return(error);

	// start conversion (if not started already)

	if((error = i2c_send_1(0x48, 0x51)) != i2c_error_ok)
		return(error);

	// read temperature

	if((error = i2c_send_1(0x48, 0xaa)) != i2c_error_ok)
		return(error);

	if((error = i2c_receive(0x48, 2, i2cbuffer)) != i2c_error_ok)
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

ICACHE_FLASH_ATTR static i2c_error_t sensor_lm75_init(void)
{
	uint8_t i2cbuffer[4];
	i2c_error_t error;

	// 0x01		select config register
	// 0x60		set all defaults, operation is not shutdown
	// 			specific for tmp275 variant select, high-res operation

	if((error = i2c_send_2(0x48, 0x01, 0x60)) != i2c_error_ok)
		return(error);

	if((error = i2c_receive(0x48, 1, i2cbuffer)) != i2c_error_ok)
		return(error);

	if(i2cbuffer[0] != 0x60)
		return(i2c_error_device_error_1);

	// 0x03	select overtemperature register

	if((error = i2c_send_3(0x48, 0x03, 0xff, 0xff)) != i2c_error_ok)
		return(error);

	if((error = i2c_receive(0x48, 2, i2cbuffer)) != i2c_error_ok)
		return(error);

	if((i2cbuffer[0] != 0xff) || ((i2cbuffer[1] & 0x0f) != 0x00))
		return(i2c_error_device_error_2);

	// 0x03	select overtemperature register

	if((error = i2c_send_3(0x48, 0x03, 0x00, 0x00)) != i2c_error_ok)
		return(error);

	if((error = i2c_receive(0x48, 2, i2cbuffer)) != i2c_error_ok)
		return(error);

	if((i2cbuffer[0] != 0x00) || (i2cbuffer[1] != 0x00))
		return(i2c_error_device_error_3);

	// select temperature register

	if((error = i2c_send_1(0x48, 0x00)) != i2c_error_ok)
		return(error);

	return(i2c_error_ok);
}

ICACHE_FLASH_ATTR static i2c_error_t sensor_lm75_read(value_t *value)
{
	uint8_t i2cbuffer[2];
	i2c_error_t error;
	uint32_t raw;

	if((error = i2c_receive(0x48, 2, i2cbuffer)) != i2c_error_ok)
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

ICACHE_FLASH_ATTR static i2c_error_t bmp085_write(uint8_t reg, uint8_t value)
{
	i2c_error_t error;

	if((error = i2c_send_2(0x77, reg, value)) != i2c_error_ok)
		return(error);

	return(0);
}

ICACHE_FLASH_ATTR static i2c_error_t bmp085_read(uint8_t reg, uint16_t *value)
{
	i2c_error_t error;
	uint8_t i2cbuffer[2];

	if((error = i2c_send_1(0x77, reg)) != i2c_error_ok)
		return(error);

	if((error = i2c_receive(0x77, 2, i2cbuffer)) != i2c_error_ok)
		return(error);

	*value = ((uint16_t)i2cbuffer[0] << 8) | (uint16_t)i2cbuffer[1];

	return(0);
}

ICACHE_FLASH_ATTR static i2c_error_t bmp085_read_long(uint8_t reg, uint32_t *value)
{
	i2c_error_t error;
	uint8_t i2cbuffer[4];

	if((error = i2c_send_1(0x77, reg)) != i2c_error_ok)
		return(error);

	if((error = i2c_receive(0x77, 3, i2cbuffer)) != i2c_error_ok)
		return(error);

	*value = ((uint32_t)i2cbuffer[0] << 16) | ((uint32_t)i2cbuffer[1] << 8) | (uint32_t)i2cbuffer[2];

	return(0);
}

ICACHE_FLASH_ATTR static i2c_error_t sensor_read_bmp085(double *temp, double *temp_raw, double *pressure, double *pressure_raw)
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

	if((error = bmp085_read(0xaa, (uint16_t *)&ac1)) != i2c_error_ok)
		return(error);

	if((error = bmp085_read(0xac, (uint16_t *)&ac2)) != i2c_error_ok)
		return(error);

	if((error = bmp085_read(0xae, (uint16_t *)&ac3)) != i2c_error_ok)
		return(error);

	if((error = bmp085_read(0xb0, &ac4)) != i2c_error_ok)
		return(error);

	if((error = bmp085_read(0xb2, &ac5)) != i2c_error_ok)
		return(error);

	if((error = bmp085_read(0xb4, &ac6)) != i2c_error_ok)
		return(error);

	if((error = bmp085_read(0xb6, (uint16_t *)&b1)) != i2c_error_ok)
		return(error);

	if((error = bmp085_read(0xb8, (uint16_t *)&b2)) != i2c_error_ok)
		return(error);

	if((error = bmp085_read(0xbc, (uint16_t *)&mc)) != i2c_error_ok)
		return(error);

	if((error = bmp085_read(0xbe, (uint16_t *)&md)) != i2c_error_ok)
		return(error);

	if((error = bmp085_write(0xf4, 0x2e)) != i2c_error_ok) // set cmd = 0x2e = start temperature measurement
		return(error);

	msleep(5);

	if((error = bmp085_read(0xf6, &ut)) != i2c_error_ok) // select result 0xf6+0xf7
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
	x2 = ((int32_t)mc << 11) / (x1 + (int32_t)md);
	b5 = x1 + x2;

	*temp_raw	= ut;
	*temp		= ((((double)b5 + 8) / 16) / 10);

	if((error = bmp085_write(0xf4, 0x34 | (oss << 6))) != i2c_error_ok) // set cmd = 0x34 = start air pressure measurement
		return(error);

	msleep(20);

	up = 0;

	if((error = bmp085_read_long(0xf6, &up)) != i2c_error_ok) // select result 0xf6+0xf7+f8
		return(error);
#if 0
			up	= 23843;
#endif

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

ICACHE_FLASH_ATTR static i2c_error_t sensor_bmp085_read_temp(value_t *value)
{
	double temp, temp_raw, pressure, pressure_raw;
	i2c_error_t error;

	if((error = sensor_read_bmp085(&temp, &temp_raw, &pressure, &pressure_raw)) != i2c_error_ok)
		return(error);

	value->raw = temp_raw;
	value->cooked = temp;

	return(i2c_error_ok);
}

ICACHE_FLASH_ATTR static i2c_error_t sensor_bmp085_read_pressure(value_t *value)
{
	double temp, temp_raw, pressure, pressure_raw;
	i2c_error_t error;

	if((error = sensor_read_bmp085(&temp, &temp_raw, &pressure, &pressure_raw)) != i2c_error_ok)
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

ICACHE_FLASH_ATTR static i2c_error_t tsl2560_write(uint8_t reg, uint8_t value)
{
	i2c_error_t error;

	// 0xc0	write byte

	if((error = i2c_send_2(0x39, 0xc0 | reg, value)) != i2c_error_ok)
		return(error);

	return(i2c_error_ok);
}

ICACHE_FLASH_ATTR static i2c_error_t tsl2560_read(uint8_t reg, uint8_t *byte)
{
	i2c_error_t error;

	// 0xc0	read byte

	if((error = i2c_send_1(0x39, 0xc0 | reg)) != i2c_error_ok)
		return(error);

	if((error = i2c_receive(0x39 , 1, byte)) != i2c_error_ok)
		return(error);

	return(i2c_error_ok);
}

ICACHE_FLASH_ATTR static i2c_error_t tsl2560_write_check(uint8_t reg, uint8_t value)
{
	i2c_error_t error;
	uint8_t rv;

	if((error = tsl2560_write(reg, value)) != i2c_error_ok)
		return(error);

	if((error = tsl2560_read(reg, &rv)) != i2c_error_ok)
		return(error);

	if(value != rv)
		return(i2c_error_device_error_1);

	return(i2c_error_ok);
}

ICACHE_FLASH_ATTR static i2c_error_t tsl2560_read_block(uint8_t reg, uint8_t *values)
{
	i2c_error_t error;

	// 0xd0	read block

	if((error = i2c_send_1(0x39, 0xd0 | reg)) != i2c_error_ok)
		return(error);

	if((error = i2c_receive(0x39 , 4, values)) != i2c_error_ok)
		return(error);

	return(i2c_error_ok);
}

ICACHE_FLASH_ATTR static i2c_error_t sensor_tsl2560_init(void)
{
	i2c_error_t error;

	if((error = tsl2560_write_check(0x00, 0x03)) != i2c_error_ok)	// power up
		return(error);

	if((error = tsl2560_write_check(0x06, 0x00)) != i2c_error_ok)	// disable interrupts
		return(error);

	if((error = tsl2560_write_check(0x01, 0x11)) != i2c_error_ok)	// start continuous sampling every 100 ms, high gain = 16x
		return(error);

	return(i2c_error_ok);
}

ICACHE_FLASH_ATTR static i2c_error_t sensor_tsl2560_read(value_t *value)
{
	uint8_t	i2cbuffer[4];
	i2c_error_t	error;
	uint32_t ch0r, ch1r;
	double ratio, ch0, ch1;
	const tsl2560_lookup_t *entry;
	uint8_t current;

	if((error = tsl2560_read_block(0x0c, i2cbuffer)) != i2c_error_ok)
		return(error);

	ch0r = i2cbuffer[0] | (i2cbuffer[1] << 8);
	ch1r = i2cbuffer[2] | (i2cbuffer[3] << 8);

	value->raw = (double)ch0r + ((double)ch1r * 10000);

	// high sensitivity = 100 ms integration time, 16X amplification
	// low  sensitivity =  13 ms integration time,  1X amplification

	if(config_get_flag(config_flag_tsl_high_sens))
	{
		ch0 = ch0r * 322 / 81 * 1;
		ch1 = ch1r * 322 / 81 * 1;
	}
	else
	{
		ch0 = ch0r * 322 / 11 * 16;
		ch1 = ch1r * 322 / 11 * 16;
	}

	if(ch0 != 0)
		ratio = ch1 / ch0;
	else
		ratio = 0;

	for(current = 0;; current++)
	{
		entry = &tsl2560_lookup[current];

		if((entry->ratio_top == 0) || (entry->ch0_factor == 0) || (entry->ch1_factor == 0))
			break;

		if(ratio <= entry->ratio_top)
			break;
	}

	value->cooked = (ch0 * entry->ch0_factor) - (ch1 * entry->ch1_factor);

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

ICACHE_FLASH_ATTR static i2c_error_t sensor_tsl2550_rw(uint8_t in, uint8_t *out)
{
	i2c_error_t error;

	if((error = i2c_send_1(0x39, in)) != i2c_error_ok)
		return(error);

	if((error = i2c_receive(0x39, 1, out)) != i2c_error_ok)
		return(error);

	return(i2c_error_ok);
}

ICACHE_FLASH_ATTR static i2c_error_t sensor_tsl2550_write_check(uint8_t in, uint8_t compare)
{
	i2c_error_t error;
	uint8_t out;

	if((error = sensor_tsl2550_rw(in, &out)) != i2c_error_ok)
		return(error);

	if(out != compare)
		return(i2c_error_device_error_1);

	return(i2c_error_ok);
}

ICACHE_FLASH_ATTR static i2c_error_t sensor_tsl2550_init(void)
{
	i2c_error_t error;
	uint8_t sens_command;

	// tsl2550 power up

	if((error = sensor_tsl2550_write_check(0x03, 0x03)) != i2c_error_ok)
		return(error);

	// standard range / extended range

	if(config_get_flag(config_flag_tsl_high_sens))
		sens_command = 0x18;
	else
		sens_command = 0x1d;

	if((error = sensor_tsl2550_write_check(sens_command, 0x1b)) != i2c_error_ok)
		return(error);

	return(i2c_error_ok);
}

ICACHE_FLASH_ATTR static i2c_error_t sensor_tsl2550_read(value_t *value)
{
	i2c_error_t	error;
	uint8_t		ch0, ch1;
	uint8_t		attempt, ratio;

	error = i2c_error_ok;

	for(attempt = 16; attempt > 0; attempt--)
	{
		// read from channel 0

		if((error = sensor_tsl2550_rw(0x43, &ch0)) != i2c_error_ok)
			goto error;

		// read from channel 1

		if((error = sensor_tsl2550_rw(0x83, &ch1)) != i2c_error_ok)
			goto error;

		if((ch0 & 0x80) && (ch1 & 0x80))
			break;
error:
		msleep(10);
	}

	if(error != i2c_error_ok);
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

	if(!config_get_flag(config_flag_tsl_high_sens))
		value->cooked *= 5;

	return(i2c_error_ok);
}

ICACHE_FLASH_ATTR static i2c_error_t sensor_bh1750_init(void)
{
	i2c_error_t error;

	// power on

	if((error = i2c_send_1(0x23, 0x01)) != i2c_error_ok)
	{
		i2c_reset();
		return(error);
	}

	// reset

	if((error = i2c_send_1(0x23, 0x07)) != i2c_error_ok)
	{
		i2c_reset();
		return(error);
	}

	// start continuous sampling every 120 ms, high resolution = 0.42 Lx

	if((error = i2c_send_1(0x23, 0x11)) != i2c_error_ok)
	{
		i2c_reset();
		return(error);
	}

	return(i2c_error_ok);
}

ICACHE_FLASH_ATTR static i2c_error_t sensor_bh1750_read(value_t *value)
{
	i2c_error_t error;
	uint8_t	i2cbuffer[2];

	if((error = i2c_receive(0x23, 2, i2cbuffer)) != i2c_error_ok)
		return(error);

	value->raw		= ((uint16_t)i2cbuffer[0] << 8) | (uint16_t)i2cbuffer[1];
	value->cooked	= value->raw * 0.42;

	return(i2c_error_ok);
}

ICACHE_FLASH_ATTR static i2c_error_t htu21_crc(uint8_t length, const uint8_t *data)
{
	i2c_error_t outer, inner, testbit, crc;

	crc = 0;

	for(outer = 0; outer < length; outer++)
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

ICACHE_FLASH_ATTR static i2c_error_t sensor_htu21_read_temp(value_t *value)
{
	i2c_error_t error;
	uint8_t	i2cbuffer[4];
	uint8_t	crc1, crc2;

	// temperature measurement "hold master" mode

	if((error = i2c_send_1(0x40, 0xe3)) != i2c_error_ok)
		return(error);

	if((error = i2c_receive(0x40, 4, i2cbuffer)) != i2c_error_ok)
		return(error);

	value->raw = ((uint16_t)i2cbuffer[0] << 8) | (uint16_t)i2cbuffer[1];

	crc1 = i2cbuffer[2];
	crc2 = htu21_crc(2, &i2cbuffer[0]);

	if(crc1 != crc2)
	{
		value->cooked = -256;
		return(i2c_error_ok);
	}

	value->cooked = ((value->raw * 175.72) / 65536) - 46.85;

	return(i2c_error_ok);
}

ICACHE_FLASH_ATTR static i2c_error_t sensor_htu21_read_hum(value_t *value)
{
	i2c_error_t error;
	uint8_t	i2cbuffer[4];
	uint8_t	crc1, crc2;

	// humidity measurement "hold master" mode

	if((error = i2c_send_1(0x40, 0xe5)) != i2c_error_ok)
		return(error);

	if((error = i2c_receive(0x40, 4, i2cbuffer)) != i2c_error_ok)
		return(error);

	value->raw = ((uint16_t)i2cbuffer[0] << 8) | (uint16_t)i2cbuffer[1];

	crc1 = i2cbuffer[2];
	crc2 = htu21_crc(2, &i2cbuffer[0]);

	if(crc1 != crc2)
	{
		value->cooked = -1;
		return(i2c_error_ok);
	}

	value->cooked = ((value->raw * 125) / 65536) - 6;

	return(i2c_error_ok);
}

ICACHE_FLASH_ATTR static uint16_t am2321_crc(uint8_t length, const uint8_t *data)
{
	uint8_t		outer, inner, testbit;
	uint16_t	crc;

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

ICACHE_FLASH_ATTR static i2c_error_t sensor_am2321_read_temp(value_t *value)
{
	i2c_error_t	error;
	uint8_t		i2cbuffer[8];
	uint16_t	crc1, crc2;

#if 0
	if((error = twi_master_send_start()) != i2c_error_ok)
		return(error);

	twi_master_send_address(0x5c, 1);	// wakeup the device
	twi_master_send_stop_no_wait();		// by issueing an empty write
#else
	i2c_send_1(0x5c, 0);
#endif

	msleep(1);

	//	0x03	read registers
	//	0x02	start address
	//	0x02	length;

	if((error = i2c_send_3(0x5c, 0x03, 0x02, 0x02)) != i2c_error_ok)
		return(error);

	msleep(1);

	if((error = i2c_receive(0x5c, 6, i2cbuffer)) != i2c_error_ok)
		return(error);

	value->raw = (int16_t)(((uint16_t)i2cbuffer[2] << 8) | (uint16_t)i2cbuffer[3]);

	if((i2cbuffer[0] != 0x03) || (i2cbuffer[1] != 0x02))
	{
		value->cooked = -257;
		return(i2c_error_ok);
	}

	crc1 = ((uint16_t)i2cbuffer[5] << 8) | (uint16_t)i2cbuffer[4];
	crc2 = am2321_crc(4, &i2cbuffer[0]);

	if(crc1 != crc2)
	{
		value->cooked = -256;
		return(i2c_error_ok);
	}

	value->cooked = value->raw / 10.0;

	return(i2c_error_ok);
}

ICACHE_FLASH_ATTR static i2c_error_t sensor_am2321_read_hum(value_t *value)
{
	i2c_error_t	error;
	uint8_t		i2cbuffer[8];
	uint16_t	crc1, crc2;

#if 0
	if((error = twi_master_send_start()) != i2c_error_ok)
		return(error);

	twi_master_send_address(0x5c, 1);	// wakeup the device
	twi_master_send_stop_no_wait();		// by issueing an empty write
#else
	i2c_send_1(0x5c, 0);
#endif

	msleep(1);

	//	0x03	read registers
	//	0x00	start address
	//	0x02	length;

	if((error = i2c_send_3(0x5c, 0x03, 0x00, 0x02)) != i2c_error_ok)
		return(error);

	msleep(1);

	if((error = i2c_receive(0x5c, 6, i2cbuffer)) != i2c_error_ok)
		return(error);

	value->raw = (int16_t)(((uint16_t)i2cbuffer[2] << 8) | (uint16_t)i2cbuffer[3]);

	if((i2cbuffer[0] != 0x03) || (i2cbuffer[1] != 0x02))
	{
		value->cooked = -257;
		return(i2c_error_ok);
	}

	crc1 = ((uint16_t)i2cbuffer[5] << 8) | (uint16_t)i2cbuffer[4];
	crc2 = am2321_crc(4, &i2cbuffer[0]);

	if(crc1 != crc2)
	{
		value->cooked = -256;
		return(i2c_error_ok);
	}

	value->cooked = value->raw / 10.0;

	return(i2c_error_ok);
}

static const device_table_t device_table[] =
{
	{
		i2c_sensor_digipicco_temperature,
		"digipicco", "temperature", "C", 1,
		0,
		sensor_digipicco_read_temp
	},
	{
		i2c_sensor_digipicco_humidity,
		"digipicco", "humidity", "%", 0,
		0,
		sensor_digipicco_read_hum
	},
	{
		i2c_sensor_ds1631,
		"ds1621/ds1631/ds1731", "temperature", "C", 2,
		0,
		sensor_ds1631_read
	},
	{
		i2c_sensor_lm75,
		"lm75/tmp275", "temperature", "C", 1,
		sensor_lm75_init,
		sensor_lm75_read
	},
	{
		i2c_sensor_bmp085_temperature,
		"bmp085", "temperature", "C", 1,
		0,
		sensor_bmp085_read_temp
	},
	{
		i2c_sensor_bmp085_airpressure,
		"bmp085", "pressure", "hPa", 0,
		0,
		sensor_bmp085_read_pressure
	},
	{
		i2c_sensor_tsl2560,
		"tsl2560", "light", "Lux", 2,
		sensor_tsl2560_init,
		sensor_tsl2560_read,
	},
	{
		i2c_sensor_tsl2550,
		"tsl2550", "light", "Lux", 2,
		sensor_tsl2550_init,
		sensor_tsl2550_read
	},
	{
		i2c_sensor_bh1750,
		"bh1750", "light", "Lux", 2,
		sensor_bh1750_init,
		sensor_bh1750_read
	},
	{
		i2c_sensor_htu21_temperature,
		"htu21", "temperature", "C", 1,
		0,
		sensor_htu21_read_temp
	},
	{
		i2c_sensor_htu21_humidity,
		"htu21", "humidity", "%", 0,
		0,
		sensor_htu21_read_hum
	},
	{
		i2c_sensor_am2321_temperature,
		"am2321", "temperature", "C", 1,
		0,
		sensor_am2321_read_temp
	},
	{
		i2c_sensor_am2321_humidity,
		"am2321", "humidity", "%", 0,
		0,
		sensor_am2321_read_hum
	}
};

ICACHE_FLASH_ATTR void i2c_sensor_init(void)
{
	const device_table_t *entry;
	uint8_t current;

	for(current = 0; current < i2c_sensor_size; current++)
	{
		entry = &device_table[current];

		if(entry->init_fn && (entry->init_fn() == i2c_error_ok))
			device_data[entry->id].detected = true;
		else
		{
			device_data[entry->id].detected = false;
			i2c_reset();
		}
	}
}

ICACHE_FLASH_ATTR uint16_t i2c_sensor_read(i2c_sensor_t sensor, bool_t list, bool_t verbose,
		uint16_t size, char *dst)
{
	const device_table_t *entry;
	i2c_error_t error;
	value_t value;
	uint16_t length;
	char *orig_dst = dst;
	uint16_t current;

	for(current = 0; current < i2c_sensor_size; current++)
	{
		entry = &device_table[current];

		if(sensor == entry->id)
			break;
	}

	if(current >= i2c_sensor_size)
		return(snprintf(dst, size, "i2c sensor read: sensor #%d unknown\n", sensor));

	error = i2c_error_ok;

	if((device_data[sensor].detected || verbose) && ((error = entry->read_fn(&value)) == i2c_error_ok))
	{
		length = snprintf(dst, size, "sensor %d:%s: %s: ", sensor, entry->name, entry->type);
		dst += length;
		size -= length;

		length = snprintf(dst, size, "%s", "[");
		dst += length;
		size -= length;

		length = double_to_string(value.cooked, entry->precision, 1e10, size, dst);
		dst += length;
		size -= length;

		length = snprintf(dst, size, "%s", "] (raw: ");
		dst += length;
		size -= length;

		length = double_to_string(value.raw, 0, 1e10, size, dst);
		dst += length;
		size -= length;

		length = snprintf(dst, size, "%s", ")\n");
		dst += length;
		size -= length;
	}
	else
	{
		if(list)
		{
			length = snprintf(dst, size, "sensor %d:%s: %s: ", sensor, entry->name, entry->type);
			dst += length;
			size -= length;

			if(verbose)
				length = i2c_error_format_string("error", error, size, dst);
			else
				length = snprintf(dst, size, "%s", "not found");

			dst += length;
			size -= length;

			length = snprintf(dst, size, "%s", "\n");
			dst += length;
			size -= length;
		}

		if(error != i2c_error_ok)
			i2c_reset();
	}

	return(dst - orig_dst);
}
