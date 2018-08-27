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

	if((error = i2c_send2(entry->address, 0xac, 0x0c)) != i2c_error_ok)
		return(error);

	// start conversions

	if((error = i2c_send1(entry->address, 0x51)) != i2c_error_ok)
		return(error);

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_ds1631_read(int bus, const device_table_entry_t *entry, value_t *value)
{
	uint8_t i2cbuffer[2];
	i2c_error_t error;
	int raw;

	// read temperature

	if((error = i2c_send1(entry->address, 0xaa)) != i2c_error_ok)
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

enum
{
	lm75_reg_temp	= 0x00,
	lm75_reg_conf	= 0x01,
	lm75_reg_thyst	= 0x02,
	lm75_reg_tos	= 0x03,
};

enum
{
	lm75_reg_conf_reserved			= 0b11100000,
	lm75_reg_conf_tmp275_res_0		= 0b00000000,
	lm75_reg_conf_tmp275_res_1		= 0b00100000,
	lm75_reg_conf_tmp275_res_2		= 0b01000000,
	lm75_reg_conf_tmp275_res_3		= 0b01100000,
	lm75_reg_conf_os_f_queue_1		= 0b00000000,
	lm75_reg_conf_os_f_queue_2		= 0b00001000,
	lm75_reg_conf_os_f_queue_3		= 0b00010000,
	lm75_reg_conf_os_f_queue_4		= 0b00011000,
	lm75_reg_conf_os_pol_low		= 0b00000000,
	lm75_reg_conf_os_pol_high		= 0b00000100,
	lm75_reg_conf_os_comp_int_comp	= 0b00000000,
	lm75_reg_conf_os_comp_int_int	= 0b00000010,
	lm75_reg_conf_shutdown_enable	= 0b00000001,
	lm75_reg_conf_shutdown_disable	= 0b00000000,
};

irom static i2c_error_t sensor_lm75_init(int bus, const device_table_entry_t *entry)
{
	uint8_t i2cbuffer[4];
	i2c_error_t error;

	if((error = i2c_send2(entry->address, lm75_reg_conf, lm75_reg_conf_tmp275_res_3 | lm75_reg_conf_shutdown_disable)) != i2c_error_ok)
		return(error);

	if((error = i2c_send1_receive_repeated_start(entry->address, lm75_reg_conf, 1, i2cbuffer)) != i2c_error_ok)
		return(error);

	if((i2cbuffer[0] != lm75_reg_conf_tmp275_res_3 /* most */) && (i2cbuffer[0] != 0x00 /* lm75bd */))
		return(i2c_error_device_error_1);

	if((error = i2c_send3(entry->address, lm75_reg_tos, 0xff, 0xff)) != i2c_error_ok)
		return(error);

	if((error = i2c_send1_receive_repeated_start(entry->address, lm75_reg_tos, 2, i2cbuffer)) != i2c_error_ok)
		return(error);

	if((i2cbuffer[0] != 0xff) || ((i2cbuffer[1] & 0x0f) != 0x00))
	{
		logfmt("\nlm75: [0] = %02x, [1] = %02x\n", i2cbuffer[0], i2cbuffer[1]);
		return(i2c_error_device_error_2);
	}

	if((error = i2c_send3(entry->address, lm75_reg_tos, 0x00, 0x00)) != i2c_error_ok)
		return(error);

	if((error = i2c_send1_receive_repeated_start(entry->address, lm75_reg_tos, 2, i2cbuffer)) != i2c_error_ok)
		return(error);

	if((i2cbuffer[0] != 0x00) || (i2cbuffer[1] != 0x00))
		return(i2c_error_device_error_3);

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_lm75_read(int bus, const device_table_entry_t *entry, value_t *value)
{
	uint8_t i2c_buffer[2];
	i2c_error_t error;

	if((error = i2c_send1_receive_repeated_start(entry->address, lm75_reg_temp, 2, i2c_buffer)) != i2c_error_ok)
		return(error);

	value->raw = (i2c_buffer[0] << 8) | (i2c_buffer[1] << 0);
	value->cooked = value->raw / 256;

	if(value->cooked > 127)
		value->cooked -= 256;

	return(i2c_error_ok);
}

static struct
{
	int16_t		ac1;
	int16_t		ac2;
	int16_t		ac3;
	uint16_t	ac4;
	uint16_t	ac5;
	uint16_t	ac6;
	int16_t		b1;
	int16_t		b2;
	int16_t		mc;
	int16_t		md;
} bmp085;

irom static i2c_error_t bmp085_write_reg_1(int address, int reg, unsigned int value)
{
	i2c_error_t error;

	if((error = i2c_send2(address, reg, (uint8_t)value)) != i2c_error_ok)
		return(error);

	return(0);
}

irom static i2c_error_t bmp085_read_reg_2(int address, int reg, uint16_t *value)
{
	i2c_error_t error;
	uint8_t i2cbuffer[2];

	if((error = i2c_send1(address, reg)) != i2c_error_ok)
		return(error);

	if((error = i2c_receive(address, 2, i2cbuffer)) != i2c_error_ok)
		return(error);

	*value = (i2cbuffer[0] << 8) | (i2cbuffer[1] << 0);

	return(0);
}

irom static i2c_error_t bmp085_read_reg_3(int address, int reg, uint32_t *value)
{
	i2c_error_t error;
	uint8_t i2cbuffer[4];

	if((error = i2c_send1(address, reg)) != i2c_error_ok)
		return(error);

	if((error = i2c_receive(address, 3, i2cbuffer)) != i2c_error_ok)
		return(error);

	*value = (i2cbuffer[0] << 16) | (i2cbuffer[1] << 8) | (i2cbuffer[2] << 0);

	return(0);
}

irom static i2c_error_t bmp085_read(int address, value_t *rv_temperature, value_t *rv_airpressure)
{
	uint16_t	ut;
	uint32_t	up = 0;
	int32_t		p;
	int32_t		x1, x2, x3;
	uint32_t	b4, b7;
	int32_t		b3, b5, b6;
	uint8_t		oss = 3;
	i2c_error_t	error;

	/* set cmd = 0x2e = start temperature measurement */

	if((error = bmp085_write_reg_1(address, 0xf4, 0x2e)) != i2c_error_ok)
		return(error);

	msleep(5);

	/* fetch result from 0xf6,0xf7 */

	if((error = bmp085_read_reg_2(address, 0xf6, &ut)) != i2c_error_ok)
		return(error);

	x1 = ((ut - bmp085.ac6) * bmp085.ac5) / (1 << 15);

	if((x1 + bmp085.md) == 0)
		return(i2c_error_device_error_1);

	x2 = (bmp085.mc * (1 << 11)) / (x1 + bmp085.md);

	b5 = x1 + x2;

	if(rv_temperature)
	{
		rv_temperature->raw		= ut;
		rv_temperature->cooked	= ((b5 + 8.0) / 16) / 10;
	}

	/* set cmd = 0x34 = start air pressure measurement */

	if((error = bmp085_write_reg_1(address, 0xf4, 0x34 | (oss << 6))) != i2c_error_ok)
		return(error);

	msleep(25);

	/* fetch result from 0xf6,0xf7,0xf8 */

	if((error = bmp085_read_reg_3(address, 0xf6, &up)) != i2c_error_ok)
		return(error);

	up = up >> (8 - oss);

	b6	= b5 - 4000;
	x1	= (bmp085.b2 * ((b6 * b6) / (1 << 12))) / (1 << 11);
	x2	= (bmp085.ac2 * b6) / (1 << 11);
	x3	= x1 + x2;
	b3	= (((bmp085.ac1 * 4 + x3) << oss) + 2) / 4;
	x1	= (bmp085.ac3 * b6) / (1 << 13);
	x2	= (bmp085.b1 * ((b6 * b6) / (1 << 12))) / (1 << 16);
	x3	= (x1 + x2 + 2) / (1 << 2);
	b4	= (bmp085.ac4 * (x3 + 32768)) / (1 << 15);
	b7	= (up - b3) * (50000 >> oss);

	if(b4 == 0)
		return(i2c_error_device_error_2);

	if(b7 & 0x80000000)
		p = ((b7 * 2) / b4) << 1;
	else
		p = (b7 / b4) * 2;

	x1	= p / (1 << 8);
	x1	= x1 * x1;
	x1	= (x1 * 3038) / (1 << 16);
	x2	= (-7357 * p) / (1 << 16);
	p	= p + ((x1 + x2 + 3791) / (1 << 4));

	if(rv_airpressure)
	{
		rv_airpressure->raw = up;
		rv_airpressure->cooked = p / 100.0;
	}

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_bmp085_init_temp(int bus, const device_table_entry_t *entry)
{
	i2c_error_t error;

	if((error = bmp085_read_reg_2(entry->address, 0xaa, &bmp085.ac1)) != i2c_error_ok)
		return(error);

	if((error = bmp085_read_reg_2(entry->address, 0xac, &bmp085.ac2)) != i2c_error_ok)
		return(error);

	if((error = bmp085_read_reg_2(entry->address, 0xae, &bmp085.ac3)) != i2c_error_ok)
		return(error);

	if((error = bmp085_read_reg_2(entry->address, 0xb0, &bmp085.ac4)) != i2c_error_ok)
		return(error);

	if((error = bmp085_read_reg_2(entry->address, 0xb2, &bmp085.ac5)) != i2c_error_ok)
		return(error);

	if((error = bmp085_read_reg_2(entry->address, 0xb4, &bmp085.ac6)) != i2c_error_ok)
		return(error);

	if((error = bmp085_read_reg_2(entry->address, 0xb6, &bmp085.b1)) != i2c_error_ok)
		return(error);

	if((error = bmp085_read_reg_2(entry->address, 0xb8, &bmp085.b2)) != i2c_error_ok)
		return(error);

	if((error = bmp085_read_reg_2(entry->address, 0xbc, &bmp085.mc)) != i2c_error_ok)
		return(error);

	if((error = bmp085_read_reg_2(entry->address, 0xbe, &bmp085.md)) != i2c_error_ok)
		return(error);

	if((error = bmp085_read(entry->address, 0, 0)) != i2c_error_ok)
		return(error);

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_bmp085_read_temp(int bus, const device_table_entry_t *entry, value_t *value)
{
	return(bmp085_read(entry->address, value, 0));
}

irom static i2c_error_t sensor_bmp085_init_pressure(int bus, const device_table_entry_t *entry)
{
	if(!i2c_sensor_detected(bus, i2c_sensor_bmp085_temperature))
		return(i2c_error_address_nak);

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_bmp085_read_pressure(int bus, const device_table_entry_t *entry, value_t *value)
{
	return(bmp085_read(entry->address, 0, value));
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

	if((error = i2c_send1(address, in)) != i2c_error_ok)
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

	if(i2c_sensor_detected(bus, i2c_sensor_tsl2561_0))
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

	if(i2c_sensor_detected(bus, i2c_sensor_tsl2561_0))
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

typedef enum
{
	tsl2561_reg_control =			0x00,
	tsl2561_reg_timeint =			0x01,
	tsl2561_reg_threshlowlow =		0x02,
	tsl2561_reg_threshlowhigh =		0x03,
	tsl2561_reg_threshhighlow =		0x04,
	tsl2561_reg_threshhighhigh =	0x05,
	tsl2561_reg_interrupt =			0x06,
	tsl2561_reg_crc =				0x08,
	tsl2561_reg_id =				0x0a,
	tsl2561_reg_data0low =			0x0c,
	tsl2561_reg_data0high =			0x0d,
	tsl2561_reg_data1low =			0x0e,
	tsl2561_reg_data1high =			0x0f,
} tsl2561_reg_t;

typedef enum
{
	tsl2561_cmd_address =	(1 << 0) | (1 << 1) | (1 << 2) | (1 << 3),
	tsl2561_cmd_block =		1 << 4,
	tsl2561_cmd_word =		1 << 5,
	tsl2561_cmd_clear =		1 << 6,
	tsl2561_cmd_cmd =		1 << 7,
} tsl2561_cmd_t;

typedef enum
{
	tsl2561_ctrl_power_off =	0x00,
	tsl2561_ctrl_power_on =		0x03,
} tsl2561_ctrl_t;

typedef enum
{
	tsl2561_tim_integ_13ms	=	(0 << 1) | (0 << 0),
	tsl2561_tim_integ_101ms	=	(0 << 1) | (1 << 0),
	tsl2561_tim_integ_402ms	=	(1 << 1) | (0 << 0),
	tsl2561_tim_manual		=	1 << 3,
	tsl2561_tim_high_gain	=	1 << 4,
} tsl2561_timeint_t;

typedef struct
{
	const double ratio_top;
	const double ch0_factor;
	const double ch1_factor;
} tsl2561_lookup_t;

static const tsl2561_lookup_t tsl2561_lookup[] =
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

irom static i2c_error_t tsl2561_write(int address, tsl2561_reg_t reg, unsigned int value)
{
	i2c_error_t error;

	if((error = i2c_send2(address, tsl2561_cmd_cmd | tsl2561_cmd_clear | (reg & tsl2561_cmd_address), value)) != i2c_error_ok)
		return(error);

	return(i2c_error_ok);
}

irom static i2c_error_t tsl2561_read(int address, tsl2561_reg_t reg, uint8_t *byte)
{
	i2c_error_t error;

	if((error = i2c_send1_receive_repeated_start(address, tsl2561_cmd_cmd | (reg & tsl2561_cmd_address), sizeof(*byte), byte)) != i2c_error_ok)
		return(error);

	return(i2c_error_ok);
}

irom static i2c_error_t tsl2561_write_check(int address, tsl2561_reg_t reg, unsigned int value)
{
	i2c_error_t error;
	uint8_t rv;

	if((error = tsl2561_write(address, reg, value)) != i2c_error_ok)
		return(error);

	if((error = tsl2561_read(address, reg, &rv)) != i2c_error_ok)
		return(error);

	if(value != rv)
		return(i2c_error_device_error_1);

	return(i2c_error_ok);
}

irom static i2c_error_t tsl2561_read_block(int address, tsl2561_reg_t reg, unsigned int *value1, unsigned int *value2)
{
	i2c_error_t error;
	uint8_t i2cbuffer[4];

	if((error = i2c_send1_receive_repeated_start(address, tsl2561_cmd_cmd | tsl2561_cmd_block | (reg & tsl2561_cmd_address), sizeof(i2cbuffer), i2cbuffer)) != i2c_error_ok)
		return(error);

	*value1 = (i2cbuffer[1] << 8) | i2cbuffer[0];
	*value2 = (i2cbuffer[3] << 8) | i2cbuffer[2];

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_tsl2561_init(int bus, const device_table_entry_t *entry)
{
	i2c_error_t error;
	uint8_t regval;
	tsl2561_timeint_t timeint;
	uint8_t i2cbuffer[2];

	if((entry->address == 0x39) && i2c_sensor_detected(bus, i2c_sensor_tsl2550))
		return(i2c_error_device_error_1);

	if((i2c_receive(0x39, 1, i2cbuffer) == i2c_error_ok) &&
			(i2c_receive(0x38, 1, i2cbuffer) == i2c_error_ok))	// try to detect veml6070
		return(i2c_error_device_error_2);						// which uses both 0x38 and 0x39 addresses

	if((error = tsl2561_write_check(entry->address, tsl2561_reg_control, tsl2561_ctrl_power_off)) != i2c_error_ok)
		return(error);

	if((error = tsl2561_read(entry->address, tsl2561_reg_id, &regval)) != i2c_error_ok)
		return(error);

	if(regval != 0x50)
		return(i2c_error_device_error_3);

	if(tsl2561_write_check(entry->address, tsl2561_reg_id, 0x00) == i2c_error_ok) // id register should not be writable
		return(i2c_error_device_error_4);

	if((error = tsl2561_write_check(entry->address, tsl2561_reg_interrupt, 0x00)) != i2c_error_ok)	// disable interrupts
		return(error);

	if(config_flags_get().flag.tsl_high_sens)
		timeint = tsl2561_tim_integ_402ms | tsl2561_tim_high_gain;
	else
		timeint = tsl2561_tim_integ_101ms;

	if((error = tsl2561_write_check(entry->address, tsl2561_reg_timeint, timeint)) != i2c_error_ok)	// start continuous sampling
		return(error);

	if((error = tsl2561_write(entry->address, tsl2561_reg_control, tsl2561_ctrl_power_on)) != i2c_error_ok)	// power up
		return(error);

	if((error = tsl2561_read(entry->address, tsl2561_reg_control, &regval)) != i2c_error_ok)
		return(error);

	if((regval & 0x0f) != tsl2561_ctrl_power_on)
		return(i2c_error_device_error_3);

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_tsl2561_read(int bus, const device_table_entry_t *entry, value_t *value)
{
	i2c_error_t	error;
	unsigned int ch0r, ch1r;
	double ratio, ch0, ch1;
	const tsl2561_lookup_t *tsl2561_entry;
	int current;

	value->raw = value->cooked = -1;

	if((error = tsl2561_read_block(entry->address, tsl2561_reg_data0low, &ch0r, &ch1r)) != i2c_error_ok)
		return(error);

	if(ch0r == 0)
	{
		value->raw = value->cooked = 0;
		return(i2c_error_ok);
	}

	if(config_flags_get().flag.tsl_high_sens)
	{
		// high sensitivity = 402 ms integration time, scaling factor = 1
		// analogue amplification = 16x, scaling factor = 1

		if((ch0r == 65535) || (ch1r == 65535))
		{
			value->raw = (100000 * ch0r) + ch1r;
			value->cooked = 2000;
			return(i2c_error_ok);
		}

		ch0 = (double)ch0r * 1.0 * 1.0;
		ch1 = (double)ch1r * 1.0 * 1.0;
	}
	else
	{
		// low  sensitivity = 101 ms integration time, scaling factor = 3.98 (402 / 101)
		// analogue amplification = 1x, scaling factor = 16

		if((ch0r == 37177) || (ch1r == 37177))
		{
			value->raw = (100000 * ch0r) + ch1r;
			value->cooked = 43000;
			return(i2c_error_ok);
		}

		ch0 = (double)ch0r * 3.98 * 16.0;
		ch1 = (double)ch1r * 3.98 * 16.0;
	}

	ratio = ch1 / ch0;

	for(current = 0;; current++)
	{
		tsl2561_entry = &tsl2561_lookup[current];

		if(((unsigned int)tsl2561_entry->ratio_top == 0) || ((unsigned int)tsl2561_entry->ch0_factor == 0) || ((unsigned int)tsl2561_entry->ch1_factor == 0))
			break;

		if(ratio <= tsl2561_entry->ratio_top)
			break;
	}

	value->raw = (ch0 * tsl2561_entry->ch0_factor) - (ch1 * tsl2561_entry->ch1_factor);

	if(value->raw < 0)
		value->raw = 0;

	if(ratio > 1.30)
		value->cooked = 0;
	else
		if(ratio >= 0.80)
			value->cooked = (0.00146 * ch0) - (0.00112 * ch1);
		else
			if(ratio >= 0.61)
				value->cooked = (0.0128 * ch0) - (0.0153 * ch1);
			else
				if(ratio >= 0.50)
					value->cooked = (0.0224 * ch0) - (0.031 * ch1);
				else
				{
					log("tsl2550: using pow\n");
					value->cooked = (0.0304 * ch0) - (0.062 * ch0 * pow(ratio, 1.4));
				}

	return(i2c_error_ok);
}

typedef enum
{
	bh1750_opcode_powerdown =		0b00000000,	// 0x00
	bh1750_opcode_poweron =			0b00000001,	// 0x01
	bh1750_opcode_reset =			0b00000111,	// 0x07
	bh1750_opcode_cont_hmode =		0b00010000,	// 0x10
	bh1750_opcode_cont_hmode2 =		0b00010001,	// 0x11
	bh1750_opcode_cont_lmode =		0b00010011,	// 0x13
	bh1750_opcode_one_hmode =		0b00100000,	// 0x20
	bh1750_opcode_one_hmode2 =		0b00100001,	// 0x21
	bh1750_opcode_one_lmode =		0b00100011,	// 0x23
	bh1750_opcode_change_meas_hi =	0b01000000,	// 0x40
	bh1750_opcode_change_meas_lo =	0b01100000,	// 0x60
} bh1740_opcode_t;

irom static i2c_error_t sensor_bh1750_init(int bus, const device_table_entry_t *entry)
{
	i2c_error_t error;
	int timing;
	uint8_t regval[2];
	bh1740_opcode_t mode;

	// there is no "read register" command on this device, so assume
	// a device at 0x23 is actually a bh1750, there is no way to be sure.

	// power on

	if((error = i2c_send1(entry->address, bh1750_opcode_poweron)) != i2c_error_ok)
		return(error);

	// reset

	if((error = i2c_send1(entry->address, bh1750_opcode_reset)) != i2c_error_ok)
		return(error);

	// set sensitivity

	if(config_flags_get().flag.bh_high_sens)
	{
		mode = bh1750_opcode_cont_hmode2;
		timing = 254; // max
	}
	else
	{
		mode = bh1750_opcode_cont_lmode;
		timing = 69; // default
	}

	regval[0] = bh1750_opcode_change_meas_hi | ((timing >> 5) & 0b00000111);
	regval[1] = bh1750_opcode_change_meas_lo | ((timing >> 0) & 0b00011111);

	if((error = i2c_send1(entry->address, regval[0])) != i2c_error_ok)
		return(error);

	if((error = i2c_send1(entry->address, regval[1])) != i2c_error_ok)
		return(error);

	// start continuous sampling

	if((error = i2c_send1(entry->address, mode)) != i2c_error_ok)
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
		// mode = hmode2, timing = 254
		// hmode2 = 1/2 lx / count
		// timing = 1 / 254 / 69 = 0.27
		luxpercount = 1.2 * 0.5 * 0.27;
	else
		// mode = hmode, timing = default = 69
		// hmode =  1 lx / count
		luxpercount = 1.2;

	value->raw		= (double)((i2cbuffer[0] << 8) | i2cbuffer[1]);
	value->cooked	= value->raw * luxpercount * 0.6;

	return(i2c_error_ok);
}

enum
{
	htu21_cmd_meas_temp_hold_master =		0xe3,
	htu21_cmd_meas_hum_hold_master =		0xe5,
	htu21_cmd_write_user =					0xe6,
	htu21_cmd_read_user =					0xe7,
	htu21_cmd_meas_temp_no_hold_master =	0xf3,
	htu21_cmd_meas_hum_no_hold_master =		0xf5,
	htu21_cmd_reset =						0xfe,
};

enum
{
	htu21_user_reg_rh12_temp14 =		0b00000000,
	htu21_user_reg_rh8_temp12 =			0b00000001,
	htu21_user_reg_rh10_temp13 =		0b10000000,
	htu21_user_reg_rh11_temp11 =		0b10000001,
	htu21_user_reg_bat_stat =			0b01000000,
	htu21_user_reg_reserved =			0b00111000,
	htu21_user_reg_heater_enable =		0b00000100,
	htu21_user_reg_otp_reload_disable =	0b00000010,
};

enum
{
	htu21_delay_reset =			2,
	htu21_delay_measurement =	8,
};

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

irom static i2c_error_t sensor_htu21_temp_init(int bus, const device_table_entry_t *entry)
{
	i2c_error_t error;
	uint8_t i2c_buffer[1];

	if((error = i2c_receive(entry->address, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	if((error = i2c_send1(entry->address, htu21_cmd_reset)) != i2c_error_ok)
		return(error);

	msleep(htu21_delay_reset);

	if((error = i2c_send1(entry->address, htu21_cmd_read_user)) != i2c_error_ok)
		return(error);

	if((error = i2c_receive(entry->address, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	i2c_buffer[0] &= htu21_user_reg_reserved | htu21_user_reg_bat_stat;
	i2c_buffer[0] |= htu21_user_reg_rh11_temp11 | htu21_user_reg_otp_reload_disable;

	if((error = i2c_send2(entry->address, htu21_cmd_write_user, i2c_buffer[0])) != i2c_error_ok)
		return(error);

	if((error = i2c_send1(entry->address, htu21_cmd_read_user)) != i2c_error_ok)
		return(error);

	if((error = i2c_receive(entry->address, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	i2c_buffer[0] &= ~(htu21_user_reg_reserved | htu21_user_reg_bat_stat);

	if(i2c_buffer[0] != (htu21_user_reg_rh11_temp11 | htu21_user_reg_otp_reload_disable))
		return(i2c_error_device_error_1);

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_htu21_hum_init(int bus, const device_table_entry_t *entry)
{
	if(!i2c_sensor_detected(bus, i2c_sensor_htu21_temperature))
		return(i2c_error_address_nak);

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_htu21_read(const device_table_entry_t *entry, uint8_t command, uint16_t *result)
{
	i2c_error_t error;
	uint8_t	i2cbuffer[4];
	uint8_t crc1, crc2;

	if((error = i2c_send1(entry->address, command)) != i2c_error_ok)
		return(error);

	msleep(htu21_delay_measurement);

	if((error = i2c_receive(entry->address, sizeof(i2cbuffer), i2cbuffer)) != i2c_error_ok)
		return(error);

	crc1 = i2cbuffer[2];
	crc2 = htu21_crc(2, &i2cbuffer[0]);

	if(crc1 != crc2)
		return(i2c_error_device_error_1);

	*result = (i2cbuffer[0] << 8) | (i2cbuffer[1] << 0);
	*result &= 0xfffc; // mask out status bits in the 2 LSB

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_htu21_temp_read(int bus, const device_table_entry_t *entry, value_t *value)
{
	i2c_error_t error;
	uint16_t result;

	if((error = sensor_htu21_read(entry, htu21_cmd_meas_temp_no_hold_master, &result)) != i2c_error_ok)
		return(error);

	value->raw = result;
	value->cooked = ((value->raw * 175.72) / 65536) - 46.85;

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_htu21_hum_read(int bus, const device_table_entry_t *entry, value_t *value)
{
	i2c_error_t error;
	uint16_t result;
	value_t temperature;

	if((error = sensor_htu21_temp_read(bus, entry, &temperature)) != i2c_error_ok)
		return(error);

	if((error = sensor_htu21_read(entry, htu21_cmd_meas_hum_no_hold_master, &result)) != i2c_error_ok)
		return(error);

	value->raw = ((result * 125.0) / 65536) - 6;
	value->cooked = value->raw + ((25 - temperature.cooked) * -0.10); // FIXME, TempCoeff guessed

	if(value->cooked < 0)
		value->cooked = 0;

	if(value->cooked > 100)
		value->cooked = 100;

	return(i2c_error_ok);
}

static value_t sensor_am2320_cached_temperature;
static value_t sensor_am2320_cached_humidity;

irom attr_pure static uint16_t am2320_crc(int length, const uint8_t *data)
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

irom static i2c_error_t sensor_am2320_read_registers(int address, int offset, int length, uint8_t *values)
{
	i2c_error_t	error;
	uint8_t		i2c_buffer[32];
	uint16_t	crc1, crc2;

	i2c_send(address, 0, 0);

	if((error = i2c_send3(address, 0x03, offset, length)) != i2c_error_ok)
		return(error);

	msleep(10);

	if((error = i2c_receive(address, length + 4, i2c_buffer)) != i2c_error_ok)
		return(error);

	if((i2c_buffer[0] != 0x03) || (i2c_buffer[1] != length))
		return(i2c_error_device_error_2);

	crc1 = i2c_buffer[length + 2] | (i2c_buffer[length + 3] << 8);
	crc2 = am2320_crc(length + 2, i2c_buffer);

	if(crc1 != crc2)
		return(i2c_error_device_error_3);

	memcpy(values, &i2c_buffer[2], length);

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_am2320_read(int address, value_t *value, bool_t request_humidity)
{
	i2c_error_t	error;
	uint8_t		values[4];
	int32_t		raw_temp, raw_hum;

	//	0x00	start address: humidity (16 bits), temperature (16 bits)
	//	0x04	length

	if((error = sensor_am2320_read_registers(address, 0x00, 0x04, values)) == i2c_error_ok)
	{
		raw_hum = (values[0] << 8) | values[1];

		if(raw_hum > 1000)
			raw_hum = 1000;

		sensor_am2320_cached_humidity.raw = raw_hum;
		sensor_am2320_cached_humidity.cooked = raw_hum / 10.0;

		raw_temp = (values[2] << 8) | values[3];

		if(raw_temp & 0x8000)
		{
			raw_temp &= 0x7fff;
			raw_temp = 0 - raw_temp;
		}

		sensor_am2320_cached_temperature.raw = raw_temp;
		sensor_am2320_cached_temperature.cooked = raw_temp / 10.0;
	}

	if(request_humidity)
		*value = sensor_am2320_cached_humidity;
	else
		*value = sensor_am2320_cached_temperature;

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_am2320_temp_read(int bus, const device_table_entry_t *entry, value_t *value)
{
	return(sensor_am2320_read(entry->address, value, false));
}

irom static i2c_error_t sensor_am2320_hum_read(int bus, const device_table_entry_t *entry, value_t *value)
{
	return(sensor_am2320_read(entry->address, value, true));
}

irom static i2c_error_t sensor_am2320_temp_init(int bus, const device_table_entry_t *entry)
{
	i2c_error_t	error;
	uint8_t		values[2];

	//	0x08	start address: device id
	//	0x02	length

	if((error = sensor_am2320_read_registers(entry->address, 0x08, 0x02, values)) != i2c_error_ok)
		return(error);

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_am2320_hum_init(int bus, const device_table_entry_t *entry)
{
	if(!i2c_sensor_detected(bus, i2c_sensor_am2320_temperature))
		return(i2c_error_address_nak);

	return(i2c_error_ok);
}

typedef enum
{
	veml6070_addr_ara = 0x09,
	veml6070_addr_cmd = 0x38,
	veml6070_addr_data_lsb = 0x38,
	veml6070_addr_data_msb = 0x39,
} veml6070_i2c_addr_t;

typedef enum
{
	veml6070_cmd_init =			0x06,
	veml6070_cmd_sd =			(1 << 0),				//	1
	veml6070_cmd_reserved0 =	(1 << 1),				//	2
	veml6070_cmd_it_0_5t =		(0 << 2) | (0 << 3),	//	0
	veml6070_cmd_it_1t =		(1 << 2) | (0 << 3),	//	4
	veml6070_cmd_it_2t =		(0 << 2) | (1 << 3),	//	8
	veml6070_cmd_it_4t =		(1 << 2) | (1 << 3),	//	12
	veml6070_cmd_thd =			(1 << 4),				//	16
	veml6070_cmd_ack =			(1 << 5),				//	32
	veml6070_cmd_reserved1 =	(1 << 6) | (1 << 7),	//	192
} veml6070_cmd_t;

irom static i2c_error_t veml6070_read(unsigned int *rv)
{
	i2c_error_t error;
	uint8_t i2c_buffer[2];

	if((error = i2c_receive(veml6070_addr_data_msb, 1, &i2c_buffer[0])) != i2c_error_ok)
		return(error);

	if((error = i2c_receive(veml6070_addr_data_lsb, 1, &i2c_buffer[1])) != i2c_error_ok)
		return(error);

	*rv = (i2c_buffer[0] << 8) | i2c_buffer[1];

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_veml6070_init(int bus, const device_table_entry_t *entry)
{
	i2c_error_t error;
	unsigned int rv;

	if(i2c_sensor_detected(bus, i2c_sensor_tsl2550)) // 0x39
		return(i2c_error_device_error_1);

	if(i2c_sensor_detected(bus, i2c_sensor_tsl2561_0)) // 0x39
		return(i2c_error_device_error_1);

	if((error = i2c_send1(veml6070_addr_cmd, veml6070_cmd_init)) != i2c_error_ok)
		return(error);

	if((error = i2c_send1(veml6070_addr_cmd, veml6070_cmd_sd)) != i2c_error_ok)
		return(error);

	if((error = i2c_send1(veml6070_addr_cmd, veml6070_cmd_it_4t)) != i2c_error_ok)
		return(error);

	if((error = veml6070_read(&rv)) != i2c_error_ok)
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
	value->cooked = (((double)rv - 700) / 750) * 1.6; // magic values determined from it = 4 and example relations from Vishay

	if(value->cooked < 0)
		value->cooked = 0;

	return(i2c_error_ok);
}

typedef enum
{
	si114x_reg_part_id =		0x00,
	si114x_reg_rev_id =			0x01,
	si114x_reg_seq_id =			0x02,
	si114x_reg_int_cfg =		0x03,
	si114x_reg_irq_enable =		0x04,
	si114x_reg_reserved_05 =	0x05,
	si114x_reg_reserved_06 =	0x06,
	si114x_reg_hw_key =			0x07,
	si114x_reg_meas_rate = 		0x08,
	si114x_reg_reserved_0a =	0x0a,
	si114x_reg_reserved_0b =	0x0b,
	si114x_reg_reserved_0c =	0x0c,
	si114x_reg_reserved_0d =	0x0d,
	si114x_reg_reserved_0e =	0x0e,
	si114x_reg_ps_led21 = 		0x0f,
	si114x_reg_ps_led3 = 		0x10,
	si114x_reg_reserved_11 =	0x11,
	si114x_reg_reserved_12 =	0x12,
	si114x_reg_ucoef =			0x13,
	si114x_reg_param_wr =		0x17,
	si114x_reg_command =		0x18,
	si114x_reg_reserved_19 =	0x19,
	si114x_reg_reserved_1a =	0x1a,
	si114x_reg_reserved_1b =	0x1b,
	si114x_reg_reserved_1c =	0x1c,
	si114x_reg_reserved_1d =	0x1d,
	si114x_reg_reserved_1e =	0x1e,
	si114x_reg_reserved_1f =	0x1f,
	si114x_reg_response =		0x20,
	si114x_reg_irq_status =		0x21,
	si114x_reg_als_vis_data	=	0x22,
	si114x_reg_als_ir_data =	0x24,
	si114x_reg_ps1_data =		0x26,
	si114x_reg_ps2_data =		0x28,
	si114x_reg_ps3_data =		0x2a,
	si114x_reg_aux_data =		0x2c,
	si114x_reg_param_rd =		0x2e,
	si114x_reg_reserved_2f =	0x2f,
	si114x_reg_chip_stat =		0x30,
	si114x_reg_ana_in_key0 =	0x3b,
	si114x_reg_ana_in_key1 =	0x3c,
	si114x_reg_ana_in_key2 =	0x3d,
	si114x_reg_ana_in_key3 =	0x3e,
} si114x_register_t;

typedef enum
{
	si114x_param_i2c_addr = 			0x00,
	si114x_param_chlist =				0x01,
	si114x_param_psled12_select =		0x02,
	si114x_param_psled3_select =		0x03,
	si114x_param_reserved_04 =			0x04,
	si114x_param_ps_encoding =			0x05,
	si114x_param_als_encoding =			0x06,
	si114x_param_ps1_adcmux =			0x07,
	si114x_param_ps2_adcmux =			0x08,
	si114x_param_ps3_adcmux =			0x09,
	si114x_param_ps_adc_counter =		0x0a,
	si114x_param_ps_adc_gain =			0x0b,
	si114x_param_ps_adc_misc =			0x0c,
	si114x_param_reserved_0d =			0x0d,
	si114x_param_als_ir_adcmux =		0x0e,
	si114x_param_aux_adcmux =			0x0f,
	si114x_param_als_vis_adc_counter =	0x10,
	si114x_param_als_vis_adc_gain =		0x11,
	si114x_param_als_vis_adc_misc =		0x12,
	si114x_param_reserved_13 =			0x13,
	si114x_param_reserved_14 =			0x14,
	si114x_param_reserved_15 =			0x15,
	si114x_param_reserved_16 =			0x16,
	si114x_param_reserved_17 =			0x17,
	si114x_param_reserved_18 =			0x18,
	si114x_param_reserved_19 =			0x19,
	si114x_param_reserved_1a =			0x1a,
	si114x_param_reserved_1b =			0x1b,
	si114x_param_led_rec =				0x1c,
	si114x_param_als_ir_adc_counter =	0x1d,
	si114x_param_als_ir_adc_gain =		0x1e,
	si114x_param_als_ir_adc_misc =		0x1f,
} si114x_parameter_t;

typedef enum
{
	si114x_cmd_nop =			0b00000000,	//	00
	si114x_cmd_reset =			0b00000001,	//	01
	si114x_cmd_busaddr =		0b00000010,	//	02
	si114x_cmd_reserved_03 =	0b00000011,	//	03
	si114x_cmd_reserved_04 =	0b00000100,	//	04
	si114x_cmd_psforce =		0b00000101,	//	05
	si114x_cmd_alsforce = 		0b00000110,	//	06
	si114x_cmd_psalsforce =		0b00000111,	//	07
	si114x_cmd_reserved_08 =	0b00001000,	//	08
	si114x_cmd_pspause =		0b00001001,	//	09
	si114x_cmd_alspause =		0b00001010,	//	0a
	si114x_cmd_psalspause =		0b00001011,	//	0b
	si114x_cmd_reserved_0c =	0b00001100,	//	0c
	si114x_cmd_psauto =			0b00001101,	//	0d
	si114x_cmd_alsauto =		0b00001110,	//	0e
	si114x_cmd_psalsauto =		0b00001111,	//	0f
	si114x_cmd_get_cal =		0b00010010,	//	12
	si114x_cmd_param_query =	0b10000000,	//	80
	si114x_cmd_param_set = 		0b10100000,	//	a0
} si114x_command_t;

typedef enum
{
	si114x_error_invalid_command =	0x80,
	si114x_error_adc_ps1_overflow = 0x88,
	si114x_error_adc_ps2_overflow = 0x89,
	si114x_error_adc_ps3_overflow = 0x8a,
	si114x_error_adc_vis_overflow = 0x8c,
	si114x_error_adc_ir_overflow =	0x8d,
	si114x_error_adc_aux_overflow = 0x8e,
} si114x_error_t;

typedef enum
{
	si114x_stat_sleep =			0b00000001,
	si114x_stat_suspend =		0b00000010,
	si114x_stat_running =		0b00000100,
} si114x_status_t;

typedef enum
{
	si114x_chlist_bit_uv =		0b10000000,
	si114x_chlist_bit_aux =		0b01000000,
	si114x_chlist_bit_ir =		0b00100000,
	si114x_chlist_bit_vis =		0b00010000,
	si114x_chlist_bit_ps3 =		0b00000100,
	si114x_chlist_bit_ps2 = 	0b00000010,
	si114x_chlist_bit_ps1 =		0b00000001,
} si114x_chlist_bit_t;

typedef enum
{
	si114x_mux_ir_small = 	0x00,
	si114x_mux_vis = 		0x02,
	si114x_mux_ir_large =	0x03,
	si114x_mux_no_diode =	0x06,
	si114x_mux_gnd =		0x25,
	si114x_mux_temp =		0x65,
	si114x_mux_vdd =		0x75,
} si114x_mux_t;

typedef enum
{
	si114x_irq_cmd_int =	0b00100000,
	si114x_irq_ps3_int =	0b00010000,
	si114x_irq_ps2_int =	0b00001000,
	si114x_irq_ps1_int =	0b00000100,
	si114x_irq_als_int =	0b00000001,
} si114_irq_t;

typedef enum
{
	si114x_sensor_visible =	0,
	si114x_sensor_infrared,
	si114x_sensor_uv_index,
	si114x_sensor_temp,
	si114x_sensor_size,
} si114x_sensor_t;

typedef struct
{
	si114x_chlist_bit_t	chlist;
	si114x_register_t	reg;
	double				offset;
	double				factor;
} si114x_sensor_entry_t;

static const si114x_sensor_entry_t si114x_sensor_entry[si114x_sensor_size] =
{
	{	si114x_chlist_bit_vis,	si114x_reg_als_vis_data,	-256,				1.0 / 0.282 * 14.5 / 2 * 1.2	},
	{	si114x_chlist_bit_ir,	si114x_reg_als_ir_data,		-256,				1.0 / 2.44 * 14.5 / 2 * 1.06	},
	{	si114x_chlist_bit_uv,	si114x_reg_aux_data,		0,					1.0 / 100 / 2 / 0.9				},
	{	si114x_chlist_bit_aux,	si114x_reg_aux_data,		-11136 + (35 * 25),	1.0 / 35						},
};

enum
{
	si114x_attempt_count = 16,
	si114x_measure_delay = 32768,
};

irom static i2c_error_t si114x_read_register(si114x_register_t reg, unsigned int length, uint8_t *bytes)
{
	i2c_error_t error;

	if((error = i2c_send1_receive_repeated_start(0x60, reg, length, bytes)) != i2c_error_ok)
		return(error);

	return(i2c_error_ok);
}

irom static i2c_error_t si114x_read_register_1(si114x_register_t reg, unsigned int *value)
{
	i2c_error_t error;
	uint8_t i2c_buffer[1];

	if((error = si114x_read_register(reg, 1, i2c_buffer)) != i2c_error_ok)
		return(error);

	*value = i2c_buffer[0];

	return(i2c_error_ok);
}

irom static i2c_error_t si114x_read_register_2(si114x_register_t reg, unsigned int *value)
{
	i2c_error_t error;
	uint8_t i2c_buffer[2];

	if((error = si114x_read_register(reg, 2, i2c_buffer)) != i2c_error_ok)
		return(error);

	*value = (i2c_buffer[1] << 8) | (i2c_buffer[0]);

	return(i2c_error_ok);
}

irom static i2c_error_t si114x_write_register_1(si114x_register_t reg, unsigned int value)
{
	return(i2c_send2(0x60, (uint8_t)reg, (uint8_t)value));
}

irom static i2c_error_t si114x_write_register_2(si114x_register_t reg, unsigned int value)
{
	uint8_t i2c_buffer[3];

	i2c_buffer[0] = (uint8_t)reg;
	i2c_buffer[1] = (value & 0x00ff) >> 0;
	i2c_buffer[2] = (value & 0xff00) >> 8;

	return(i2c_send(0x60, 3, i2c_buffer));
}

irom static i2c_error_t si114x_write_register_4(si114x_register_t reg, uint8_t val0, uint8_t val1, uint8_t val2, uint8_t val3)
{
	uint8_t i2c_buffer[5];

	i2c_buffer[0] = (uint8_t)reg;
	i2c_buffer[1] = val0;
	i2c_buffer[2] = val1;
	i2c_buffer[3] = val2;
	i2c_buffer[4] = val3;

	return(i2c_send(0x60, 5, i2c_buffer));
}

irom static i2c_error_t si114x_sendcmd(si114x_command_t command, unsigned int *response)
{
	unsigned int attempt1, attempt2, local_response;
	i2c_error_t error;

	if(response)
		*response = 0;

	for(attempt1 = si114x_attempt_count; attempt1 > 0; attempt1--)
	{
		if((error = si114x_read_register_1(si114x_reg_chip_stat, &local_response)) != i2c_error_ok)
			goto failed;

		if(local_response != si114x_stat_sleep)
			goto failed;

		if((error = si114x_write_register_1(si114x_reg_command, si114x_cmd_nop)) != i2c_error_ok)
			goto failed;

		msleep(1);

		if((error = si114x_read_register_1(si114x_reg_response, &local_response)) != i2c_error_ok)
			goto failed;

		if(local_response != 0x00)
		{
			logfmt("si114x: response not 0: %d\n", local_response);
			goto failed;
		}

		if((error = si114x_write_register_1(si114x_reg_command, command)) != i2c_error_ok)
			goto failed;

		msleep(1);

		for(attempt2 = si114x_attempt_count; attempt2 > 0; attempt2--)
		{
			if((error = si114x_read_register_1(si114x_reg_response, &local_response)) != i2c_error_ok)
				goto failed;

			if(response)
				*response = local_response;

			if(local_response == 0b00000001)					// normal, ok
				break;

			if((local_response & 0b10001000) == 0b10001000)		// overflow, treat as OK
			{
				logfmt("si114x: response overflow: %x\n", local_response);
				break;
			}

			logfmt("si114x: response invalid command / timeout: %x\n", local_response);
			logfmt("si114x: attempt2: %d\n", attempt2);

			msleep(1);
		}

		if(attempt2 > 0)
			break;

failed:
		logfmt("si114x: attempt1: %d\n", attempt1);
		msleep(1);
	}

	if(attempt1 == 0)
		return(i2c_error_device_error_1);

	return(i2c_error_ok);
}

irom static i2c_error_t si114x_get_param(si114x_parameter_t param, unsigned int *value)
{
	i2c_error_t error;

	if((error = si114x_sendcmd(si114x_cmd_param_query | ((uint8_t)param & 0x1f), (unsigned int *)0)) != i2c_error_ok)
		return(error);

	if((error = si114x_read_register_1(si114x_reg_param_rd, value)) != i2c_error_ok)
		return(error);

	return(i2c_error_ok);
}

irom static i2c_error_t si114x_set_param(si114x_parameter_t param, unsigned int value)
{
	unsigned int response;
	i2c_error_t error;

	if((error = si114x_write_register_1(si114x_reg_param_wr, value)) != i2c_error_ok)
		return(error);

	if((error = si114x_sendcmd(si114x_cmd_param_set | ((uint8_t)param & 0x1f), (unsigned int *)0)) != i2c_error_ok)
		return(error);

	if((error = si114x_get_param(param, &response)) != i2c_error_ok)
		return(error);

	if(response != value)
		return(i2c_error_device_error_2);

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_si114x_visible_light_init(int bus, const device_table_entry_t *entry)
{
	i2c_error_t error;
	unsigned int value;
	uint8_t i2c_buffer[1];

	if(i2c_sensor_detected(bus, i2c_sensor_mpl3115a2_temperature))
		return(i2c_error_device_error_1);

	if((error = i2c_receive(entry->address, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	if((error = si114x_read_register_1(si114x_reg_part_id, &value)) != i2c_error_ok)
		return(error);

	if((value != 0x45) && (value != 0x46) && (value != 0x47))
		return(i2c_error_device_error_2);

	if((error = si114x_write_register_2(si114x_reg_meas_rate, 0)) != i2c_error_ok)
		return(error);

	if((error = si114x_write_register_1(si114x_reg_command, si114x_cmd_reset)) != i2c_error_ok)
		return(error);

	msleep(20);

	if((error = si114x_write_register_1(si114x_reg_hw_key, 0x17)) != i2c_error_ok)
		return(error);

	if((error = si114x_sendcmd(si114x_cmd_psalspause, (unsigned int *)0)) != i2c_error_ok)
		return(error);

	if((error = si114x_write_register_1(si114x_reg_int_cfg, 0x00)) != i2c_error_ok)
		return(error);

	if((error = si114x_write_register_1(si114x_reg_irq_enable, 0)) != i2c_error_ok)
		return(error);

	// default UCOEF values for UV measurements

	if((error = si114x_write_register_4(si114x_reg_ucoef, 0x7b, 0x6b, 0x01, 0x00)) != i2c_error_ok)
		return(error);

	// sequencer parameters

	if((error = si114x_set_param(si114x_param_psled12_select, 0x00)) != i2c_error_ok) // leds PS1 and PS2 OFF
		return(error);

	if((error = si114x_set_param(si114x_param_psled3_select, 0x00)) != i2c_error_ok) // leds PS3 OFF
		return(error);

	if((error = si114x_set_param(si114x_param_ps_encoding, 0b00000000)) != i2c_error_ok)
		return(error);

	if((error = si114x_set_param(si114x_param_ps_adc_misc, 0b00000000)) != i2c_error_ok)
		return(error);

	if((error = si114x_set_param(si114x_param_ps1_adcmux, si114x_mux_gnd)) != i2c_error_ok)
		return(error);

	if((error = si114x_set_param(si114x_param_ps2_adcmux, si114x_mux_gnd)) != i2c_error_ok)
		return(error);

	if((error = si114x_set_param(si114x_param_ps3_adcmux, si114x_mux_gnd)) != i2c_error_ok)
		return(error);

	if((error = si114x_set_param(si114x_param_als_encoding, 0b00110000)) != i2c_error_ok) // report LSB
		return(error);

	if((error = si114x_set_param(si114x_param_als_ir_adc_misc, 0b00100000)) != i2c_error_ok) // reduce sensitivity, IR_RANGE(5) = 1, gain /= 14.5
		return(error);

	if((error = si114x_set_param(si114x_param_als_vis_adc_misc, 0b00100000)) != i2c_error_ok) // reduce sensitivity, VIS_RANGE(5) = 1, gain /= 14.5
		return(error);

	if((error = si114x_set_param(si114x_param_als_ir_adcmux, si114x_mux_ir_small)) != i2c_error_ok)
		return(error);

	if((error = si114x_set_param(si114x_param_aux_adcmux, si114x_mux_temp)) != i2c_error_ok)
		return(error);

	return(i2c_error_ok);
}

irom static i2c_error_t si114x_read_sensor(int address, si114x_sensor_t sensor, value_t *value)
{
	i2c_error_t error;
	unsigned int regval;
	const si114x_sensor_entry_t *sensor_entry = &si114x_sensor_entry[sensor];

	if(sensor >= si114x_sensor_size)
		return(i2c_error_device_error_4);

	sensor_entry = &si114x_sensor_entry[sensor];

	if((error = si114x_write_register_2(sensor_entry->reg, 0)) != i2c_error_ok)
		return(error);

	if((error = si114x_set_param(si114x_param_chlist, sensor_entry->chlist)) != i2c_error_ok)
		return(error);

	if((error = si114x_sendcmd(si114x_cmd_alsforce, (unsigned int *)0)) != i2c_error_ok)
		return(error);

	if((error = si114x_read_register_2(sensor_entry->reg, &regval)) != i2c_error_ok)
		return(error);

	value->raw = regval;

	value->cooked = (value->raw + sensor_entry->offset) * sensor_entry->factor;

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_si114x_visible_light_read(int bus, const device_table_entry_t *entry, value_t *value)
{
	return(si114x_read_sensor(entry->address, si114x_sensor_visible, value));
}

irom static i2c_error_t sensor_si114x_infrared_init(int bus, const device_table_entry_t *entry)
{
	if(i2c_sensor_detected(bus, i2c_sensor_si114x_visible_light))
		return(i2c_error_ok);

	return(i2c_error_address_nak);
}

irom static i2c_error_t sensor_si114x_infrared_read(int bus, const device_table_entry_t *entry, value_t *value)
{
	return(si114x_read_sensor(entry->address, si114x_sensor_infrared, value));
}

irom static i2c_error_t sensor_si114x_ultraviolet_init(int bus, const device_table_entry_t *entry)
{
	if(i2c_sensor_detected(bus, i2c_sensor_si114x_visible_light))
		return(i2c_error_ok);

	return(i2c_error_address_nak);
}

irom static i2c_error_t sensor_si114x_ultraviolet_read(int bus, const device_table_entry_t *entry, value_t *value)
{
	return(si114x_read_sensor(entry->address, si114x_sensor_uv_index, value));
}

irom static i2c_error_t sensor_si114x_temperature_init(int bus, const device_table_entry_t *entry)
{
	if(i2c_sensor_detected(bus, i2c_sensor_si114x_visible_light))
		return(i2c_error_ok);

	return(i2c_error_address_nak);
}

irom static i2c_error_t sensor_si114x_temperature_read(int bus, const device_table_entry_t *entry, value_t *value)
{
	return(si114x_read_sensor(entry->address, si114x_sensor_temp, value));
}

static struct
{
	uint16_t	dig_T1;		//	88/89
	int16_t		dig_T2;		//	8a/8b
	int16_t		dig_T3;		//	8c/8d
	uint16_t	dig_P1;		//	8e/8f
	int16_t		dig_P2;		//	90/91
	int16_t		dig_P3;		//	92/93
	int16_t		dig_P4;		//	94/95
	int16_t		dig_P5;		//	96/97
	int16_t		dig_P6;		//	98/99
	int16_t		dig_P7;		//	9a/9b
	int16_t		dig_P8;		//	9c/9d
	int16_t		dig_P9;		//	9e/9f
	uint8_t		dig_H1;		//	a1
	uint16_t	dig_H2;		//	e1/e2
	uint8_t		dig_H3;		//	e3
	int16_t		dig_H4;		//	e4/e5[3:0]
	int16_t		dig_H5;		//	e5[7:4]/e6
	int8_t		dig_H6;		//	e7
} bme280;

irom static i2c_error_t bme280_read_register_1(int address, int reg, uint8_t *value)
{
	i2c_error_t error;

	if((error = i2c_send1(address, reg)) != i2c_error_ok)
		return(error);

	if((error = i2c_receive(address, 1, value)) != i2c_error_ok)
		return(error);

	return(i2c_error_ok);
}

irom static i2c_error_t bme280_read_register_2(int address, int reg, uint16_t *value)
{
	i2c_error_t error;

	if((error = i2c_send1(address, reg)) != i2c_error_ok)
		return(error);

	if((error = i2c_receive(address, 2, (uint8_t *)value)) != i2c_error_ok)
		return(error);

	return(i2c_error_ok);
}

irom static i2c_error_t bme280_read(int address, value_t *rv_temperature, value_t *rv_pressure, value_t *rv_humidity)
{
	i2c_error_t		error;
	uint8_t 		i2c_buffer[8];
	int32_t			t_fine;
	uint32_t		adc_T, adc_P, adc_H;
	double			var1, var2;
	double			temperature, pressure, humidity;

	// retrieve all ADC values in one go to make use of the register shadowing feature

	if((error = i2c_send1(address, 0xf7)) != i2c_error_ok)
		return(error);

	if((error = i2c_receive(address, 8, i2c_buffer)) != i2c_error_ok)
		return(error);

	adc_P	= ((i2c_buffer[0] << 16) | 	(i2c_buffer[1] << 8) | (i2c_buffer[2] << 0)) >> 4;
	adc_T	= ((i2c_buffer[3] << 16) |	(i2c_buffer[4] << 8) | (i2c_buffer[5] << 0)) >> 4;
	adc_H	= (							(i2c_buffer[6] << 8) | (i2c_buffer[7] << 0)) >> 0;

	var1 = (adc_T / 16384.0 - bme280.dig_T1 / 1024.0) * bme280.dig_T2;
	var2 = ((adc_T / 131072.0 - bme280.dig_T1 / 8192.0) * (adc_T / 131072.0 - bme280.dig_T1 / 8192.0)) * bme280.dig_T3;

	t_fine = (int32_t)(var1 + var2);

	temperature = (var1 + var2) / 5120.0;

	var1 = (t_fine / 2.0) - 64000.0;
	var2 = var1 * var1 * bme280.dig_P6 / 32768.0;
	var2 = var2 + var1 * bme280.dig_P5 * 2.0;
	var2 = (var2 / 4.0) + (bme280.dig_P4 * 65536.0);
	var1 = (bme280.dig_P3 * var1 * var1 / 524288.0 + bme280.dig_P2 * var1) / 524288.0;
	var1 = (1.0 + var1 / 32768.0) * bme280.dig_P1;

	if(var1 < 0.0001)
		pressure = 0;
	else
	{
		pressure = 1048576.0 - adc_P;
		pressure = (pressure - (var2 / 4096.0)) * 6250.0 / var1;
		var1 = bme280.dig_P9 * pressure * pressure / 2147483648.0;
		var2 = pressure * bme280.dig_P8 / 32768.0;
		pressure = pressure + (var1 + var2 + bme280.dig_P7) / 16.0;
		pressure /= 100.0;
	}

	humidity = (t_fine - 76800.0);
	humidity = (adc_H - (bme280.dig_H4 * 64.0 + bme280.dig_H5 / 16384.0 * humidity)) * (bme280.dig_H2 / 65536.0 * (1.0 + bme280.dig_H6 / 67108864.0 * humidity * (1.0 + bme280.dig_H3 / 67108864.0 * humidity)));
	humidity = humidity * (1.0 - bme280.dig_H1 * humidity / 524288.0);

	if (humidity > 100.0)
		humidity = 100.0;

	if (humidity < 0.0)
		humidity = 0.0;

	if(rv_temperature)
	{
		rv_temperature->raw = adc_T;
		rv_temperature->cooked = temperature;
	}

	if(rv_pressure)
	{
		rv_pressure->raw = adc_P;
		rv_pressure->cooked = pressure;
	}

	if(rv_humidity)
	{
		rv_humidity->raw = adc_H;
		rv_humidity->cooked = humidity;
	}

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_bme280_temperature_init(int bus, const device_table_entry_t *entry)
{
	i2c_error_t	error;
	uint8_t		i2c_buffer[1];
	uint8_t		e4, e5, e6;

	if((error = i2c_receive(entry->address, 1, i2c_buffer)) != i2c_error_ok)
		return(error);

	if((error = i2c_send1(entry->address, 0xd0)) != i2c_error_ok)
		return(error);

	if((error = i2c_receive(entry->address, 1, i2c_buffer)) != i2c_error_ok)
		return(error);

	if((i2c_buffer[0] != 0x56) && (i2c_buffer[0] != 0x57) && (i2c_buffer[0] != 0x58) && (i2c_buffer[0] != 0x60))
		return(i2c_error_device_error_1);

	/* set device to sleep mode, so we can write configuration registers */

	// crtl_meas	0xf4		configure oversampling		0b00000000		temperature sampling is skipped
	// 														0b00000000		pressure sampling is skipped
	// 														0b00000000		device in sleep mode

	/* read calibration data */

	if((error = bme280_read_register_2(entry->address, 0x88, &bme280.dig_T1)) != i2c_error_ok)
		return(error);

	if((error = bme280_read_register_2(entry->address, 0x8a, &bme280.dig_T2)) != i2c_error_ok)
		return(error);

	if((error = bme280_read_register_2(entry->address, 0x8c, &bme280.dig_T3)) != i2c_error_ok)
		return(error);

	if((error = bme280_read_register_2(entry->address, 0x8e, &bme280.dig_P1)) != i2c_error_ok)
		return(error);

	if((error = bme280_read_register_2(entry->address, 0x90, &bme280.dig_P2)) != i2c_error_ok)
		return(error);

	if((error = bme280_read_register_2(entry->address, 0x92, &bme280.dig_P3)) != i2c_error_ok)
		return(error);

	if((error = bme280_read_register_2(entry->address, 0x94, &bme280.dig_P4)) != i2c_error_ok)
		return(error);

	if((error = bme280_read_register_2(entry->address, 0x96, &bme280.dig_P5)) != i2c_error_ok)
		return(error);

	if((error = bme280_read_register_2(entry->address, 0x98, &bme280.dig_P6)) != i2c_error_ok)
		return(error);

	if((error = bme280_read_register_2(entry->address, 0x9a, &bme280.dig_P7)) != i2c_error_ok)
		return(error);

	if((error = bme280_read_register_2(entry->address, 0x9c, &bme280.dig_P8)) != i2c_error_ok)
		return(error);

	if((error = bme280_read_register_2(entry->address, 0x9e, &bme280.dig_P9)) != i2c_error_ok)
		return(error);

	if((error = bme280_read_register_1(entry->address, 0xa1, &bme280.dig_H1)) != i2c_error_ok)
		return(error);

	if((error = bme280_read_register_2(entry->address, 0xe1, &bme280.dig_H2)) != i2c_error_ok)
		return(error);

	if((error = bme280_read_register_1(entry->address, 0xe3, &bme280.dig_H3)) != i2c_error_ok)
		return(error);

	if((error = bme280_read_register_1(entry->address, 0xe4, &e4)) != i2c_error_ok)
		return(error);

	if((error = bme280_read_register_1(entry->address, 0xe5, &e5)) != i2c_error_ok)
		return(error);

	if((error = bme280_read_register_1(entry->address, 0xe6, &e6)) != i2c_error_ok)
		return(error);

	bme280.dig_H4 = (e4 << 4) | ((e5 & 0x0f) >> 0);
	bme280.dig_H5 = (e6 << 4) | ((e5 & 0xf0) >> 4);

	if((error = bme280_read_register_1(entry->address, 0xe7, &bme280.dig_H6)) != i2c_error_ok)
		return(error);

	if((error = i2c_send2(entry->address, 0xf4, 0x00)) != i2c_error_ok)
		return(error);

	// crtl_hum		0xf2		humidity oversampling		0b00000101		humidity oversampling = 16

	if((error = i2c_send2(entry->address, 0xf2, 0x05)) != i2c_error_ok)
		return(error);

	// config		0xf5		device config				0b00000000		standby = 0.5 ms
	// 														0b00010000		filter range = 16
	// 														0b00000000		disable SPI interface

	if((error = i2c_send2(entry->address, 0xf5, 0x10)) != i2c_error_ok)
		return(error);

	/* now start sampling in normal mode */

	// crtl_meas	0xf4		configure oversampling		0b10100000		temperature oversampling = 16
	// 														0b00010100		pressure oversampling = 16
	// 														0b00000011		device normal acquisition mode

	if((error = i2c_send2(entry->address, 0xf4, 0xb7)) != i2c_error_ok)
		return(error);

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_bme280_temperature_read(int bus, const device_table_entry_t *entry, value_t *value)
{
	return(bme280_read(entry->address, value, 0, 0));
}

irom static i2c_error_t sensor_bme280_humidity_init(int bus, const device_table_entry_t *entry)
{
	if(i2c_sensor_detected(bus, i2c_sensor_bme280_temperature))
		return(i2c_error_ok);

	return(i2c_error_address_nak);
}

irom static i2c_error_t sensor_bme280_humidity_read(int bus, const device_table_entry_t *entry, value_t *value)
{
	return(bme280_read(entry->address, 0, 0, value));
}

irom static i2c_error_t sensor_bme280_airpressure_init(int bus, const device_table_entry_t *entry)
{
	if(i2c_sensor_detected(bus, i2c_sensor_bme280_temperature))
		return(i2c_error_ok);

	return(i2c_error_address_nak);
}

irom static i2c_error_t sensor_bme280_airpressure_read(int bus, const device_table_entry_t *entry, value_t *value)
{
	return(bme280_read(entry->address, 0, value, 0));
}

typedef enum
{
	max44009_reg_ints =			0x00,
	max44009_reg_inte =			0x01,
	max44009_reg_conf =			0x02,
	max44009_reg_data_msb =		0x03,
	max44009_reg_data_lsb =		0x04,
	max44009_reg_thresh_msb =	0x05,
	max44009_reg_thresh_lsb =	0x06,
	max44009_reg_thresh_timer =	0x07,
} max44009_register_t;

typedef enum
{
	max44009_conf_tim_800 =		(0 << 2) | (0 << 1) | (0 << 0),
	max44009_conf_tim_400 =		(0 << 2) | (0 << 1) | (1 << 0),
	max44009_conf_tim_200 =		(0 << 2) | (1 << 1) | (0 << 0),
	max44009_conf_tim_100 =		(0 << 2) | (1 << 1) | (1 << 0),
	max44009_conf_tim_50 =		(1 << 2) | (0 << 1) | (0 << 0),
	max44009_conf_tim_25 =		(1 << 2) | (0 << 1) | (1 << 0),
	max44009_conf_tim_12 =		(1 << 2) | (1 << 1) | (0 << 0),
	max44009_conf_tim_6 =		(1 << 2) | (1 << 1) | (1 << 0),
	max44009_conf_cdr = 		(1 << 3),
	max44009_conf_reserved4 = 	(1 << 4),
	max44009_conf_reserved5 = 	(1 << 5),
	max44009_conf_manual =		(1 << 6),
	max44009_conf_cont =		(1 << 7),
} max44009_reg_conf_t;

irom static i2c_error_t max44009_read_register(int address, int regaddr, unsigned int *value)
{
	i2c_error_t error;
	uint8_t i2c_buffer[1];

	if((error = i2c_send1_receive_repeated_start(address, regaddr, 1, i2c_buffer)) != i2c_error_ok)
	{
		*value = 0;
		return(error);
	}

	*value = i2c_buffer[0];

	return(i2c_error_ok);
}

irom static i2c_error_t max44009_write_register(int address, int regaddr, unsigned int value)
{
	i2c_error_t error;

	if((error = i2c_send2(address, regaddr, value)) != i2c_error_ok)
		return(error);

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_max44009_init(int bus, const device_table_entry_t *entry)
{
	i2c_error_t		error;
	unsigned int	value;

	if(i2c_sensor_detected(bus, i2c_sensor_lm75_2))
		return(i2c_error_address_nak);

	if((error = max44009_write_register(entry->address, max44009_reg_inte, 0xff)) != i2c_error_ok)
		return(error);

	if((error = max44009_read_register(entry->address, max44009_reg_inte, &value)) != i2c_error_ok)
		return(error);

	if(value != 0x01)
		return(i2c_error_device_error_1);

	if((error = max44009_write_register(entry->address, max44009_reg_inte, 0x00)) != i2c_error_ok)
		return(error);

	if((error = max44009_read_register(entry->address, max44009_reg_inte, &value)) != i2c_error_ok)
		return(error);

	if(value != 0x00)
		return(i2c_error_device_error_2);

	if((error = max44009_write_register(entry->address, max44009_reg_ints, 0xff)) != i2c_error_ok)
		return(error);

	if((error = max44009_read_register(entry->address, max44009_reg_ints, &value)) != i2c_error_ok)
		return(error);

	if(value != 0x00)
		return(i2c_error_device_error_3);

	if((error = max44009_write_register(entry->address, max44009_reg_conf, max44009_conf_cont)) != i2c_error_ok)
		return(error);

	if((error = max44009_read_register(entry->address, max44009_reg_conf, &value)) != i2c_error_ok)
		return(error);

	if((value & (max44009_conf_cont | max44009_conf_manual)) != max44009_conf_cont)
		return(i2c_error_device_error_4);

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_max44009_read(int bus, const device_table_entry_t *entry, value_t *value)
{
	i2c_error_t	error;
	uint8_t		i2c_buffer[2];
	int			exponent, mantissa;

	if((error = i2c_send1_receive_repeated_start(entry->address, max44009_reg_data_msb, 2, i2c_buffer)) != i2c_error_ok)
		return(error);

	exponent =	(i2c_buffer[0] & 0xf0) >> 4;
	mantissa =	(i2c_buffer[0] & 0x0f) << 4;
	mantissa |=	(i2c_buffer[1] & 0x0f) << 0;

	value->raw = (exponent * 10000) + mantissa;

	if(exponent == 0b1111)
		return(i2c_error_device_error_2);

	value->cooked = (1 << exponent) * mantissa * 0.045;

	return(i2c_error_ok);
}

typedef enum
{
	veml6075_reg_uv_conf = 			0x00,
	veml6075_reg_uv_uva_data =		0x07,
	veml6075_reg_uv_uvb_data =		0x09,
	veml6075_reg_uv_uvcomp1_data =	0x0a,
	veml6075_reg_uv_uvcomp2_data =	0x0b,
	veml6075_reg_uv_id =			0x0c,
} veml6075_register_t;

typedef enum
{
	veml6075_conf_sd =				1 << 0,
	veml6075_conf_uv_af =			1 << 1,
	veml6075_conf_uv_trig =			1 << 2,
	veml6075_conf_hd =				1 << 3,
	veml6075_conf_it =				(1 << 4) || (1 << 5) || (1 << 6),
	veml6075_conf_reserved =		1 << 7,
} veml6075_conf_bit_t;

typedef enum
{
	veml6075_conf_it_50ms = 		0b00000000,
	veml6075_conf_it_100ms = 		0b00010000,
	veml6075_conf_it_200ms = 		0b00100000,
	veml6075_conf_it_400ms = 		0b00110000,
	veml6075_conf_it_800ms = 		0b01000000,
} veml6075_integration_time_t;

typedef enum
{
	veml6075_id_vendor =			0x00,
	veml6075_id_device =			0x26,
} veml6075_id_t;


irom static i2c_error_t sensor_veml6075_uvindex_init(int bus, const device_table_entry_t *entry)
{
	i2c_error_t	error;
	uint8_t		i2c_buffer[2];

	if((error = i2c_send1_receive_repeated_start(entry->address, 0x0c, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	if((i2c_buffer[0] != veml6075_id_device) || (i2c_buffer[1] != veml6075_id_vendor))
		return(i2c_error_device_error_1);

	if(i2c_send2(entry->address, veml6075_reg_uv_conf, veml6075_conf_sd) != i2c_error_ok)
		return(i2c_error_device_error_2);

	if(i2c_send2(entry->address, veml6075_reg_uv_conf, veml6075_conf_it_100ms) != i2c_error_ok)
		return(i2c_error_device_error_3);

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_veml6075_uvindex_read(int bus, const device_table_entry_t *entry, value_t *value)
{
	/* "no teflon" values */
	static const double a = 2.22;
	static const double b = 1.33;
	static const double c = 2.95;
	static const double d = 1.74;
	static const double k1 = 1;
	static const double k2 = 1;
	static const double uvar = 0.001461;
	static const double uvbr = 0.002591;

	i2c_error_t	error;
	uint8_t i2c_buffer[2];
	unsigned int uva_data, uvb_data, uv_comp1_data, uv_comp2_data;
	double uva, uvb, uvia, uvib, uvi;

	if((error = i2c_send1_receive_repeated_start(entry->address, veml6075_reg_uv_uva_data, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	uva_data = (i2c_buffer[1] << 8) | i2c_buffer[0];

	if((error = i2c_send1_receive_repeated_start(entry->address, veml6075_reg_uv_uvb_data, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	uvb_data = (i2c_buffer[1] << 8) | i2c_buffer[0];

	if((error = i2c_send1_receive_repeated_start(entry->address, veml6075_reg_uv_uvcomp1_data, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	uv_comp1_data = (i2c_buffer[1] << 8) | i2c_buffer[0];

	if((error = i2c_send1_receive_repeated_start(entry->address, veml6075_reg_uv_uvcomp2_data, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	uv_comp2_data = (i2c_buffer[1] << 8) | i2c_buffer[0];

	uva	= (double)uva_data - (a * (double)uv_comp1_data) - (b * (double)uv_comp2_data);
	uvb	= (double)uvb_data - (c * (double)uv_comp1_data) - (d * (double)uv_comp2_data);

	if(uva < 0)
		uva = 0;

	if(uvb < 0)
		uvb = 0;

	uvia	= uva * k1 * uvar;
	uvib	= uvb * k2 * uvbr;
	uvi		= (uvia + uvib) / 2;

	value->raw = (unsigned int)uvia * 10000 + (unsigned int)uvib;
	value->cooked = uvi * 0.6;

	log("uva_data: %u, uvb_data: %u, uv_comp1_data: %u, uv_comp2_data: %u\n", uva_data, uvb_data, uv_comp1_data, uv_comp2_data);

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_veml6075_visible_light_init(int bus, const device_table_entry_t *entry)
{
	if(i2c_sensor_detected(bus, i2c_sensor_veml6075_uvindex))
		return(i2c_error_ok);

	return(i2c_error_address_nak);
}

irom static i2c_error_t sensor_veml6075_visible_light_read(int bus, const device_table_entry_t *entry, value_t *value)
{
	i2c_error_t	error;
	uint8_t i2c_buffer[2];

	if((error = i2c_send1_receive_repeated_start(entry->address, veml6075_reg_uv_uvcomp1_data, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	value->raw = (i2c_buffer[1] << 8) | i2c_buffer[0];
	value->cooked = 60 * value->raw;

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_veml6075_infrared_light_init(int bus, const device_table_entry_t *entry)
{
	if(i2c_sensor_detected(bus, i2c_sensor_veml6075_uvindex))
		return(i2c_error_ok);

	return(i2c_error_address_nak);
}

irom static i2c_error_t sensor_veml6075_infrared_light_read(int bus, const device_table_entry_t *entry, value_t *value)
{
	i2c_error_t	error;
	uint8_t i2c_buffer[2];

	if((error = i2c_send1_receive_repeated_start(entry->address, veml6075_reg_uv_uvcomp2_data, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	value->raw = (i2c_buffer[1] << 8) | i2c_buffer[0];
	value->cooked = 50 * value->raw;

	return(i2c_error_ok);
}

typedef enum
{
	mpl3115_reg_out_p =			0x01,
	mpl3115_reg_out_t =			0x04,
	mpl3115_reg_drstatus =		0x06,
	mpl3115_reg_whoami =		0x0c,
	mpl3115_reg_ptdatacfg =		0x13,
	mpl3115_reg_ctrl_reg1 =		0x26,
	mpl3115_reg_ctrl_reg2 =		0x27,
} mpl3115_reg_t;

typedef enum
{
	mpl3115_drstatus_tdr =	 	(1 << 1),
	mpl3115_drstatus_pdr =	 	(1 << 2),
	mpl3115_drstatus_ptdr = 	(1 << 3),
	mpl3115_drstatus_tow = 		(1 << 5),
	mpl3115_drstatus_pow = 		(1 << 6),
	mpl3115_drstatus_ptow = 	(1 << 7),
} mpl3115_drstatus_t;

typedef enum
{
	mpl3115_id_mpl3115a2 = 0xc4,
} mpl3115_id_t;

typedef enum
{
	mpl3115_ptdatacfg_tdefe =	(1 << 0),
	mpl3115_ptdatacfg_pdefe =	(1 << 1),
	mpl3115_ptdatacfg_drem =	(1 << 2),
} mpl3115_ptdatacfg_t;

typedef enum
{
	mpl3115_ctrl1_sbyb =	(1 << 0),
	mpl3115_ctrl1_ost =		(1 << 1),
	mpl3115_ctrl1_reset =	(1 << 2),
	mpl3115_ctrl1_os_1 =	(0 << 5) | (0 << 4) | (0 << 3),
	mpl3115_ctrl1_os_2 =	(0 << 5) | (0 << 4) | (1 << 3),
	mpl3115_ctrl1_os_4 =	(0 << 5) | (1 << 4) | (0 << 3),
	mpl3115_ctrl1_os_8 =	(0 << 5) | (1 << 4) | (1 << 3),
	mpl3115_ctrl1_os_16 =	(1 << 5) | (0 << 4) | (0 << 3),
	mpl3115_ctrl1_os_32 =	(1 << 5) | (0 << 4) | (1 << 3),
	mpl3115_ctrl1_os_64 =	(1 << 5) | (1 << 4) | (0 << 3),
	mpl3115_ctrl1_os_128 =	(1 << 5) | (1 << 4) | (1 << 3),
	mpl3115_ctrl1_raw =		(1 << 6),
	mpl3115_ctrl1_alt =		(1 << 7),
} mpl3115_ctrl1_t;

typedef enum
{
	mpl3115_ctrl2_st =		(1 << 0) | (1 << 1) | (1 << 2) | (1 << 3),
	mpl3115_ctrl2_alarm =	(1 << 4),
	mpl3115_crtl2_load =	(1 << 5),
} mpl3115_ctrl2_t;

irom static i2c_error_t sensor_mpl3115a2_temperature_init(int bus, const device_table_entry_t *entry)
{
	i2c_error_t error;
	uint8_t i2c_buffer;

	if(i2c_sensor_detected(bus, i2c_sensor_si114x_visible_light))
		return(i2c_error_device_error_1);

	if((error = i2c_send1_receive_repeated_start(entry->address, mpl3115_reg_whoami, 1, &i2c_buffer)) != i2c_error_ok)
		return(error);

	if(i2c_buffer != mpl3115_id_mpl3115a2)
		return(i2c_error_device_error_2);

	i2c_send2(entry->address, mpl3115_reg_ctrl_reg1, mpl3115_ctrl1_reset);

	msleep(1);

	if((error = i2c_send1_receive_repeated_start(entry->address, mpl3115_reg_ctrl_reg1, 1, &i2c_buffer)) != i2c_error_ok)
		return(error);

	if(i2c_buffer != 0x00)
		return(i2c_error_device_error_3);

	if((error = i2c_send2(entry->address, mpl3115_reg_ptdatacfg, mpl3115_ptdatacfg_tdefe | mpl3115_ptdatacfg_pdefe)) != i2c_error_ok)
		return(error);

	if((error = i2c_send2(entry->address, mpl3115_reg_ctrl_reg1, mpl3115_ctrl1_os_128)) != i2c_error_ok)
		return(error);

	if((error = i2c_send2(entry->address, mpl3115_reg_ctrl_reg2, (0x00 & mpl3115_ctrl2_st))) != i2c_error_ok) // auto acquisition step = 1 sec
		return(error);

	if((error = i2c_send2(entry->address, mpl3115_reg_ctrl_reg1, mpl3115_ctrl1_os_128 | mpl3115_ctrl1_sbyb)) != i2c_error_ok) // go to "active" auto operation
		return(error);

	if((error = i2c_send1_receive_repeated_start(entry->address, mpl3115_reg_ctrl_reg1, 1, &i2c_buffer)) != i2c_error_ok)
		return(error);

	if(i2c_buffer != (mpl3115_ctrl1_os_128 | mpl3115_ctrl1_sbyb))
		return(i2c_error_device_error_5);

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_mpl3115a2_airpressure_init(int bus, const device_table_entry_t *entry)
{
	if(i2c_sensor_detected(bus, i2c_sensor_mpl3115a2_temperature))
		return(i2c_error_ok);

	return(i2c_error_address_nak);
}

irom static i2c_error_t sensor_mpl3115a2_temperature_read(int bus, const device_table_entry_t *entry, value_t *value)
{
	uint8_t i2c_buffer[2];
	i2c_error_t error;

	if((error = i2c_send1_receive_repeated_start(entry->address, mpl3115_reg_out_t, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	value->raw = (i2c_buffer[0] << 8) | (i2c_buffer[1] << 0);
	value->cooked = value->raw / 256;

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_mpl3115a2_airpressure_read(int bus, const device_table_entry_t *entry, value_t *value)
{
	uint8_t i2c_buffer[3];
	i2c_error_t error;

	if((error = i2c_send1_receive_repeated_start(entry->address, mpl3115_reg_out_p, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	value->raw = (i2c_buffer[0] << 16 ) | (i2c_buffer[1] << 8) | (i2c_buffer[2] << 0);
	value->cooked = value->raw / 64 / 100;

	return(i2c_error_ok);
}

enum
{
	ccs811_reg_status		= 0x00,
	ccs811_reg_meas_mode	= 0x01,
	ccs811_reg_alg_result	= 0x02,
	ccs811_reg_hw_id		= 0x20,
	ccs811_reg_app_start	= 0xf4,
	ccs811_reg_reset		= 0xff,
};

enum
{
	ccs811_status_app_mode		= 0b10000000,
	ccs811_status_app_valid		= 0b00010000,
	ccs811_status_data_ready	= 0b00001000,
	ccs811_status_error			= 0b00000001,
};

enum
{
	ccs811_algdata_eco2		= 0x00,
	ccs811_algdata_tvoc		= 0x02,
	ccs811_algdata_status	= 0x04,
	ccs811_algdata_error_id	= 0x05,
	ccs811_raw_data			= 0x06,
};

enum
{
	ccs811_mm_0 =		0b00000000, // idle
	ccs811_mm_1 =		0b00010000,	// constant power 1/1s
	ccs811_mm_2 =		0b00100000,	// pulse heat 1/10s
	ccs811_mm_3 =		0b00110000,	// low power pulse heat 1/60s
	ccs811_mm_4 =		0b01000000,	// constant power 4/1s
	ccs811_mm_int_en =	0b00001000,	// enable interrupts
	ccs811_mm_int_thr =	0b00000100,	// use interrupts for threshold crossing
};

enum
{
	ccs811_hw_id		= 0x81,
};

enum
{
	ccs811_reset_seq_0	=	0x11,
	ccs811_reset_seq_1	=	0xe5,
	ccs811_reset_seq_2	=	0x72,
	ccs811_reset_seq_3	=	0x8a,
};

irom static i2c_error_t sensor_ccs811_co2_init(int bus, const device_table_entry_t *entry)
{
	i2c_error_t error;
	uint8_t i2c_buffer[8];

	if((error = i2c_send1_receive_repeated_start(entry->address, ccs811_reg_hw_id, 1, &i2c_buffer[0])) != i2c_error_ok)
		return(error);

	if(i2c_buffer[0] != ccs811_hw_id)
		return(i2c_error_address_nak);

	if((error = i2c_send1_receive_repeated_start(entry->address, ccs811_reg_status, 1, &i2c_buffer[0])) != i2c_error_ok)
		return(error);

	if(i2c_buffer[0] & ccs811_status_app_mode)
	{
		i2c_buffer[0] = ccs811_reg_reset;
		i2c_buffer[1] = ccs811_reset_seq_0;
		i2c_buffer[2] = ccs811_reset_seq_1;
		i2c_buffer[3] = ccs811_reset_seq_2;
		i2c_buffer[4] = ccs811_reset_seq_3;

		i2c_send(entry->address, 5, i2c_buffer);

		msleep(2);
	}

	if((error = i2c_send1_receive_repeated_start(entry->address, ccs811_reg_status, 1, &i2c_buffer[0])) != i2c_error_ok)
		return(error);

	if(!(i2c_buffer[0] & ccs811_status_app_valid))
		return(i2c_error_device_error_2);

	if((error = i2c_send1(entry->address, ccs811_reg_app_start)) != i2c_error_ok)
		return(error);

	msleep(2);

	if((error = i2c_send2(entry->address, ccs811_reg_meas_mode, ccs811_mm_1)) != i2c_error_ok)
		return(error);

	if((error = i2c_send1_receive_repeated_start(entry->address, ccs811_reg_status, 1, &i2c_buffer[0])) != i2c_error_ok)
		return(error);

	if(i2c_buffer[0] & ccs811_status_error)
		return(i2c_error_device_error_3);

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_ccs811_tov_init(int bus, const device_table_entry_t *entry)
{
	if(!i2c_sensor_detected(bus, i2c_sensor_ccs811_co2))
		return(i2c_error_address_nak);

	return(i2c_error_ok);
}

irom static i2c_error_t ccs811_read(int address, value_t *value_co2, value_t *value_tov)
{
	i2c_error_t error;
	uint8_t i2c_buffer[4];

	if(value_co2)
		value_co2->raw = value_co2->cooked = 0;

	if(value_tov)
		value_tov->raw = value_tov->cooked = 0;

	if((error = i2c_send1_receive_repeated_start(address, ccs811_reg_alg_result, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	if(value_co2)
		value_co2->raw = value_co2->cooked = (i2c_buffer[ccs811_algdata_eco2 + 0] << 8) | (i2c_buffer[ccs811_algdata_eco2 + 1] << 0);

	if(value_tov)
		value_tov->raw = value_tov->cooked = (i2c_buffer[ccs811_algdata_tvoc + 0] << 8) | (i2c_buffer[ccs811_algdata_tvoc + 1] << 0);

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_ccs811_co2_read(int bus, const device_table_entry_t *entry, value_t *value)
{
	return(ccs811_read(entry->address, value, (value_t *)0));
}

irom static i2c_error_t sensor_ccs811_tov_read(int bus, const device_table_entry_t *entry, value_t *value)
{
	return(ccs811_read(entry->address, (value_t *)0, value));
}

typedef enum
{
	hdc1080_reg_data_temp =	0x00,
	hdc1080_reg_data_hum =	0x01,
	hdc1080_reg_conf =		0x02,
	hdc1080_reg_serial1 =	0xfb,
	hdc1080_reg_serial2 =	0xfc,
	hdc1080_reg_serial3 =	0xfd,
	hdc1080_reg_man_id =	0xfe,
	hdc1080_reg_dev_id =	0xff,
} hdc1080_reg_t;

typedef enum
{
	hdc1080_conf_hres_8 =	(1 << 9) | (0 << 8),
	hdc1080_conf_hres_11 =	(0 << 9) | (1 << 8),
	hdc1080_conf_hres_14 =	(0 << 9) | (0 << 8),
	hdc1080_conf_tres_11 =	(1 << 10),
	hdc1080_conf_tres_14 =	(0 << 10),
	hdc1080_conf_btst =		(1 << 11),
	hdc1080_conf_mode_one =	(0 << 12),
	hdc1080_conf_mode_two =	(1 << 12),
	hdc1080_conf_heat =		(1 << 13),
	hdc1080_conf_reserve0 =	(0 << 14),
	hdc1080_conf_rst =		(1 << 15),
} hdc1080_conf_t;

typedef enum
{
	hdc1080_action_temperature,
	hdc1080_action_humidity,
} hdc1080_action_t;

enum
{
	hdc1080_max_attempts = 16
};

irom static i2c_error_t sensor_hdc1080_read(int bus, const device_table_entry_t *entry, value_t *value, hdc1080_action_t action)
{
	i2c_error_t error;
	int attempt;
	uint32_t conf;
	uint8_t i2c_buffer[3];
	uint8_t reg;

	value->cooked = value->raw = -1;

	conf = hdc1080_conf_tres_14 | hdc1080_conf_hres_14 | hdc1080_conf_mode_one;

	i2c_buffer[0] = hdc1080_reg_conf;
	i2c_buffer[1] = (conf & 0xff00) >> 8;
	i2c_buffer[2] = (conf & 0x00ff) >> 0;

	if((error = i2c_send(entry->address, 3, i2c_buffer)) != i2c_error_ok)
		return(error);

	if(action == hdc1080_action_temperature)
		reg = 0x00;
	else
		reg = 0x01;

	if((error = i2c_send1(entry->address, reg)) != i2c_error_ok)
		return(error);

	for(attempt = hdc1080_max_attempts; attempt > 0; attempt--)
	{
		if(i2c_receive(entry->address, 2, i2c_buffer) == i2c_error_ok)
			break;

		msleep(2);
	}

	if(attempt <= 0)
		return(i2c_error_device_error_1);

	value->raw = (i2c_buffer[0] << 8) + i2c_buffer[1];

	if(action == hdc1080_action_temperature)
		value->cooked = ((value->raw * 165) / (1 << 16)) - 40;
	else
		value->cooked = ((value->raw * 100) / (1 << 16));

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_hdc1080_temperature_init(int bus, const device_table_entry_t *entry)
{
	uint8_t i2c_buffer[2];
	i2c_error_t error;

	if(i2c_sensor_detected(bus, i2c_sensor_htu21_temperature)) // shares 0x40
		return(i2c_error_address_nak);

	if((error = i2c_send1_receive_repeated_start(entry->address, hdc1080_reg_man_id, 2, i2c_buffer)) != i2c_error_ok)
		return(error);

	if((i2c_buffer[0] != 0x54) || (i2c_buffer[1] != 0x49))
		return(i2c_error_device_error_1);

	if((error = i2c_send1_receive_repeated_start(entry->address, hdc1080_reg_dev_id, 2, i2c_buffer)) != i2c_error_ok)
		return(error);

	if((i2c_buffer[0] != 0x10) || (i2c_buffer[1] != 0x50))
		return(i2c_error_device_error_2);

	if((error = i2c_send1_receive_repeated_start(entry->address, 0xff, 2, i2c_buffer)) != i2c_error_ok)
		return(error);

	if((error = i2c_send2(entry->address, hdc1080_reg_conf, hdc1080_conf_rst)) != i2c_error_ok)
		return(error);

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_hdc1080_humidity_init(int bus, const device_table_entry_t *entry)
{
	if(i2c_sensor_detected(bus, i2c_sensor_hdc1080_temperature))
		return(i2c_error_ok);

	return(i2c_error_address_nak);
}

irom static i2c_error_t sensor_hdc1080_temperature_read(int bus, const device_table_entry_t *entry, value_t *value)
{
	return(sensor_hdc1080_read(bus, entry, value, hdc1080_action_temperature));
}

irom static i2c_error_t sensor_hdc1080_humidity_read(int bus, const device_table_entry_t *entry, value_t *value)
{
	return(sensor_hdc1080_read(bus, entry, value, hdc1080_action_humidity));
}

typedef enum
{
	hih6130_action_temperature,
	hih6130_action_humidity,
} hih6130_action_t;

typedef enum
{
	hih6130_status_normal =	(0 << 7) | (0 << 6),
	hih6130_status_stale =	(0 << 7) | (1 << 6),
	hih6130_status_cmd =	(1 << 7) | (0 << 6),
	hih6130_status_mask =	(1 << 7) | (1 << 6),
} hih6130_status_t;

enum
{
	hih6130_max_attempts = 3,
};

irom static i2c_error_t sensor_hih6130_read(int bus, const device_table_entry_t *entry, value_t *value, hih6130_action_t action)
{
	uint8_t i2c_buffer[4];
	i2c_error_t error;
	int attempt;

	value->cooked = value->raw = -1;

	if((error = i2c_send(entry->address, 0, 0)) != i2c_error_ok)
		return(error);

	for(attempt = hih6130_max_attempts; attempt > 0; attempt--)
	{
		if((i2c_receive(entry->address, 4, i2c_buffer) == i2c_error_ok) && ((i2c_buffer[0] & hih6130_status_mask) == hih6130_status_normal))
			break;

		logfmt("hih6130: retry %d\n", attempt);

		msleep(20);
	}

	if(attempt <= 0)
		return(i2c_error_device_error_1);

	i2c_buffer[0] &= ~hih6130_status_mask;

	if(action == hih6130_action_temperature)
	{
		value->raw = ((i2c_buffer[2] << 8) + i2c_buffer[3]) >> 2;
		value->cooked = ((value->raw * 165) / ((1 << 14) - 2)) - 40;
	}
	else
	{
		value->raw = ((i2c_buffer[0] << 8) + i2c_buffer[1]) & 0b0011111111111111;
		value->cooked = (value->raw * 100) / ((1 << 14) - 2);
	}

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_hih6130_temperature_init(int bus, const device_table_entry_t *entry)
{
	uint8_t i2c_buffer[4];
	i2c_error_t error;

	if((error = i2c_receive(entry->address, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_hih6130_humidity_init(int bus, const device_table_entry_t *entry)
{
	if(i2c_sensor_detected(bus, i2c_sensor_hih6130_temperature))
		return(i2c_error_ok);

	return(i2c_error_address_nak);
}

irom static i2c_error_t sensor_hih6130_temperature_read(int bus, const device_table_entry_t *entry, value_t *value)
{
	return(sensor_hih6130_read(bus, entry, value, hih6130_action_temperature));
}

irom static i2c_error_t sensor_hih6130_humidity_read(int bus, const device_table_entry_t *entry, value_t *value)
{
	return(sensor_hih6130_read(bus, entry, value, hih6130_action_humidity));
}

typedef enum
{
	sht30_cmd_single_meas_clock_high =		0x2c06,
	sht30_cmd_single_meas_clock_medium =	0x2c0d,
	sht30_cmd_single_meas_clock_low =		0x2c10,

	sht30_cmd_single_meas_noclock_high =	0x2400,
	sht30_cmd_single_meas_noclock_medium =	0x240b,
	sht30_cmd_single_meas_noclock_low =		0x2416,

	sht30_cmd_auto_meas_high_05 =			0x2032,
	sht30_cmd_auto_meas_medium_05 =			0x2024,
	sht30_cmd_auto_meas_low_05 =			0x202f,

	sht30_cmd_auto_meas_high_1 =			0x2130,
	sht30_cmd_auto_meas_medium_1 =			0x2126,
	sht30_cmd_auto_meas_low_1 =				0x212d,

	sht30_cmd_auto_meas_high_2 =			0x2236,
	sht30_cmd_auto_meas_medium_2 =			0x2220,
	sht30_cmd_auto_meas_low_2 =				0x222b,

	sht30_cmd_auto_meas_high_4 =			0x2334,
	sht30_cmd_auto_meas_medium_4 =			0x2322,
	sht30_cmd_auto_meas_low_4 =				0x2329,

	sht30_cmd_auto_meas_high_10 =			0x2737,
	sht30_cmd_auto_meas_medium_10 =			0x2721,
	sht30_cmd_auto_meas_low_10 =			0x272a,

	sht30_cmd_fetch_data =					0xe000,
	sht30_cmd_art =							0x2b32,
	sht30_cmd_break =						0x3093,
	sht30_cmd_reset =						0x30a2,
	sht30_cmd_heater_en =					0x306d,
	sht30_cmd_heater_dis =					0x3066,
	sht30_cmd_read_status =					0xf32d,
	sht30_cmd_clear_status =				0x3041,
} sht30_cmd_t;

typedef enum
{
	sht30_status_none =				0x00,
	sht30_status_write_checksum =	(1 << 0),
	sht30_status_command_status =	(1 << 1),
	sht30_status_reset_detected =	(1 << 4),
	sht30_status_temp_track_alert =	(1 << 10),
	sht30_status_hum_track_alert =	(1 << 11),
	sht30_status_heater =			(1 << 13),
	sht30_status_alert =			(1 << 15),
} sht30_status_t;

enum
{
	sht30_inter_access_delay = 500,
};

irom attr_pure static uint8_t sht30_crc(int length, const uint8_t *data)
{
	uint8_t outer, inner, testbit, crc;

	crc = 0xff;

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

irom static i2c_error_t sht30_register_access(int address, sht30_cmd_t cmd, int *result1, int *result2)
{
	i2c_error_t error;
	uint8_t i2c_buffer[8];	// 2 + 3 + 3
	uint8_t crc_local, crc_remote;

	i2c_buffer[0] = (cmd & 0xff00) >> 8;
	i2c_buffer[1] = (cmd & 0x00ff) >> 0;

	usleep(sht30_inter_access_delay);

	if((error = i2c_send(address, 2, &i2c_buffer[0])) != i2c_error_ok)
		return(error);

	if(result1)
	{
		usleep(sht30_inter_access_delay);

		if((error = i2c_receive(address, result2 ? 6 : 3, &i2c_buffer[2])) != i2c_error_ok)
			return(error);

		crc_local = i2c_buffer[4];
		crc_remote = sht30_crc(2, &i2c_buffer[2]);

		if(crc_local != crc_remote)
			return(i2c_error_device_error_1);

		*result1 = (i2c_buffer[2] << 8) | i2c_buffer[3];

		if(result2)
		{
			crc_local = i2c_buffer[7];
			crc_remote = sht30_crc(2, &i2c_buffer[5]);

			if(crc_local != crc_remote)
				return(error);

			*result2 = (i2c_buffer[5] << 8) | i2c_buffer[6];
		}
	}

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_sht30_temperature_init(int bus, const device_table_entry_t *entry)
{
	int result;
	i2c_error_t error;

	if((error = sht30_register_access(entry->address, sht30_cmd_break, 0, 0)) != i2c_error_ok)
		return(error);

	if((error = sht30_register_access(entry->address, sht30_cmd_reset, 0, 0)) != i2c_error_ok)
		return(error);

	if((error = sht30_register_access(entry->address, sht30_cmd_read_status, &result, 0)) != i2c_error_ok)
		return(error);

	if((result & (sht30_status_write_checksum | sht30_status_command_status)) != 0x00)
		return(i2c_error_device_error_1);

	if((error = sht30_register_access(entry->address, sht30_cmd_clear_status, 0, 0)) != i2c_error_ok)
		return(error);

	if((error = sht30_register_access(entry->address, sht30_cmd_auto_meas_high_1, 0, 0)) != i2c_error_ok)
		return(error);

	if((error = sht30_register_access(entry->address, sht30_cmd_read_status, &result, 0)) != i2c_error_ok)
		return(error);

	if((result & (sht30_status_write_checksum | sht30_status_command_status | sht30_status_reset_detected)) != 0x00)
		return(i2c_error_device_error_2);

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_sht30_humidity_init(int bus, const device_table_entry_t *entry)
{
	if(i2c_sensor_detected(bus, i2c_sensor_sht30_temperature))
		return(i2c_error_ok);

	return(i2c_error_address_nak);
}

static value_t sht30_temperature_cache = { 0, 0 };
static value_t sht30_humidity_cache = { -1, -1 };

irom static i2c_error_t sht30_read(int address, value_t *value_temperature, value_t *value_humidity)
{
	i2c_error_t error;
	int result1, result2;

	if((error = sht30_register_access(address, sht30_cmd_fetch_data, &result1, &result2)) != i2c_error_ok)
	{
		if(sht30_humidity_cache.raw < 0)
		{
			if(value_temperature)
				value_temperature->raw = value_temperature->cooked = 0;

			if(value_humidity)
				value_humidity->raw = value_humidity->cooked = -1;

			return(i2c_error_device_error_1);
		}

		if(value_temperature)
			*value_temperature = sht30_temperature_cache;

		if(value_humidity)
			*value_humidity = sht30_humidity_cache;

		return(i2c_error_ok);
	}

	sht30_temperature_cache.raw = result1;
	sht30_temperature_cache.cooked = ((sht30_temperature_cache.raw * 175) / ((1 << 16) - 1)) - 45;

	sht30_humidity_cache.raw = result2;
	sht30_humidity_cache.cooked = (sht30_humidity_cache.raw * 100) / ((1 << 16) - 1);

	if(value_temperature)
		*value_temperature = sht30_temperature_cache;

	if(value_humidity)
		*value_humidity = sht30_humidity_cache;

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_sht30_temperature_read(int bus, const device_table_entry_t *entry, value_t *value)
{
	return(sht30_read(entry->address, value, (value_t *)0));
}

irom static i2c_error_t sensor_sht30_humidity_read(int bus, const device_table_entry_t *entry, value_t *value)
{
	return(sht30_read(entry->address, (value_t *)0, value));
}

enum
{
	mcp9808_reg_rfu =			0b00000000,
	mcp9808_reg_config =		0b00000001,
	mcp9808_reg_alert_u =		0b00000010,
	mcp9808_reg_alert_l =		0b00000011,
	mcp9808_reg_critical =		0b00000100,
	mcp9808_reg_temperature =	0b00000101,
	mcp9808_reg_manufacturer =	0b00000110,
	mcp9808_reg_device_id =		0b00000111,
	mcp9808_reg_resolution =	0b00001000,
};

enum
{
	mcp9808_config_hyst_0_0 =		0b0000000000000000,
	mcp9808_config_hyst_1_5 =		0b0000001000000000,
	mcp9808_config_hyst_3_0 =		0b0000010000000000,
	mcp9808_config_hyst_6_0 =		0b0000011000000000,
	mcp9808_config_shutdown =		0b0000000100000000,
	mcp9808_config_lock_crit =		0b0000000010000000,
	mcp9808_config_lock_wind =		0b0000000001000000,
	mcp9808_config_int_clear =		0b0000000000100000,
	mcp9808_config_alert_status =	0b0000000000010000,
	mcp9808_config_alert_control =	0b0000000000001000,
	mcp9808_config_alert_select =	0b0000000000000100,
	mcp9808_config_alert_pol =		0b0000000000000010,
	mcp9808_config_alert_mode =		0b0000000000000001,
};

enum
{
	mcp9808_resolution_0_5 =		0b00000000,
	mcp9808_resolution_0_25 =		0b00000001,
	mcp9808_resolution_0_125 =		0b00000010,
	mcp9808_resolution_0_0625 =		0b00000011,
};

enum
{
	mcp9808_manufacturer_id_0 =		0x00,
	mcp9808_manufacturer_id_1 =		0x54,
	mcp9808_device_id =				0x04,
};

irom static i2c_error_t sensor_mcp9808_init(int bus, const device_table_entry_t *entry)
{
	i2c_error_t error;
	uint8_t i2c_buffer[2];

	if((error = i2c_send1_receive_repeated_start(entry->address, mcp9808_reg_manufacturer, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	if((i2c_buffer[0] != mcp9808_manufacturer_id_0) || (i2c_buffer[1] != mcp9808_manufacturer_id_1))
		return(i2c_error_device_error_1);

	if((error = i2c_send1_receive_repeated_start(entry->address, mcp9808_reg_device_id, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	if((i2c_buffer[0] != mcp9808_device_id))
		return(i2c_error_device_error_2);

	if((error = i2c_send2(entry->address, mcp9808_reg_config, mcp9808_config_int_clear)) != i2c_error_ok)
		return(error);

	if((error = i2c_send2(entry->address, mcp9808_reg_resolution, mcp9808_resolution_0_0625)) != i2c_error_ok)
		return(error);

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_mcp9808_read(int bus, const device_table_entry_t *entry, value_t *value)
{
	i2c_error_t error;
	uint8_t i2c_buffer[2];
	unsigned int raw;

	if((error = i2c_send1_receive_repeated_start(entry->address, mcp9808_reg_temperature, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	raw = (i2c_buffer[0] << 8) | (i2c_buffer[1] << 0);
	value->raw = raw;
	value->cooked = (raw & 0x0fff) / 16.0;

	if(raw & (1 << 12))
		value->cooked = 256 - value->cooked;

	return(i2c_error_ok);
}

static const device_table_entry_t device_table[] =
{
	{
		i2c_sensor_digipicco_temperature, 0x78,
		"digipicco", "temperature", "C", 2,
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
		"lm75 compatible #0", "temperature", "C", 2,
		sensor_lm75_init,
		sensor_lm75_read
	},
	{
		i2c_sensor_lm75_1, 0x49,
		"lm75 compatible #1", "temperature", "C", 2,
		sensor_lm75_init,
		sensor_lm75_read
	},
	{
		i2c_sensor_lm75_2, 0x4a,
		"lm75 compatible #2", "temperature", "C", 2,
		sensor_lm75_init,
		sensor_lm75_read
	},
	{
		i2c_sensor_lm75_3, 0x4b,
		"lm75 compatible #3", "temperature", "C", 2,
		sensor_lm75_init,
		sensor_lm75_read
	},
	{
		i2c_sensor_ds1631_6, 0x4e,
		"ds1621/ds1631/ds1731", "temperature", "C", 2,
		sensor_ds1631_init,
		sensor_ds1631_read
	},
	{
		i2c_sensor_lm75_7, 0x4f,
		"lm75 compatible #7", "temperature", "C", 2,
		sensor_lm75_init,
		sensor_lm75_read
	},
	{
		i2c_sensor_bmp085_temperature, 0x77,
		"bmp085/bmp180", "temperature", "C", 2,
		sensor_bmp085_init_temp,
		sensor_bmp085_read_temp
	},
	{
		i2c_sensor_bmp085_airpressure, 0x77,
		"bmp085/bmp180", "pressure", "hPa", 2,
		sensor_bmp085_init_pressure,
		sensor_bmp085_read_pressure
	},
	{
		i2c_sensor_tsl2561_0, 0x39,
		"tsl2561 #0", "visible light", "lx", 2,
		sensor_tsl2561_init,
		sensor_tsl2561_read,
	},
	{
		i2c_sensor_tsl2550, 0x39,
		"tsl2550", "visible light", "lx", 2,
		sensor_tsl2550_init,
		sensor_tsl2550_read
	},
	{
		i2c_sensor_bh1750, 0x23,
		"bh1750", "visible light", "lx", 2,
		sensor_bh1750_init,
		sensor_bh1750_read
	},
	{
		i2c_sensor_htu21_temperature, 0x40,
		"htu21", "temperature", "C", 2,
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
		i2c_sensor_am2320_temperature, 0x5c,
		"am2320/1/2", "temperature", "C", 2,
		sensor_am2320_temp_init,
		sensor_am2320_temp_read
	},
	{
		i2c_sensor_am2320_humidity, 0x5c,
		"am2320/1/2", "humidity", "%", 0,
		sensor_am2320_hum_init,
		sensor_am2320_hum_read
	},
	{
		i2c_sensor_veml6070, 0x38,
		"veml6070", "ultraviolet light index", "", 1,
		sensor_veml6070_init,
		sensor_veml6070_read
	},
	{
		i2c_sensor_si114x_visible_light, 0x60,
		"si114x", "visible light", "lx", 1,
		sensor_si114x_visible_light_init,
		sensor_si114x_visible_light_read,
	},
	{
		i2c_sensor_si114x_infrared, 0x60,
		"si114x", "infrared light", "lx", 1,
		sensor_si114x_infrared_init,
		sensor_si114x_infrared_read,
	},
	{
		i2c_sensor_si114x_ultraviolet, 0x60,
		"si114x", "ultraviolet light index", "", 1,
		sensor_si114x_ultraviolet_init,
		sensor_si114x_ultraviolet_read,
	},
	{
		i2c_sensor_bme280_temperature, 0x76,
		"bmp280/bme280", "temperature", "C", 2,
		sensor_bme280_temperature_init,
		sensor_bme280_temperature_read,
	},
	{
		i2c_sensor_bme280_humidity, 0x76,
		"bmp280/bme280", "humidity", "%", 1,
		sensor_bme280_humidity_init,
		sensor_bme280_humidity_read,
	},
	{
		i2c_sensor_bme280_airpressure, 0x76,
		"bmp280/bme280", "pressure", "hPa", 2,
		sensor_bme280_airpressure_init,
		sensor_bme280_airpressure_read,
	},
	{
		i2c_sensor_tsl2561_1, 0x29,
		"tsl2561 #1", "visible light", "lx", 2,
		sensor_tsl2561_init,
		sensor_tsl2561_read,
	},
	{
		i2c_sensor_max44009_0, 0x4a,
		"max44009 #0", "visible light", "lx", 2,
		sensor_max44009_init,
		sensor_max44009_read,
	},
	{
		i2c_sensor_veml6075_uvindex, 0x10,
		"veml6075", "ultraviolet light index", "", 1,
		sensor_veml6075_uvindex_init,
		sensor_veml6075_uvindex_read,
	},
	{
		i2c_sensor_mpl3115a2_temperature, 0x60,
		"mpl3115a2", "temperature", "C", 2,
		sensor_mpl3115a2_temperature_init,
		sensor_mpl3115a2_temperature_read,
	},
	{
		i2c_sensor_mpl3115a2_airpressure, 0x60,
		"mpl3115a2", "pressure", "hPa", 2,
		sensor_mpl3115a2_airpressure_init,
		sensor_mpl3115a2_airpressure_read,
	},
	{
		i2c_sensor_ccs811_co2, 0x5a,
		"ccs811", "co2", "ppm", 0,
		sensor_ccs811_co2_init,
		sensor_ccs811_co2_read,
	},
	{
		i2c_sensor_ccs811_tov, 0x5a,
		"ccs811", "tov", "ppm", 0,
		sensor_ccs811_tov_init,
		sensor_ccs811_tov_read,
	},
	{
		i2c_sensor_veml6075_visible_light, 0x10,
		"veml6075", "visible light", "", 2,
		sensor_veml6075_visible_light_init,
		sensor_veml6075_visible_light_read,
	},
	{
		i2c_sensor_veml6075_infrared_light, 0x10,
		"veml6075", "infrared light", "", 2,
		sensor_veml6075_infrared_light_init,
		sensor_veml6075_infrared_light_read,
	},
	{
		i2c_sensor_si114x_temperature, 0x60,
		"si114x", "temperature", "C", 1,
		sensor_si114x_temperature_init,
		sensor_si114x_temperature_read,
	},
	{
		i2c_sensor_hdc1080_temperature, 0x40,
		"hdc1080", "temperature", "C", 2,
		sensor_hdc1080_temperature_init,
		sensor_hdc1080_temperature_read,
	},
	{
		i2c_sensor_hdc1080_humidity, 0x40,
		"hdc1080", "humidity", "", 0,
		sensor_hdc1080_humidity_init,
		sensor_hdc1080_humidity_read,
	},
	{
		i2c_sensor_hih6130_temperature, 0x27,
		"hih6130", "temperature", "C", 2,
		sensor_hih6130_temperature_init,
		sensor_hih6130_temperature_read,
	},
	{
		i2c_sensor_hih6130_humidity, 0x27,
		"hih6130", "humidity", "", 0,
		sensor_hih6130_humidity_init,
		sensor_hih6130_humidity_read,
	},
	{
		i2c_sensor_sht30_temperature, 0x44,
		"sht30", "temperature", "C", 2,
		sensor_sht30_temperature_init,
		sensor_sht30_temperature_read,
	},
	{
		i2c_sensor_sht30_humidity, 0x44,
		"sht30", "humidity", "", 0,
		sensor_sht30_humidity_init,
		sensor_sht30_humidity_read,
	},
	{
		i2c_sensor_mcp9808_temperature, 0x18,
		"mcp9808", "temperature", "C", 2,
		sensor_mcp9808_init,
		sensor_mcp9808_read,
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
	i2c_info_t i2c_info;
	unsigned int bus, buses;
	i2c_sensor_t current;

	i2c_get_info(&i2c_info);
	buses = i2c_info.buses;

	for(bus = 0; bus < buses; bus++)
		for(current = 0; current != i2c_sensor_size; current++)
			if((bus == 0) || !(device_data[current].detected & (1 << 0)))
				i2c_sensor_init(bus, current);
}

irom bool_t i2c_sensor_read(string_t *dst, int bus, i2c_sensor_t sensor, bool_t verbose, bool_t html)
{
	const device_table_entry_t *entry;
	i2c_error_t error;
	value_t value;
	int current;
	int int_factor, int_offset;
	double extracooked;
	string_init(varname_i2s_factor, "i2s.%u.%u.factor");
	string_init(varname_i2s_offset, "i2s.%u.%u.offset");

	for(current = 0; current < i2c_sensor_size; current++)
	{
		entry = &device_table[current];

		if(sensor == entry->id)
			break;
	}

	if((current >= i2c_sensor_size) || (entry->read_fn == (void *)0))
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

	if(html)
		string_format(dst, "%u</td><td align=\"right\">%u</td><td align=\"right\">0x%02x</td><td>%s</td><td>%s</td>", bus, sensor, entry->address, entry->name, entry->type);
	else
		string_format(dst, "%s sensor %u/%02u@%02x: %s, %s: ", device_data[sensor].detected ? "+" : " ", bus, sensor, entry->address, entry->name, entry->type);

	if((error = entry->read_fn(bus, entry, &value)) == i2c_error_ok)
	{
		if(!config_get_int(&varname_i2s_factor, bus, sensor, &int_factor))
			int_factor = 1000;

		if(!config_get_int(&varname_i2s_offset, bus, sensor, &int_offset))
			int_offset = 0;

		extracooked = (value.cooked * int_factor / 1000.0) + (int_offset / 1000.0);

		if(html)
		{
			string_append(dst, "<td align=\"right\">");
			string_double(dst, extracooked, entry->precision, 1e10);
			string_append(dst, " ");
			string_format(dst, "%s", entry->unity);
		}
		else
		{
			string_append(dst, "[");
			string_double(dst, extracooked, entry->precision, 1e10);
			string_append(dst, "]");
			string_format(dst, " %s", entry->unity);
		}

		if(verbose)
		{
			string_append(dst, " (uncalibrated: ");
			string_double(dst, value.cooked, entry->precision, 1e10);
			string_append(dst, ", raw: ");
			string_double(dst, value.raw, 0, 1e10);
			string_append(dst, ")");
		}
	}
	else
	{
		if(verbose)
		{
			string_append(dst, "error");
			i2c_error_format_string(dst, error);
		}
		else
			string_append(dst, "error");
	}

	if(verbose)
	{
		if(!config_get_int(&varname_i2s_factor, bus, sensor, &int_factor))
			int_factor = 1000;

		if(!config_get_int(&varname_i2s_offset, bus, sensor, &int_offset))
			int_offset = 0;

		string_append(dst, ", calibration: factor=");
		string_double(dst, int_factor / 1000.0, 4, 1e10);
		string_append(dst, ", offset=");
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
