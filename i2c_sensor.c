#include "i2c_sensor.h"

#include "util.h"

#include <math.h>

typedef struct
{
	double raw;
	double cooked;
} value_t;

typedef struct
{
	i2c_sensor_t id;
	const char *name;
	const char *type;
	const char *unity;
	uint8_t precision;
	i2c_error_t (* const read_fn)(value_t *);
} fn_table_t;

ICACHE_FLASH_ATTR static i2c_error_t bmp085_write(uint8_t reg, uint8_t value)
{
	i2c_error_t error;
	uint8_t i2cbuffer[2];

	i2cbuffer[0] = reg;
	i2cbuffer[1] = value;

	if((error = i2c_send(0x77, 2, i2cbuffer)) != i2c_error_ok)
		return(error);

	return(0);
}

ICACHE_FLASH_ATTR static i2c_error_t bmp085_read(uint8_t reg, uint16_t *value)
{
	i2c_error_t error;
	uint8_t i2cbuffer[2];

	i2cbuffer[0] = reg;

	if((error = i2c_send(0x77, 1, i2cbuffer)) != i2c_error_ok)
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

	i2cbuffer[0] = reg;

	if((error = i2c_send(0x77, 1, i2cbuffer)) != i2c_error_ok)
		return(error);

	if((error = i2c_receive(0x77, 3, i2cbuffer)) != i2c_error_ok)
		return(error);

	*value = ((uint32_t)i2cbuffer[0] << 16) | ((uint32_t)i2cbuffer[1] << 8) | (uint32_t)i2cbuffer[2];

	return(0);
}

ICACHE_FLASH_ATTR static i2c_error_t tsl2560_write(uint8_t reg, uint8_t value)
{
	i2c_error_t error;
	uint8_t i2cbuffer[2];

	i2cbuffer[0]= 0b11000000 | reg; // write byte
	i2cbuffer[1] = value;

	if((error = i2c_send(0x39, 2, i2cbuffer)) != i2c_error_ok)
		return(error);

	return(0);
}

ICACHE_FLASH_ATTR static i2c_error_t tsl2560_read(uint8_t reg, uint8_t size, uint8_t *byte)
{
	i2c_error_t error;
	uint8_t i2cbuffer;

	i2cbuffer = 0b11000000 | reg; // read byte

	if((error = i2c_send(0x39, 1, &i2cbuffer)) != i2c_error_ok)
		return(error);

	if((error = i2c_receive(0x39 , 1, byte)) != i2c_error_ok)
		return(error);

	return(i2c_error_ok);
}

ICACHE_FLASH_ATTR static i2c_error_t tsl2560_read_block(uint8_t reg, uint8_t size, uint8_t *values)
{
	i2c_error_t error;
	uint8_t i2cbuffer;

	i2cbuffer = 0b10010000 | reg; // read block

	if((error = i2c_send(0x39, 1, &i2cbuffer)) != i2c_error_ok)
		return(error);

	if((error = i2c_receive(0x39 , 4, values)) != i2c_error_ok)
		return(error);

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

ICACHE_FLASH_ATTR static i2c_error_t sensor_lm75_read(value_t *value)
{
	uint8_t i2cbuffer[2];
	i2c_error_t error;
	uint32_t raw;

	i2cbuffer[0] = 0x01;		// select config register
	i2cbuffer[1] = 0b01100000;	// write r0=r1=1, max resolution, other bits zero

	if((error = i2c_send(0x48, 2, i2cbuffer)) != i2c_error_ok)
		return(error);

	i2cbuffer[0] = 0x00; // select temperature register

	if((error = i2c_send(0x48, 1, i2cbuffer)) != i2c_error_ok)
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

ICACHE_FLASH_ATTR static i2c_error_t sensor_ds1631_read(value_t *value)
{
	uint8_t i2cbuffer[2];
	i2c_error_t error;
	uint32_t raw;

	i2cbuffer[0] = 0xac;		// select config register
	i2cbuffer[1] = 0b00001100;	// r0=r1=1, max resolution, other bits zero

	if((error = i2c_send(0x48, 2, i2cbuffer)) != i2c_error_ok)
		return(error);

	i2cbuffer[0] = 0x51;		// start conversion (if not started already)

	if((error = i2c_send(0x48, 1, i2cbuffer)) != i2c_error_ok)
		return(error);

	i2cbuffer[0] = 0xaa;		// read temperature

	if((error = i2c_send(0x48, 1, i2cbuffer)) != i2c_error_ok)
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

ICACHE_FLASH_ATTR static i2c_error_t sensor_tsl2560_init(void)
{
	i2c_error_t error;

	if((error = tsl2560_write(0x00, 0x03)) != i2c_error_ok)	// tsl2560; power up
		return(error);

	if((error = tsl2560_write(0x06, 0x00)) != i2c_error_ok)	// disable interrupts
		return(error);

	if((error = tsl2560_write(0x01, 0x11)) != i2c_error_ok)	// start continuous sampling every 100 ms, high gain = 16x
		return(error);

	return(i2c_error_ok);
}

ICACHE_FLASH_ATTR static i2c_error_t sensor_tsl2560_read(value_t *value)
{
	uint8_t	i2cbuffer[4];
	i2c_error_t	error;
	uint32_t ch0, ch1;
	double raw;

	if((error = sensor_tsl2560_init()) != i2c_error_ok)
		return(error);

	if((error = tsl2560_read_block(0x0c, 4, i2cbuffer)) != i2c_error_ok)
		return(error);

	ch0 = i2cbuffer[0] | (i2cbuffer[1] << 8);
	ch1 = i2cbuffer[2] | (i2cbuffer[3] << 8);

	if((ch0 < 37170) && (ch1 < 37170))
	{
		if(ch0 != 0)
		{
			raw = (double)ch1 / (double)ch0;

			if(raw <= 0.50)
				//value->cooked = (0.0304 * (double)ch0) - (0.062 * ch0 * powf(raw, 1.4)); // FIXME
				value->cooked = (0.0224 * (double)ch0) - (0.031 * (double)ch1); // duplicated from next line
			else if(raw <= 0.61)
				value->cooked = (0.0224 * (double)ch0) - (0.031 * (double)ch1);
			else if(raw <= 0.80)
				value->cooked = (0.0128 * (double)ch0) - (0.0153 * (double)ch1);
			else if(raw <= 1.30)
				value->cooked = (0.00146 * (double)ch0) - (0.00112 * (double)ch1);
			else
				value->cooked = 0;

			value->cooked /= 0.252; // integration time = 100 ms, scale = 0.252
		}
		else
			value->cooked = 0;
	}
	else
		value->cooked = -1;

	value->raw = ((double)ch0 * 100000.0) + (double)ch1;

	return(i2c_error_ok);
}

ICACHE_FLASH_ATTR static i2c_error_t sensor_bh1750_init(void)
{
	uint8_t i2cbuffer[1];
	i2c_error_t error;

	i2cbuffer[0] = 0x01;	// bh1750; power on

	if((error = i2c_send(0x23, 1, i2cbuffer)) != i2c_error_ok)
	{
		i2c_reset();
		return(error);
	}

	i2cbuffer[0] = 0x07;	// reset

	if((error = i2c_send(0x23, 1, i2cbuffer)) != i2c_error_ok)
	{
		i2c_reset();
		return(error);
	}

	i2cbuffer[0] = 0x11;	// start continuous sampling every 120 ms, high resolution = 0.42 Lx

	if((error = i2c_send(0x23, 1, i2cbuffer)) != i2c_error_ok)
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

ICACHE_FLASH_ATTR static i2c_error_t sensor_htu21_read_temp(value_t *value)
{
	i2c_error_t error;
	uint8_t	i2cbuffer[4];
	uint8_t	crc1, crc2;

	i2cbuffer[0] = 0xe3; // temperature measurement "hold master" mode

	if((error = i2c_send(0x40, 1, i2cbuffer)) != i2c_error_ok)
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

	i2cbuffer[0] = 0xe5; // humidity measurement "hold master" mode

	if((error = i2c_send(0x40, 1, i2cbuffer)) != i2c_error_ok)
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
	i2c_send(0x5c, 0, 0);
#endif

	msleep(1);

	i2cbuffer[0] = 0x03;	// read registers
	i2cbuffer[1] = 0x02;	// start address
	i2cbuffer[2] = 0x02;	// length;

	if((error = i2c_send(0x5c, 3, i2cbuffer)) != i2c_error_ok)
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
	i2c_send(0x5c, 0, 0);
#endif

	msleep(1);

	i2cbuffer[0] = 0x03;	// read registers
	i2cbuffer[1] = 0x00;	// start address
	i2cbuffer[2] = 0x02;	// length;

	if((error = i2c_send(0x5c, 3, i2cbuffer)) != i2c_error_ok)
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

ICACHE_FLASH_ATTR void i2c_sensor_init(void)
{
	if(sensor_init_tsl2560() != i2c_error_ok)
		i2c_reset();

	if(sensor_init_bh1750() != i2c_error_ok)
		i2c_reset();
}

static const fn_table_t fn_table[] =
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
		i2c_sensor_lm75,
		"lm75", "temperature", "C", 1,
		sensor_lm75_read
	},
	{
		i2c_sensor_ds1631,
		"ds1631", "temperature", "C", 2,
		sensor_ds1631_read
	},
	{
		i2c_sensor_bmp085_temperature,
		"bmp085", "temperature", "C", 1,
		sensor_bmp085_read_temp
	},
	{
		i2c_sensor_bmp085_airpressure,
		"bmp085", "pressure", "hPa", 0,
		sensor_bmp085_read_pressure
	},
	{
		i2c_sensor_tsl2560,
		"tsl2560", "light", "Lux", 0,
		sensor_tsl2560_read,
	},
	{
		i2c_sensor_bh1750,
		"bh1750", "light", "Lux", 0,
		sensor_bh1750_read
	},
	{
		i2c_sensor_htu21_temperature,
		"htu21", "temperature", "C", 1,
		sensor_htu21_read_temp
	},
	{
		i2c_sensor_htu21_humidity,
		"htu21", "humidity", "%", 0,
		sensor_htu21_read_hum
	},
	{
		i2c_sensor_am2321_temperature,
		"am2321", "temperature", "C", 1,
		sensor_am2321_read_temp
	},
	{
		i2c_sensor_am2321_humidity,
		"am2321", "humidity", "%", 0,
		sensor_am2321_read_hum
	}
};

ICACHE_FLASH_ATTR uint16_t i2c_sensor_read(i2c_sensor_t sensor, bool_t list, bool_t verbose,
		uint16_t size, char *dst)
{
	const fn_table_t *fn_table_entry;
	i2c_error_t error;
	value_t value;
	uint16_t length;
	char *orig_dst = dst;

	if(sensor >= i2c_sensor_size)
		return(snprintf(dst, size, "i2c sensor read: sensor %d out of range\n", sensor));

	fn_table_entry = &fn_table[sensor];

	if((error = fn_table_entry->read_fn(&value)) == i2c_error_ok)
	{
		length = snprintf(dst, size, "sensor %d/%s: %s: ", sensor,
				fn_table_entry->name, fn_table_entry->type);
		dst += length;
		size -= length;

		length = snprintf(dst, size, "%s", "[");
		dst += length;
		size -= length;

		length = double_to_string(value.cooked, fn_table_entry->precision, 1e10, size, dst);
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
			length = snprintf(dst, size, "sensor %d/%s: %s: ", sensor,
					fn_table_entry->name, fn_table_entry->type);
			dst += length;
			size -= length;

			if(verbose)
				length = i2c_error_format_string("error", error, size, dst);
			else
				length = snprintf(dst, size, "%s", "error");

			dst += length;
			size -= length;

			length = snprintf(dst, size, "%s", "\n");
			dst += length;
			size -= length;
		}

		i2c_reset();
	}

	return(dst - orig_dst);
}
