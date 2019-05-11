#include "i2c_sensor.h"

#include "util.h"
#include "config.h"
#include "sys_time.h"

#include <stdint.h>
#include <stdbool.h>

static i2c_sensor_device_data_t device_data[i2c_sensor_size];

static void sensor_register(int bus, i2c_sensor_t sensor_id)
{
	if(sensor_id >= i2c_sensor_size)
		return;

	device_data[sensor_id].registered |= (1 << bus);
}

static void sensor_deregister(int bus, i2c_sensor_t sensor_id)
{
	if(sensor_id >= i2c_sensor_size)
		return;

	device_data[sensor_id].registered &= ~(1 << bus);
}

attr_pure bool i2c_sensor_registered(int bus, i2c_sensor_t sensor)
{
	if(sensor >= i2c_sensor_size)
		return(false);

	return(!!(device_data[sensor].registered & (1 << bus)));
}

static int signed_8(unsigned int lsb)
{
	int rv = lsb & 0xff;

	if(rv > (1 << 7))
		rv = 0 - ((1 << 8) - rv);

	return(rv);
}

static unsigned int unsigned_8(unsigned int lsb)
{
	return(lsb & 0xff);
}

static unsigned int unsigned_12(unsigned int msb, unsigned int lsb)
{
	return(((msb & 0xff) << 4) | ((lsb & 0x0f) << 0));
}

static int signed_16(unsigned int msb, unsigned int lsb)
{
	int rv = ((msb & 0xff) << 8) | (lsb & 0xff);

	if(rv > (1 << 15))
		rv = 0 - ((1 << 16) - rv);

	return(rv);
}

static unsigned int unsigned_16(unsigned int msb, unsigned int lsb)
{
	return(((msb & 0xff) << 8) | (lsb & 0xff));
}

static unsigned int unsigned_20(unsigned int msb, unsigned int lsb, unsigned int xlsb)
{
	return(((msb & 0xff) << 12) | ((lsb & 0xff) << 4) | (xlsb & 0xff) >> 4);
}

enum
{
	opt3001_reg_result =		0x00,
	opt3001_reg_conf =			0x01,
	opt3001_reg_limit_low =		0x02,
	opt3001_reg_limit_high =	0x03,
	opt3001_reg_id_manuf =		0x7e,
	opt3001_reg_id_dev =		0x7f,
} opt3001_register;

enum
{
	opt3001_id_manuf_ti =		0x5449,
	opt3001_id_dev_opt3001 =	0x3001,
} opt3001_id;

enum
{
	opt3001_conf_fault_count =		0b0000000000000011,
	opt3001_conf_mask_exp =			0b0000000000000100,
	opt3001_conf_pol =				0b0000000000001000,
	opt3001_conf_latch =			0b0000000000010000,
	opt3001_conf_flag_low =			0b0000000000100000,
	opt3001_conf_flag_high =		0b0000000001000000,
	opt3001_conf_flag_ready =		0b0000000010000000,
	opt3001_conf_flag_ovf =			0b0000000100000000,
	opt3001_conf_conv_mode =		0b0000011000000000,
	opt3001_conf_conv_time =		0b0000100000000000,
	opt3001_conf_range =			0b1111000000000000,

	opt3001_conf_range_auto =		0b1100000000000000,
	opt3001_conf_conv_time_100 =	0b0000000000000000,
	opt3001_conf_conv_time_800 =	0b0000100000000000,
	opt3001_conf_conv_mode_shut =	0b0000000000000000,
	opt3001_conf_conv_mode_single =	0b0000001000000000,
	opt3001_conf_conv_mode_cont =	0b0000011000000000,
} opt3001_conf;

static i2c_error_t sensor_opt3001_init(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_device_data_t *data)
{
	i2c_error_t error;
	uint8_t i2c_buffer[4];
	unsigned int config;

	if((error = i2c_send1_receive(entry->address, opt3001_reg_id_manuf, 2, i2c_buffer)) != i2c_error_ok)
		return(error);

	config = (i2c_buffer[0] << 8) | (i2c_buffer[1] << 0);

	if(config != opt3001_id_manuf_ti)
		return(i2c_error_device_error_1);

	if((error = i2c_send1_receive(entry->address, opt3001_reg_id_dev, 2, i2c_buffer)) != i2c_error_ok)
		return(error);

	config = (i2c_buffer[0] << 8) | (i2c_buffer[1] << 0);

	if(config != opt3001_id_dev_opt3001)
		return(i2c_error_device_error_2);

	config = opt3001_conf_range_auto | opt3001_conf_conv_time_800 | opt3001_conf_conv_mode_cont;

	i2c_buffer[0] = opt3001_reg_conf;
	i2c_buffer[1] = (config & 0xff00) >> 8;
	i2c_buffer[2] = (config & 0x00ff) >> 0;

	if((error = i2c_send(entry->address, 3, i2c_buffer)) != i2c_error_ok)
		return(error);

	if((error = i2c_send1_receive(entry->address, opt3001_reg_conf, 2, i2c_buffer)) != i2c_error_ok)
		return(error);

	config = ((i2c_buffer[0] << 8) | (i2c_buffer[1] << 0)) & (opt3001_conf_mask_exp | opt3001_conf_conv_mode | opt3001_conf_conv_time | opt3001_conf_range);

	if(config != (opt3001_conf_range_auto | opt3001_conf_conv_time_800 | opt3001_conf_conv_mode_cont))
		return(i2c_error_device_error_3);

	sensor_register(bus, entry->id);

	return(i2c_error_ok);
}

static i2c_error_t sensor_opt3001_read(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_value_t *value, i2c_sensor_device_data_t *data)
{
	i2c_error_t	error;
	uint8_t i2c_buffer[2];
	unsigned int config, exponent, mantissa;

	if((error = i2c_send1_receive(entry->address, opt3001_reg_conf, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	config = (i2c_buffer[0] << 8) | (i2c_buffer[1] << 0);

	if(!(config & opt3001_conf_flag_ready))
		return(i2c_error_device_error_1);

	if(config & opt3001_conf_flag_ovf)
	{
		value->raw = value->cooked = -1;
		return(i2c_error_ok);
	}

	if((error = i2c_send1_receive(entry->address, opt3001_reg_result, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	exponent = (i2c_buffer[0] & 0xf0) >> 4;
	mantissa = ((i2c_buffer[0] & 0x0f) << 8) | i2c_buffer[1];

	value->raw = exponent * 10000 + mantissa;
	value->cooked = 0.01 * (1 << exponent) * mantissa;

	return(i2c_error_ok);
}

typedef enum
{
	veml6075_reg_uv_conf =			0x00,
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
	veml6075_conf_it_50ms =			0b00000000,
	veml6075_conf_it_100ms =		0b00010000,
	veml6075_conf_it_200ms =		0b00100000,
	veml6075_conf_it_400ms =		0b00110000,
	veml6075_conf_it_800ms =		0b01000000,
} veml6075_integration_time_t;

typedef enum
{
	veml6075_id_vendor =			0x00,
	veml6075_id_device =			0x26,
} veml6075_id_t;


static i2c_error_t sensor_veml6075_uvindex_init(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_device_data_t *data)
{
	i2c_error_t	error;
	uint8_t		i2c_buffer[2];

	if((error = i2c_send1_receive(entry->address, 0x0c, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	if((i2c_buffer[0] != veml6075_id_device) || (i2c_buffer[1] != veml6075_id_vendor))
		return(i2c_error_device_error_1);

	if(i2c_send2(entry->address, veml6075_reg_uv_conf, veml6075_conf_sd) != i2c_error_ok)
		return(i2c_error_device_error_2);

	if(i2c_send2(entry->address, veml6075_reg_uv_conf, veml6075_conf_it_100ms) != i2c_error_ok)
		return(i2c_error_device_error_3);

	sensor_register(bus, entry->id);

	return(i2c_error_ok);
}

static i2c_error_t sensor_veml6075_uvindex_read(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_value_t *value, i2c_sensor_device_data_t *data)
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

	if((error = i2c_send1_receive(entry->address, veml6075_reg_uv_uva_data, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	uva_data = (i2c_buffer[1] << 8) | i2c_buffer[0];

	if((error = i2c_send1_receive(entry->address, veml6075_reg_uv_uvb_data, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	uvb_data = (i2c_buffer[1] << 8) | i2c_buffer[0];

	if((error = i2c_send1_receive(entry->address, veml6075_reg_uv_uvcomp1_data, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	uv_comp1_data = (i2c_buffer[1] << 8) | i2c_buffer[0];

	if((error = i2c_send1_receive(entry->address, veml6075_reg_uv_uvcomp2_data, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
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
	value->cooked = uvi * 1.25;

	return(i2c_error_ok);
}

enum
{
	tmd2771_reg_enable =		0x80,
	tmd2771_reg_atime =			0x81,
	tmd2771_reg_wtime =			0x83,
	tmd2771_reg_config =		0x8d,
	tmd2771_reg_control =		0x8f,
	tmd2771_reg_id =			0x92,
	tmd2771_reg_status =		0x93,
	tmd2771_reg_c0data =		0x94,
	tmd2771_reg_c0datah =		0x95,
	tmd2771_reg_c1data =		0x96,
	tmd2771_reg_c1datah =		0x97,

	tmd2771_autoincr =			0b00100000,

	tmd2771_enable_wen =		0b00001000,
	tmd2771_enable_aen =		0b00000010,
	tmd2771_enable_pon =		0b00000001,
	tmd2771_enable_poff =		0b00000000,

	tmd2771_atime_174 =			0xc0,
	tmd2771_atime_696 =			0x00,

	tmd2771_wtime_200 =			0xb6,

	tmd2771_config_none =		0b00000000,

	tmd2771_ctrl_pdrive_100 =	0b00000000,
	tmd2771_ctrl_pdiode_pca =	0b00110000,
	tmd2771_ctrl_again_1 =		0b00000000,
	tmd2771_ctrl_again_16 =		0b00000010,

	tmd2771_id_tmd27711 =		0x20,
	tmd2771_id_tmd27713 =		0x29,

	tmd2771_status_avalid =		0b00000001,
};

static i2c_error_t sensor_tmd2771_init(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_device_data_t *data)
{
	uint8_t i2c_buffer[1];
	i2c_error_t error;

	if((error = i2c_send1_receive(entry->address, tmd2771_reg_id, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	if((i2c_buffer[0] != tmd2771_id_tmd27711) && (i2c_buffer[0] != tmd2771_id_tmd27713))
		return(i2c_error_address_nak);

	data->high_sensitivity = config_flags_match(flag_tmd_high_sens);

	if((error = i2c_send2(entry->address, tmd2771_reg_enable, tmd2771_enable_poff)) != i2c_error_ok)
		return(error);

	if((error = i2c_send2(entry->address, tmd2771_reg_atime,
			(data->high_sensitivity ? tmd2771_atime_696 : tmd2771_atime_174))) != i2c_error_ok)
		return(error);

	if((error = i2c_send2(entry->address, tmd2771_reg_wtime, tmd2771_wtime_200)) != i2c_error_ok)
		return(error);

	if((error = i2c_send2(entry->address, tmd2771_reg_config, tmd2771_config_none)) != i2c_error_ok)
		return(error);

	if((error = i2c_send2(entry->address, tmd2771_reg_control, tmd2771_ctrl_pdrive_100 | tmd2771_ctrl_pdiode_pca |
			(data->high_sensitivity ? tmd2771_ctrl_again_16 : tmd2771_ctrl_again_1))) != i2c_error_ok)
		return(error);

	if((error = i2c_send2(entry->address, tmd2771_reg_enable,
			tmd2771_enable_wen | tmd2771_enable_aen | tmd2771_enable_pon)) != i2c_error_ok)
		return(error);

	sensor_register(bus, entry->id);

	return(i2c_error_ok);
}

static i2c_error_t sensor_tmd2771_read(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_value_t *value, i2c_sensor_device_data_t *data)
{
	uint8_t i2c_buffer[2];
	i2c_error_t error;
	unsigned int ch0, ch1;
	double cpl, lux1, lux2, lux;

	value->raw = value->cooked = -1;

	if((error = i2c_send1_receive(entry->address, tmd2771_reg_status, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	if(!(i2c_buffer[0] & tmd2771_status_avalid))
		return(i2c_error_device_error_1);

	if((error = i2c_send1_receive(entry->address, tmd2771_autoincr | tmd2771_reg_c0data, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	ch0 = (i2c_buffer[1] << 8) | (i2c_buffer[0]);

	if((error = i2c_send1_receive(entry->address, tmd2771_autoincr | tmd2771_reg_c1data, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	ch1 = (i2c_buffer[1] << 8) | (i2c_buffer[0]);

	if((ch0 == 0xffff) || (ch1 == 0xffff))
		return(i2c_error_overflow);

	cpl = ((data->high_sensitivity ? 696 : 174) * (data->high_sensitivity ? 16 : 1)) / 84;
	lux1 = (ch0 - 2 * ch1) / cpl;
	lux2 = ((0.6 * ch0) - ch1) / cpl;
	lux = fmax(fmax(lux1, lux2), 0);

	value->raw = ch0;
	value->cooked = lux;

	return(i2c_error_ok);
}

enum
{
	apds9930_reg_enable =		0x80,
	apds9930_reg_atime =		0x81,
	apds9930_reg_wtime =		0x83,
	apds9930_reg_config =		0x8d,
	apds9930_reg_control =		0x8f,
	apds9930_reg_id =			0x92,
	apds9930_reg_status =		0x93,
	apds9930_reg_c0data =		0x94,
	apds9930_reg_c0datah =		0x95,
	apds9930_reg_c1data =		0x96,
	apds9930_reg_c1datah =		0x97,

	apds9930_autoincr =			0b00100000,

	apds9930_enable_wen =		0b00001000,
	apds9930_enable_aen =		0b00000010,
	apds9930_enable_pon =		0b00000001,
	apds9930_enable_poff =		0b00000000,

	apds9930_atime_175 =		0xc0,
	apds9930_atime_699 =		0x00,

	apds9930_wtime_200 =		0xb6,

	apds9930_config_none =		0b00000000,

	apds9930_ctrl_pdrive_100 =	0b00000000,
	apds9930_ctrl_pdiode_ch1 =	0b00100000,
	apds9930_ctrl_again_1 =		0b00000000,
	apds9930_ctrl_again_16 =	0b00000010,

	apds9930_id_apds9930 =		0x30,

	apds9930_status_avalid =	0b00000001,
};

static i2c_error_t sensor_apds9930_init(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_device_data_t *data)
{
	uint8_t i2c_buffer[1];
	i2c_error_t error;

	if((error = i2c_send1_receive(entry->address, apds9930_reg_id, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	if(i2c_buffer[0] != apds9930_id_apds9930)
		return(i2c_error_address_nak);

	data->high_sensitivity = config_flags_match(flag_apds3_high_sens);

	if((error = i2c_send2(entry->address, apds9930_reg_enable, apds9930_enable_poff)) != i2c_error_ok)
		return(error);

	if((error = i2c_send2(entry->address, apds9930_reg_atime,
			(data->high_sensitivity ? apds9930_atime_699 : apds9930_atime_175))) != i2c_error_ok)
		return(error);

	if((error = i2c_send2(entry->address, apds9930_reg_wtime, apds9930_wtime_200)) != i2c_error_ok)
		return(error);

	if((error = i2c_send2(entry->address, apds9930_reg_config, apds9930_config_none)) != i2c_error_ok)
		return(error);

	if((error = i2c_send2(entry->address, apds9930_reg_control, apds9930_ctrl_pdrive_100 | apds9930_ctrl_pdiode_ch1 |
			(data->high_sensitivity ? apds9930_ctrl_again_16 : apds9930_ctrl_again_1))) != i2c_error_ok)
		return(error);

	if((error = i2c_send2(entry->address, apds9930_reg_enable,
			apds9930_enable_wen | apds9930_enable_aen | apds9930_enable_pon)) != i2c_error_ok)
		return(error);

	sensor_register(bus, entry->id);

	return(i2c_error_ok);
}

static i2c_error_t sensor_apds9930_read(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_value_t *value, i2c_sensor_device_data_t *data)
{
	uint8_t i2c_buffer[2];
	i2c_error_t error;
	unsigned int ch0, ch1;
	double lux, iac1, iac2, iac, lpc;

	value->raw = value->cooked = -1;

	if((error = i2c_send1_receive(entry->address, apds9930_reg_status, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	if(!(i2c_buffer[0] & apds9930_status_avalid))
		return(i2c_error_device_error_1);

	if((error = i2c_send1_receive(entry->address, apds9930_autoincr | apds9930_reg_c0data, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	ch0 = (i2c_buffer[1] << 8) | (i2c_buffer[0]);

	if((error = i2c_send1_receive(entry->address, apds9930_autoincr | apds9930_reg_c1data, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	ch1 = (i2c_buffer[1] << 8) | (i2c_buffer[0]);

	if((ch0 == 0xffff) || (ch1 == 0xffff))
		return(i2c_error_overflow);

	iac1 = ch0 - (1.862 * ch1);
	iac2 = (0.746 * ch0) - (1.291 * ch1);
	iac = fmax(fmax(iac1, iac2), 0);
	lpc = 25.48 / ((data->high_sensitivity ? 699 : 175) * (data->high_sensitivity ? 16 : 1));
	lux = iac * lpc;

	value->raw = ch0;
	value->cooked = lux;

	return(i2c_error_ok);
}

enum
{
	apds9960_reg_enable =		0x80,
	apds9960_reg_atime =		0x81,
	apds9960_reg_wtime =		0x83,
	apds9960_reg_config1 =		0x8d,
	apds9960_reg_control =		0x8f,
	apds9960_reg_id =			0x92,
	apds9960_reg_status =		0x93,
	apds9960_reg_cdata =		0x94,
	apds9960_reg_cdatah =		0x95,
	apds9960_clr_cpsat_status =	0xe6,

	apds9960_enable_wen =		0b00001000,
	apds9960_enable_aen =		0b00000010,
	apds9960_enable_pon =		0b00000001,
	apds9960_enable_poff =		0b00000000,

	apds9960_atime_200 =		0xb6,
	apds9960_atime_712 =		0x00,

	apds9960_wtime_236 =		0xab,

	apds9960_config1_none =		0b01100000,

	apds9960_ctrl_again_1 =		0b00000000,
	apds9960_ctrl_again_16 =	0b00000010,

	apds9960_id_apds9960 =		0xab,

	apds9960_status_cpsat =		0b10000000,
	apds9960_status_avalid =	0b00000001,
};

static i2c_error_t sensor_apds9960_init(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_device_data_t *data)
{
	uint8_t i2c_buffer[1];
	i2c_error_t error;

	if((error = i2c_send1_receive(entry->address, apds9960_reg_id, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	if(i2c_buffer[0] != apds9960_id_apds9960)
		return(i2c_error_address_nak);

	data->high_sensitivity = config_flags_match(flag_apds6_high_sens);

	if((error = i2c_send2(entry->address, apds9960_reg_enable, apds9960_enable_poff)) != i2c_error_ok)
		return(error);

	if((error = i2c_send2(entry->address, apds9960_reg_atime,
			(data->high_sensitivity ? apds9960_atime_712 : apds9960_atime_200))) != i2c_error_ok)
		return(error);

	if((error = i2c_send2(entry->address, apds9960_reg_wtime, apds9960_wtime_236)) != i2c_error_ok)
		return(error);

	if((error = i2c_send2(entry->address, apds9960_reg_config1, apds9960_config1_none)) != i2c_error_ok)
		return(error);

	if((error = i2c_send2(entry->address, apds9960_reg_control,
			(data->high_sensitivity ? apds9960_ctrl_again_16 : apds9960_ctrl_again_1))) != i2c_error_ok)
		return(error);

	if((error = i2c_send2(entry->address, apds9960_reg_enable,
			apds9960_enable_wen | apds9960_enable_aen | apds9960_enable_pon)) != i2c_error_ok)
		return(error);

	sensor_register(bus, entry->id);

	return(i2c_error_ok);
}

static i2c_error_t sensor_apds9960_read(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_value_t *value, i2c_sensor_device_data_t *data)
{
	uint8_t i2c_buffer[2];
	i2c_error_t error;
	unsigned int ch0;

	value->raw = value->cooked = -1;

	if((error = i2c_send1_receive(entry->address, apds9960_reg_status, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	if(i2c_buffer[0] & apds9960_status_cpsat)
	{
		i2c_send1(entry->address, apds9960_clr_cpsat_status);
		return(i2c_error_overflow);
	}

	if(!(i2c_buffer[0] & apds9960_status_avalid))
		return(i2c_error_device_error_1);

	if((error = i2c_send1_receive(entry->address, apds9960_reg_cdata, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	ch0 = (i2c_buffer[1] << 8) | (i2c_buffer[0]);

	if(ch0 == 0xffff)
	{
		i2c_send1(entry->address, apds9960_clr_cpsat_status);
		return(i2c_error_overflow);
	}

	value->raw = ch0;
	value->cooked = ch0;

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
	si114x_reg_meas_rate =		0x08,
	si114x_reg_reserved_0a =	0x0a,
	si114x_reg_reserved_0b =	0x0b,
	si114x_reg_reserved_0c =	0x0c,
	si114x_reg_reserved_0d =	0x0d,
	si114x_reg_reserved_0e =	0x0e,
	si114x_reg_ps_led21 =		0x0f,
	si114x_reg_ps_led3 =		0x10,
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
	si114x_param_i2c_addr =				0x00,
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
	si114x_cmd_alsforce =		0b00000110,	//	06
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
	si114x_cmd_param_set =		0b10100000,	//	a0
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
	si114x_chlist_bit_ps2 =		0b00000010,
	si114x_chlist_bit_ps1 =		0b00000001,
} si114x_chlist_bit_t;

typedef enum
{
	si114x_mux_ir_small =	0x00,
	si114x_mux_vis =		0x02,
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

typedef struct
{
	si114x_chlist_bit_t	chlist;
	si114x_register_t	reg;
	double				offset;
	double				factor;
} si114x_sensor_entry_t;

enum
{
	si114x_attempt_count = 16,
};

static i2c_error_t si114x_read_register(si114x_register_t reg, unsigned int length, uint8_t *bytes)
{
	i2c_error_t error;

	if((error = i2c_send1_receive(0x60, reg, length, bytes)) != i2c_error_ok)
		return(error);

	return(i2c_error_ok);
}

static i2c_error_t si114x_read_register_1(si114x_register_t reg, unsigned int *value)
{
	i2c_error_t error;
	uint8_t i2c_buffer[1];

	if((error = si114x_read_register(reg, 1, i2c_buffer)) != i2c_error_ok)
		return(error);

	*value = i2c_buffer[0];

	return(i2c_error_ok);
}

static i2c_error_t si114x_read_register_2(si114x_register_t reg, unsigned int *value)
{
	i2c_error_t error;
	uint8_t i2c_buffer[2];

	if((error = si114x_read_register(reg, 2, i2c_buffer)) != i2c_error_ok)
		return(error);

	*value = (i2c_buffer[1] << 8) | (i2c_buffer[0]);

	return(i2c_error_ok);
}

static i2c_error_t si114x_write_register_1(si114x_register_t reg, unsigned int value)
{
	return(i2c_send2(0x60, (uint8_t)reg, (uint8_t)value));
}

static i2c_error_t si114x_write_register_2(si114x_register_t reg, unsigned int value)
{
	uint8_t i2c_buffer[3];

	i2c_buffer[0] = (uint8_t)reg;
	i2c_buffer[1] = (value & 0x00ff) >> 0;
	i2c_buffer[2] = (value & 0xff00) >> 8;

	return(i2c_send(0x60, 3, i2c_buffer));
}

static i2c_error_t si114x_write_register_4(si114x_register_t reg, uint8_t val0, uint8_t val1, uint8_t val2, uint8_t val3)
{
	uint8_t i2c_buffer[5];

	i2c_buffer[0] = (uint8_t)reg;
	i2c_buffer[1] = val0;
	i2c_buffer[2] = val1;
	i2c_buffer[3] = val2;
	i2c_buffer[4] = val3;

	return(i2c_send(0x60, 5, i2c_buffer));
}

static i2c_error_t si114x_sendcmd(si114x_command_t command, unsigned int *response)
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
			log("si114x: response not 0: %u\n", local_response);
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
				log("si114x: response overflow: %x\n", local_response);
				break;
			}

			log("si114x: response invalid command / timeout: %x\n", local_response);
			log("si114x: attempt2: %u\n", attempt2);

			msleep(1);
		}

		if(attempt2 > 0)
			break;

failed:
		log("si114x: attempt1: %u\n", attempt1);
		msleep(1);
	}

	if(attempt1 == 0)
		return(i2c_error_device_error_1);

	return(i2c_error_ok);
}

static i2c_error_t si114x_get_param(si114x_parameter_t param, unsigned int *value)
{
	i2c_error_t error;

	if((error = si114x_sendcmd(si114x_cmd_param_query | ((uint8_t)param & 0x1f), (unsigned int *)0)) != i2c_error_ok)
		return(error);

	if((error = si114x_read_register_1(si114x_reg_param_rd, value)) != i2c_error_ok)
		return(error);

	return(i2c_error_ok);
}

static i2c_error_t si114x_set_param(si114x_parameter_t param, unsigned int value)
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

static i2c_error_t sensor_si114x_ultraviolet_init(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_device_data_t *data)
{
	i2c_error_t error;
	unsigned int value;
	uint8_t i2c_buffer[1];

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

	sensor_register(bus, entry->id);
	sensor_register(bus, i2c_sensor_si114x_visible_light);
	sensor_register(bus, i2c_sensor_si114x_infrared);
	sensor_register(bus, i2c_sensor_si114x_temperature);

	return(i2c_error_ok);
}

static i2c_error_t sensor_si114x_ultraviolet_read(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_value_t *value, i2c_sensor_device_data_t *data)
{
	i2c_error_t error;
	unsigned int regval;

	if((error = si114x_write_register_2(si114x_reg_aux_data, 0)) != i2c_error_ok)
		return(error);

	if((error = si114x_set_param(si114x_param_chlist, si114x_chlist_bit_uv)) != i2c_error_ok)
		return(error);

	if((error = si114x_sendcmd(si114x_cmd_alsforce, (unsigned int *)0)) != i2c_error_ok)
		return(error);

	if((error = si114x_read_register_2(si114x_reg_aux_data, &regval)) != i2c_error_ok)
		return(error);

	value->raw = regval;
	value->cooked = value->raw / (100 / 2 / 0.9);

	return(i2c_error_ok);
}

static i2c_error_t sensor_si114x_visible_light_read(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_value_t *value, i2c_sensor_device_data_t *data)
{
	i2c_error_t error;
	int visible;

	value->cooked = -1;

	if((error = si114x_write_register_2(si114x_reg_als_vis_data, 0)) != i2c_error_ok)
		return(error);

	if((error = si114x_set_param(si114x_param_chlist, si114x_chlist_bit_vis | si114x_chlist_bit_ir)) != i2c_error_ok)
		return(error);

	if((error = si114x_sendcmd(si114x_cmd_alsforce, (unsigned int *)0)) != i2c_error_ok)
		return(error);

	if((error = si114x_read_register_2(si114x_reg_als_vis_data, (unsigned int *)&visible)) != i2c_error_ok)
		return(error);

	visible -= 256;

	if(visible < 0)
		visible = 0;

	value->raw = visible;

	if(value->raw < 20)
		return(i2c_error_overflow);

	value->cooked = visible * 12;

	return(i2c_error_ok);
}

static i2c_error_t sensor_si114x_infrared_read(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_value_t *value, i2c_sensor_device_data_t *data)
{
	i2c_error_t error;
	int visible, ir;

	if((error = si114x_write_register_2(si114x_reg_als_ir_data, 0)) != i2c_error_ok)
		return(error);

	if((error = si114x_write_register_2(si114x_reg_als_vis_data, 0)) != i2c_error_ok)
		return(error);

	if((error = si114x_set_param(si114x_param_chlist, si114x_chlist_bit_vis | si114x_chlist_bit_ir)) != i2c_error_ok)
		return(error);

	if((error = si114x_sendcmd(si114x_cmd_alsforce, (unsigned int *)0)) != i2c_error_ok)
		return(error);

	if((error = si114x_read_register_2(si114x_reg_als_ir_data, (unsigned int *)&ir)) != i2c_error_ok)
		return(error);

	if((error = si114x_read_register_2(si114x_reg_als_vis_data, (unsigned int *)&visible)) != i2c_error_ok)
		return(error);

	ir -= 256;
	visible -= 256;

	if(ir < 0)
		ir = 0;

	if(visible < 1)
		visible = 1;

	value->raw = ir * 10000 + visible;
	value->cooked = (ir * 8.3) / visible;

	return(i2c_error_ok);
}

static i2c_error_t sensor_si114x_temperature_read(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_value_t *value, i2c_sensor_device_data_t *data)
{
	i2c_error_t error;
	unsigned int regval;

	if((error = si114x_write_register_2(si114x_reg_aux_data, 0)) != i2c_error_ok)
		return(error);

	if((error = si114x_set_param(si114x_param_chlist, si114x_chlist_bit_aux)) != i2c_error_ok)
		return(error);

	if((error = si114x_sendcmd(si114x_cmd_alsforce, (unsigned int *)0)) != i2c_error_ok)
		return(error);

	if((error = si114x_read_register_2(si114x_reg_aux_data, &regval)) != i2c_error_ok)
		return(error);

	value->raw = regval;
	value->cooked = (value->raw / 35) - 299;

	return(i2c_error_ok);
}

enum
{
	max44009_reg_ints =			0x00,
	max44009_reg_inte =			0x01,
	max44009_reg_conf =			0x02,
	max44009_reg_data_msb =		0x03,
	max44009_reg_data_lsb =		0x04,
	max44009_reg_thresh_msb =	0x05,
	max44009_reg_thresh_lsb =	0x06,
	max44009_reg_thresh_timer =	0x07,

	max44009_conf_tim_800 =		(0 << 2) | (0 << 1) | (0 << 0),
	max44009_conf_tim_400 =		(0 << 2) | (0 << 1) | (1 << 0),
	max44009_conf_tim_200 =		(0 << 2) | (1 << 1) | (0 << 0),
	max44009_conf_tim_100 =		(0 << 2) | (1 << 1) | (1 << 0),
	max44009_conf_tim_50 =		(1 << 2) | (0 << 1) | (0 << 0),
	max44009_conf_tim_25 =		(1 << 2) | (0 << 1) | (1 << 0),
	max44009_conf_tim_12 =		(1 << 2) | (1 << 1) | (0 << 0),
	max44009_conf_tim_6 =		(1 << 2) | (1 << 1) | (1 << 0),
	max44009_conf_cdr =			(1 << 3),
	max44009_conf_reserved4 =	(1 << 4),
	max44009_conf_reserved5 =	(1 << 5),
	max44009_conf_manual =		(1 << 6),
	max44009_conf_cont =		(1 << 7),

	max44009_probe_ints =			0x00,
	max44009_probe_inte =			0x00,
	max44009_probe_thresh_msb =		0xef,
	max44009_probe_thresh_lsb =		0x00,
	max44009_probe_thresh_timer =	0xff,
};

static i2c_error_t sensor_max44009_init(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_device_data_t *data)
{
	uint8_t i2c_buffer[2];
	i2c_error_t error = i2c_error_ok;

	if(((error = i2c_send1_receive(entry->address, max44009_reg_ints, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok) ||
			((i2c_buffer[0] != max44009_probe_ints) || (i2c_buffer[1] != max44009_probe_ints)) ||
			((error = i2c_send1_receive(entry->address, max44009_reg_inte, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok) ||
			((i2c_buffer[0] != max44009_probe_inte) || (i2c_buffer[1] != max44009_probe_inte)) ||
			((error = i2c_send1_receive(entry->address, max44009_reg_thresh_msb, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok) ||
			((i2c_buffer[0] != max44009_probe_thresh_msb) || (i2c_buffer[1] != max44009_probe_thresh_msb)) ||
			((error = i2c_send1_receive(entry->address, max44009_reg_thresh_lsb, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok) ||
			((i2c_buffer[0] != max44009_probe_thresh_lsb) || (i2c_buffer[1] != max44009_probe_thresh_lsb)) ||
			((error = i2c_send1_receive(entry->address, max44009_reg_thresh_timer, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok) ||
			((i2c_buffer[0] != max44009_probe_thresh_timer) || (i2c_buffer[1] != max44009_probe_thresh_timer)))
	{
		return((error == i2c_error_ok) ? i2c_error_address_nak : error);
	}

	if((error = i2c_send2(entry->address, max44009_reg_conf, max44009_conf_cont)) != i2c_error_ok)
		return(error);

	if((error = i2c_send1_receive(entry->address, max44009_reg_conf, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	if((i2c_buffer[0] & (max44009_conf_cont | max44009_conf_manual)) != max44009_conf_cont)
		return(i2c_error_device_error_1);

	sensor_register(bus, entry->id);

	return(i2c_error_ok);
}

static i2c_error_t sensor_max44009_read(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_value_t *value, i2c_sensor_device_data_t *data)
{
	i2c_error_t	error;
	uint8_t		i2c_buffer[2];
	int			exponent, mantissa;

	if((error = i2c_send1_receive(entry->address, max44009_reg_data_msb, 2, i2c_buffer)) != i2c_error_ok)
		return(error);

	exponent =	(i2c_buffer[0] & 0xf0) >> 4;
	mantissa =	(i2c_buffer[0] & 0x0f) << 4;
	mantissa |=	(i2c_buffer[1] & 0x0f) << 0;

	value->raw = (exponent * 10000) + mantissa;

	if(exponent == 0b1111)
		return(i2c_error_overflow);

	value->cooked = (1 << exponent) * mantissa * 0.045;

	return(i2c_error_ok);
}

enum
{
	veml6070_addr_cmd =			0x38,
	veml6070_addr_data_lsb =	0x38,
	veml6070_addr_data_msb =	0x39,

	veml6070_cmd_init =			0x06,
	veml6070_cmd_sd =			(1 << 0),
	veml6070_cmd_reserved0 =	(1 << 1),
	veml6070_cmd_it_0_5t =		(0 << 2) | (0 << 3),
	veml6070_cmd_it_1t =		(1 << 2) | (0 << 3),
	veml6070_cmd_it_2t =		(0 << 2) | (1 << 3),
	veml6070_cmd_it_4t =		(1 << 2) | (1 << 3),
	veml6070_cmd_thd =			(1 << 4),
	veml6070_cmd_ack =			(1 << 5),
	veml6070_cmd_reserved1 =	(1 << 6) | (1 << 7),

	veml6070_probe_data =		0xff,
};

static i2c_error_t sensor_veml6070_38_init(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_device_data_t *data)
{
	i2c_error_t error;
	uint8_t i2c_buffer[2];

	if((error = i2c_receive(veml6070_addr_data_msb, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	if(i2c_buffer[1] != veml6070_probe_data)
		return(i2c_error_address_nak);

	if((error = i2c_receive(veml6070_addr_data_lsb, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	if(i2c_buffer[1] != veml6070_probe_data)
		return(i2c_error_address_nak);

	if((error = i2c_send1(veml6070_addr_cmd, veml6070_cmd_init)) != i2c_error_ok)
		return(error);

	if((error = i2c_send1(veml6070_addr_cmd, veml6070_cmd_sd)) != i2c_error_ok)
		return(error);

	if((error = i2c_send1(veml6070_addr_cmd, veml6070_cmd_it_4t)) != i2c_error_ok)
		return(error);

	sensor_register(bus, entry->id);

	return(i2c_error_ok);
}

static i2c_error_t sensor_veml6070_38_read(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_value_t *value, i2c_sensor_device_data_t *data)
{
	i2c_error_t error;
	uint8_t i2c_buffer[1];
	unsigned int rv;

	if((error = i2c_receive(veml6070_addr_data_msb, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	rv = i2c_buffer[0];

	if((error = i2c_receive(veml6070_addr_data_lsb, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	rv = (rv << 8) | i2c_buffer[0];

	value->raw = rv;
	value->cooked = (((double)rv - 700) / 750) * 1.6; // magic values determined from it = 4 and example relations from Vishay

	if(value->cooked < 0)
		value->cooked = 0;

	return(i2c_error_ok);
}

static i2c_error_t sensor_veml6070_39_init(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_device_data_t *data)
{
	if(!i2c_sensor_registered(bus, i2c_sensor_veml6070_38))
		return(i2c_error_address_nak);

	sensor_register(bus, entry->id);

	return(i2c_error_ok);
}

static i2c_error_t sensor_veml6070_39_read(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_value_t *value, i2c_sensor_device_data_t *data)
{
	return(sensor_veml6070_38_read(bus, entry, value, data));
}

typedef enum
{
	tsl2561_reg_control =		0x00,
	tsl2561_reg_timeint =		0x01,
	tsl2561_reg_threshlow =		0x02,
	tsl2561_reg_threshhigh =	0x04,
	tsl2561_reg_interrupt =		0x06,
	tsl2561_reg_crc =			0x08,
	tsl2561_reg_id =			0x0a,
	tsl2561_reg_data0 =			0x0c,
	tsl2561_reg_data1 =			0x0e,
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
	tsl2561_tim_integ_13ms	=	(0 << 1) | (0 << 0),
	tsl2561_tim_integ_101ms	=	(0 << 1) | (1 << 0),
	tsl2561_tim_integ_402ms	=	(1 << 1) | (0 << 0),
	tsl2561_tim_manual		=	1 << 3,
	tsl2561_tim_high_gain	=	1 << 4,
} tsl2561_timeint_t;

enum
{
	tsl2561_ctrl_power_off =	0x00,
	tsl2561_ctrl_power_on =		0x03,

	tsl2561_id_tsl2561 =		0x50,
	tsl2561_probe_threshold =	0x00,
};

static i2c_error_t tsl2561_write(int address, tsl2561_reg_t reg, unsigned int value)
{
	i2c_error_t error;

	if((error = i2c_send2(address, tsl2561_cmd_cmd | tsl2561_cmd_clear | (reg & tsl2561_cmd_address), value)) != i2c_error_ok)
		return(error);

	return(i2c_error_ok);
}

static i2c_error_t tsl2561_read_byte(int address, tsl2561_reg_t reg, uint8_t *byte)
{
	i2c_error_t error;

	if((error = i2c_send1_receive(address, tsl2561_cmd_cmd | (reg & tsl2561_cmd_address), sizeof(*byte), byte)) != i2c_error_ok)
		return(error);

	return(i2c_error_ok);
}

static i2c_error_t tsl2561_read_word(int address, tsl2561_reg_t reg, uint16_t *word)
{
	uint8_t i2c_buffer[2];
	i2c_error_t error;

	if((error = i2c_send1_receive(address, tsl2561_cmd_cmd | (reg & tsl2561_cmd_address), sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	*word = (i2c_buffer[0] << 8) | i2c_buffer[1];

	return(i2c_error_ok);
}

static i2c_error_t tsl2561_write_check(int address, tsl2561_reg_t reg, unsigned int value)
{
	i2c_error_t error;
	uint8_t rv;

	if((error = tsl2561_write(address, reg, value)) != i2c_error_ok)
		return(error);

	if((error = tsl2561_read_byte(address, reg, &rv)) != i2c_error_ok)
		return(error);

	if(value != rv)
		return(i2c_error_device_error_1);

	return(i2c_error_ok);
}

static i2c_error_t sensor_tsl2561_init(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_device_data_t *data)
{
	i2c_error_t error;
	uint8_t regval;
	uint16_t word;
	tsl2561_timeint_t timeint;

	if((error = tsl2561_read_byte(entry->address, tsl2561_reg_id, &regval)) != i2c_error_ok)
		return(error);

	if(regval != tsl2561_id_tsl2561)
		return(i2c_error_address_nak);

	if((error = tsl2561_read_word(entry->address, tsl2561_reg_threshlow, &word)) != i2c_error_ok)
		return(error);

	if(word != tsl2561_probe_threshold)
		return(i2c_error_address_nak);

	if((error = tsl2561_read_word(entry->address, tsl2561_reg_threshhigh, &word)) != i2c_error_ok)
		return(error);

	if(word != tsl2561_probe_threshold)
		return(i2c_error_address_nak);

	if((error = tsl2561_write_check(entry->address, tsl2561_reg_control, tsl2561_ctrl_power_off)) != i2c_error_ok)
		return(error);

	if(tsl2561_write_check(entry->address, tsl2561_reg_id, 0x00) == i2c_error_ok) // id register should not be writable
		return(i2c_error_address_nak);

	if((error = tsl2561_write_check(entry->address, tsl2561_reg_interrupt, 0x00)) != i2c_error_ok)	// disable interrupts
		return(error);

	data->high_sensitivity = config_flags_match(flag_tsl_high_sens);

	if(data->high_sensitivity)
		timeint = tsl2561_tim_integ_402ms | tsl2561_tim_high_gain;
	else
		timeint = tsl2561_tim_integ_101ms;

	if((error = tsl2561_write_check(entry->address, tsl2561_reg_timeint, timeint)) != i2c_error_ok)	// start continuous sampling
		return(error);

	if((error = tsl2561_write(entry->address, tsl2561_reg_control, tsl2561_ctrl_power_on)) != i2c_error_ok)	// power up
		return(error);

	if((error = tsl2561_read_byte(entry->address, tsl2561_reg_control, &regval)) != i2c_error_ok)
		return(error);

	if((regval & 0x0f) != tsl2561_ctrl_power_on)
		return(i2c_error_device_error_1);

	sensor_register(bus, entry->id);

	return(i2c_error_ok);
}

static i2c_error_t sensor_tsl2561_read(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_value_t *value, i2c_sensor_device_data_t *data)
{
	i2c_error_t	error;
	uint8_t i2c_buffer[2];
	unsigned int ch0r, ch1r;
	double ratio, ch0, ch1;

	value->raw = value->cooked = -1;

	if((error = i2c_send1_receive(entry->address, tsl2561_cmd_cmd | tsl2561_reg_data0, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	ch0r = (i2c_buffer[1] << 8) | i2c_buffer[0];

	if((error = i2c_send1_receive(entry->address, tsl2561_cmd_cmd | tsl2561_reg_data1, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	ch1r = (i2c_buffer[1] << 8) | i2c_buffer[0];

	if(ch0r == 0)
	{
		value->raw = value->cooked = 0;
		return(i2c_error_device_error_1);
	}

	if(data->high_sensitivity)
	{
		// high sensitivity = 402 ms integration time, scaling factor = 1
		// analogue amplification = 16x, scaling factor = 1

		if((ch0r == 65535) || (ch1r == 65535))
			return(i2c_error_overflow);

		ch0 = (double)ch0r * 1.0 * 1.0;
		ch1 = (double)ch1r * 1.0 * 1.0;
	}
	else
	{
		// low  sensitivity = 101 ms integration time, scaling factor = 3.98 (402 / 101)
		// analogue amplification = 1x, scaling factor = 16

		if((ch0r >= 37177) || (ch1r >= 37177))
			return(i2c_error_overflow);

		ch0 = (double)ch0r * 3.98 * 16.0;
		ch1 = (double)ch1r * 3.98 * 16.0;
	}

	ratio = ch1 / ch0;

	value->raw = ch0;

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
					value->cooked = (0.0304 * ch0) - (0.062 * ch0 * pow(ratio, 1.4));

	return(i2c_error_ok);
}

roflash static const uint32_t tsl2550_count[128] =
{
	0,		1,		2,		3,		4,		5,		6,		7,		8,		9,		10,		11,		12,		13,		14,		15,		16,
	18,		20,		22,		24,		26,		28,		30,		32,		34,		36,		38,		40,		42,		44,		46,
	49,		53,		57,		61,		65,		69,		73,		77,		81,		85,		89,		93,		97,		101,	105,	109,
	115,	123,	131,	139,	147,	155,	163,	171,	179,	187,	195,	203,	211,	219,	227,	235,
	247,	263,	279,	295,	311,	327,	343,	359,	375,	391,	407,	423,	439,	455,	471,	487,	511,
	543,	575,	607,	639,	671,	703,	735,	767,	799,	831,	863,	895,	927,	959,	991,
	1039,	1103,	1167,	1231,	1295,	1359,	1423,	1487,	1551,	1615,	1679,	1743,	1807,	1871,	1935,	1999,
	2095,
	2223,	2351,	2479,	2607,	2735,	2863,	2991,	3119,	3247,	3375,	3503,	3631,	3759,	3887,	4015
};

roflash static const uint32_t tsl2550_ratio[129] =
{
	100,	100,	100,	100,	100,	100,	100,	100,
	100,	100,	100,	100,	100,	100,	99,		99,
	99,		99,		99,		99,		99,		99,		99,		99,
	99,		99,		99,		98,		98,		98,		98,		98,
	98,		98,		97,		97,		97,		97,		97,		96,
	96,		96,		96,		95,		95,		95,		94,		94,
	93,		93,		93,		92,		92,		91,		91,		90,
	89,		89,		88,		87,		87,		86,		85,		84,
	83,		82,		81,		80,		79,		78,		77,		75,
	74,		73,		71,		69,		68,		66,		64,		62,
	60,		58,		56,		54,		52,		49,		47,		44,
	42,		41,		40,		40,		39,		39,		38,		38,
	37,		37,		37,		36,		36,		36,		35,		35,
	35,		35,		34,		34,		34,		34,		33,		33,
	33,		33,		32,		32,		32,		32,		32,		31,
	31,		31,		31,		31,		30,		30,		30,		30,
	30
};

static i2c_error_t sensor_tsl2550_rw(int address, int in, uint8_t *out)
{
	i2c_error_t error;

	if((error = i2c_send1(address, in)) != i2c_error_ok)
		return(error);

	if((error = i2c_receive(address, 1, out)) != i2c_error_ok)
		return(error);

	return(i2c_error_ok);
}

static i2c_error_t sensor_tsl2550_write_check(int address, int in, int compare)
{
	i2c_error_t error;
	uint8_t out;

	if((error = sensor_tsl2550_rw(address, in, &out)) != i2c_error_ok)
		return(error);

	if(out != compare)
		return(i2c_error_device_error_1);

	return(i2c_error_ok);
}

static i2c_error_t sensor_tsl2550_init(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_device_data_t *data)
{
	i2c_error_t error;

	// tsl2550 power up

	if((error = sensor_tsl2550_write_check(entry->address, 0x03, 0x03)) != i2c_error_ok)
		return(error);

	if((error = sensor_tsl2550_write_check(entry->address, data->high_sensitivity ? 0x18 : 0x1d, 0x1b)) != i2c_error_ok)
		return(error);

	data->high_sensitivity = config_flags_match(flag_tsl_high_sens);

	sensor_register(bus, entry->id);

	return(i2c_error_ok);
}

static i2c_error_t sensor_tsl2550_read(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_value_t *value, i2c_sensor_device_data_t *data)
{
	i2c_error_t	error;
	uint8_t		ch0, ch1;
	int			attempt, ratio;

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

	if(data->high_sensitivity)
		value->cooked *= 5;

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

static i2c_error_t sensor_bh1750_init(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_device_data_t *data)
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

	data->high_sensitivity = config_flags_match(flag_bh_high_sens);

	if(data->high_sensitivity)
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

	sensor_register(bus, entry->id);

	return(i2c_error_ok);
}

static i2c_error_t sensor_bh1750_read(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_value_t *value, i2c_sensor_device_data_t *data)
{
	i2c_error_t error;
	uint8_t	i2cbuffer[2];
	double luxpercount;

	if((error = i2c_receive(entry->address, 2, i2cbuffer)) != i2c_error_ok)
		return(error);

	if(data->high_sensitivity)
		// mode = hmode2, timing = 254
		// hmode2 = 1/2 lx / count
		// timing = 1 / 254 / 69 = 0.27
		luxpercount = 1.2 * 0.5 * 0.27;
	else
		// mode = hmode, timing = default = 69
		// hmode =  1 lx / count
		luxpercount = 1.2;

	value->raw = (i2cbuffer[0] << 8) | i2cbuffer[1];

	if(value->raw >= 0xffff)
		value->cooked = -1;
	else
		value->cooked = value->raw * luxpercount * 0.6;

	return(i2c_error_ok);
}

enum
{
	tmp75_reg_temp =	0x00,
	tmp75_reg_conf =	0x01,
	tmp75_reg_tlow =	0x02,
	tmp75_reg_thigh =	0x03,

	tmp75_reg_conf_os =			0b10000000,
	tmp75_reg_conf_res_9 =		0b00000000,
	tmp75_reg_conf_res_10 =		0b00100000,
	tmp75_reg_conf_res_11 =		0b01000000,
	tmp75_reg_conf_res_12 =		0b01100000,
	tmp75_reg_conf_f_queue =	0b00011000,
	tmp75_reg_conf_pol =		0b00000100,
	tmp75_reg_conf_tm =			0b00000010,
	tmp75_reg_conf_shutdown =	0b00000001,
	tmp75_reg_conf_no_shut =	0b00000000,

	tmp75_probe_04 =		0x04,
	tmp75_probe_a1 =		0xa1,
	tmp75_probe_a2 =		0xa2,
	tmp75_probe_aa =		0xaa,
	tmp75_probe_ac =		0xac,

	tmp75_probe_tl_h =		0x4b,
	tmp75_probe_tl_l =		0x00,
	tmp75_probe_th_h =		0x50,
	tmp75_probe_th_l =		0x00,
	tmp75_probe_conf =		0b00000000,
	tmp75_probe_conf_mask =	0b10000000,
};

static i2c_error_t sensor_tmp75_init(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_device_data_t *data)
{
	uint8_t i2c_buffer[2];
	i2c_error_t error = i2c_error_ok;

	if(((error = i2c_send1_receive(entry->address, tmp75_reg_conf, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok) ||
			((i2c_buffer[0] & tmp75_probe_conf_mask) != tmp75_probe_conf) ||
			((error = i2c_send1_receive(entry->address, tmp75_reg_tlow, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok) ||
			((i2c_buffer[0] != tmp75_probe_tl_h) || (i2c_buffer[1] != tmp75_probe_tl_l)) ||
			((error = i2c_send1_receive(entry->address, tmp75_reg_thigh, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok) ||
			((i2c_buffer[0] != tmp75_probe_th_h) || (i2c_buffer[1] != tmp75_probe_th_l)) ||
			(i2c_send1(entry->address, tmp75_probe_04) == i2c_error_ok) ||
			(i2c_send1(entry->address, tmp75_probe_a1) == i2c_error_ok) ||
			(i2c_send1(entry->address, tmp75_probe_a2) == i2c_error_ok) ||
			(i2c_send1(entry->address, tmp75_probe_aa) == i2c_error_ok) ||
			(i2c_send1(entry->address, tmp75_probe_ac) == i2c_error_ok))
	{
		return((error == i2c_error_ok) ? i2c_error_address_nak : error);
	}

	if((error = i2c_send2(entry->address, tmp75_reg_conf, tmp75_reg_conf_res_12 | tmp75_reg_conf_no_shut)) != i2c_error_ok)
		return(error);

	if((error = i2c_send1_receive(entry->address, tmp75_reg_conf, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	if(i2c_buffer[0] != (tmp75_reg_conf_res_12 | tmp75_reg_conf_no_shut))
		return(i2c_error_device_error_1);

	sensor_register(bus, entry->id);

	return(i2c_error_ok);
}

static i2c_error_t sensor_tmp75_read(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_value_t *value, i2c_sensor_device_data_t *data)
{
	uint8_t i2c_buffer[2];
	i2c_error_t error;
	int16_t raw_temperature;

	if((error = i2c_send1_receive(entry->address, tmp75_reg_temp, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	value->raw = raw_temperature = (i2c_buffer[0] << 8) | i2c_buffer[1];
	value->cooked = raw_temperature / 256.0;

	return(i2c_error_ok);
}

enum
{
	ds7505_reg_temp =	0x00,
	ds7505_reg_conf =	0x01,
	ds7505_reg_thyst =	0x02,
	ds7505_reg_tos =	0x03,

	ds7505_reg_conf_nvb =		0b10000000,
	ds7505_reg_conf_res_9 =		0b00000000,
	ds7505_reg_conf_res_10 =	0b00100000,
	ds7505_reg_conf_res_11 =	0b01000000,
	ds7505_reg_conf_res_12 =	0b01100000,
	ds7505_reg_conf_f_queue =	0b00011000,
	ds7505_reg_conf_pol =		0b00000100,
	ds7505_reg_conf_tm =		0b00000010,
	ds7505_reg_conf_shutdown =	0b00000001,
	ds7505_reg_conf_no_shut =	0b00000000,

	ds7505_probe_04 =		0x04,
	ds7505_probe_05 =		0x05,
	ds7505_probe_06 =		0x06,
	ds7505_probe_07 =		0x07,
	ds7505_probe_10 =		0x10,
	ds7505_probe_a1 =		0xa1,
	ds7505_probe_a2 =		0xa2,
	ds7505_probe_aa =		0xaa,
	ds7505_probe_ac =		0xac,
	ds7505_probe_ee =		0xee,

	ds7505_probe_th_h =			0x4b,
	ds7505_probe_th_l =			0x00,
	ds7505_probe_to_h =			0x50,
	ds7505_probe_to_l =			0x00,
	ds7505_probe_conf =			0b00000000,
	ds7505_probe_conf_mask =	0b10011111,
};

static i2c_error_t sensor_ds7505_init(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_device_data_t *data)
{
	uint8_t i2c_buffer[2];
	i2c_error_t error = i2c_error_ok;

	if(((error = i2c_send1_receive(entry->address, ds7505_reg_conf, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok) ||
			((i2c_buffer[0] & ds7505_probe_conf_mask) != ds7505_probe_conf) ||
			((error = i2c_send1_receive(entry->address, ds7505_reg_thyst, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok) ||
			((i2c_buffer[0] != ds7505_probe_th_h) || (i2c_buffer[1] != ds7505_probe_th_l)) ||
			((error = i2c_send1_receive(entry->address, ds7505_reg_tos, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok) ||
			((i2c_buffer[0] != ds7505_probe_to_h) && (i2c_buffer[1] != ds7505_probe_to_l)) ||
			(i2c_send1(entry->address, ds7505_probe_04) != i2c_error_ok) ||
			(i2c_send1(entry->address, ds7505_probe_05) != i2c_error_ok) ||
			(i2c_send1(entry->address, ds7505_probe_06) != i2c_error_ok) ||
			(i2c_send1(entry->address, ds7505_probe_07) != i2c_error_ok) ||
			(i2c_send1(entry->address, ds7505_probe_10) == i2c_error_ok) ||
			(i2c_send1(entry->address, ds7505_probe_a1) == i2c_error_ok) ||
			(i2c_send1(entry->address, ds7505_probe_a2) == i2c_error_ok) ||
			(i2c_send1(entry->address, ds7505_probe_aa) == i2c_error_ok) ||
			(i2c_send1(entry->address, ds7505_probe_ac) == i2c_error_ok) ||
			(i2c_send1(entry->address, ds7505_probe_ee) != i2c_error_ok))
	{
		return((error == i2c_error_ok) ? i2c_error_address_nak : error);
	}

	if((error = i2c_send2(entry->address, ds7505_reg_conf, ds7505_reg_conf_res_12 | ds7505_reg_conf_no_shut)) != i2c_error_ok)
		return(error);

	if((error = i2c_send1_receive(entry->address, ds7505_reg_conf, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	if(i2c_buffer[0] != (ds7505_reg_conf_res_12 | ds7505_reg_conf_no_shut))
		return(i2c_error_device_error_1);

	sensor_register(bus, entry->id);

	return(i2c_error_ok);
}

static i2c_error_t sensor_ds7505_read(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_value_t *value, i2c_sensor_device_data_t *data)
{
	uint8_t i2c_buffer[2];
	i2c_error_t error;
	int16_t raw_temperature;

	if((error = i2c_send1_receive(entry->address, ds7505_reg_temp, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	value->raw = raw_temperature = (i2c_buffer[0] << 8) | i2c_buffer[1];
	value->cooked = raw_temperature / 256.0;

	return(i2c_error_ok);
}

enum
{
	ds1631_reg_temp =			0xaa,
	ds1631_reg_th =				0xa1,
	ds1631_reg_tl =				0xa2,
	ds1631_reg_conf =			0xac,
	ds1631_reg_start =			0x51,
	ds1631_reg_stop =			0x22,
	ds1631_reg_por =			0x54,

	ds1631_reg_conf_done =		0b10000000,
	ds1631_reg_conf_thf =		0b01000000,
	ds1631_reg_conf_tlf =		0b00100000,
	ds1631_reg_conf_nvb =		0b00010000,
	ds1631_reg_conf_r1 =		0b00001000,
	ds1631_reg_conf_r0 =		0b00000100,
	ds1631_reg_conf_pol =		0b00000010,
	ds1631_reg_conf_1shot =		0b00000001,
	ds1631_reg_conf_cont =		0b00000000,

	ds1631_probe_00 =			0x00,
	ds1631_probe_01 =			0x01,
	ds1631_probe_02 =			0x02,
	ds1631_probe_03 =			0x03,
	ds1631_probe_04 =			0x04,
	ds1631_probe_t_h =			0xc4,
	ds1631_probe_t_l =			0x00,
	ds1631_probe_th_h =			0x0f,
	ds1631_probe_th_l =			0x00,
	ds1631_probe_tl_h =			0x0a,
	ds1631_probe_tl_l =			0x00,
	ds1631_probe_conf =			0b10001100,
	ds1631_probe_conf_mask =	0b11111100,
};

static i2c_error_t sensor_ds1631_init(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_device_data_t *data)
{
	uint8_t i2c_buffer[2];
	i2c_error_t error = i2c_error_ok;

	i2c_send1(entry->address, ds1631_reg_por);

	if(((error = i2c_send1_receive(entry->address, ds1631_reg_conf, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok) ||
			((i2c_buffer[0] & ds1631_probe_conf_mask) != ds1631_probe_conf) ||
			((error = i2c_send1_receive(entry->address, ds1631_reg_temp, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok) ||
			(i2c_buffer[0] != ds1631_probe_t_h) ||
			(i2c_buffer[1] != ds1631_probe_t_l) ||
			((error = i2c_send1_receive(entry->address, ds1631_reg_th, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok) ||
			(i2c_buffer[0] != ds1631_probe_th_h) ||
			(i2c_buffer[1] != ds1631_probe_th_l) ||
			((error = i2c_send1_receive(entry->address, ds1631_reg_tl, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok) ||
			(i2c_buffer[0] != ds1631_probe_tl_h) ||
			(i2c_buffer[1] != ds1631_probe_tl_l) ||
			(i2c_send1_receive(entry->address, ds1631_probe_00, sizeof(i2c_buffer), i2c_buffer) == i2c_error_ok) ||
			(i2c_send1_receive(entry->address, ds1631_probe_01, sizeof(i2c_buffer), i2c_buffer) == i2c_error_ok) ||
			(i2c_send1_receive(entry->address, ds1631_probe_02, sizeof(i2c_buffer), i2c_buffer) == i2c_error_ok) ||
			(i2c_send1_receive(entry->address, ds1631_probe_03, sizeof(i2c_buffer), i2c_buffer) == i2c_error_ok) ||
			(i2c_send1_receive(entry->address, ds1631_probe_04, sizeof(i2c_buffer), i2c_buffer) == i2c_error_ok))
	{
		return((error == i2c_error_ok) ? i2c_error_address_nak : error);
	}

	if((error = i2c_send1(entry->address, ds1631_reg_stop)) != i2c_error_ok)
		return(error);

	if((error = i2c_send2(entry->address, ds1631_reg_conf, ds1631_reg_conf_r1 | ds1631_reg_conf_r0 | ds1631_reg_conf_cont)) != i2c_error_ok)
		return(error);

	if((error = i2c_send1_receive(entry->address, ds1631_reg_conf, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	if((i2c_buffer[0] & ~ds1631_reg_conf_nvb) != (ds1631_reg_conf_done | ds1631_reg_conf_r1 | ds1631_reg_conf_r0 | ds1631_reg_conf_cont))
		return(i2c_error_device_error_1);

	if((error = i2c_send1(entry->address, ds1631_reg_start) != i2c_error_ok))
		return(error);

	sensor_register(bus, entry->id);

	return(i2c_error_ok);
}

static i2c_error_t sensor_ds1631_read(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_value_t *value, i2c_sensor_device_data_t *data)
{
	uint8_t i2c_buffer[2];
	i2c_error_t error;
	unsigned int raw_temperature;

	if((error = i2c_send1_receive(entry->address, ds1631_reg_temp, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	value->raw = raw_temperature = (i2c_buffer[0] << 8) | i2c_buffer[1];
	value->cooked = (int16_t)raw_temperature / 256.0;

	return(i2c_error_ok);
}

enum
{
	ds1621_reg_temp =			0xaa,
	ds1621_reg_th =				0xa1,
	ds1621_reg_tl =				0xa2,
	ds1621_reg_conf =			0xac,
	ds1621_reg_start=			0xee,
	ds1621_reg_stop =			0x22,

	ds1621_reg_conf_done =		0b10000000,
	ds1621_reg_conf_thf =		0b01000000,
	ds1621_reg_conf_tlf =		0b00100000,
	ds1621_reg_conf_nvb =		0b00010000,
	ds1621_reg_conf_xx =		0b00001100,
	ds1621_reg_conf_pol =		0b00000010,
	ds1621_reg_conf_1shot =		0b00000001,
	ds1621_reg_conf_cont =		0b00000000,
	ds1621_reg_conf_volatile =	0b10011100,

	ds1621_probe_00	=			0x00,
	ds1621_probe_01	=			0x01,
	ds1621_probe_02	=			0x02,
	ds1621_probe_03 =			0x03,
	ds1621_probe_04 =			0x04,
	ds1621_probe_h =			0xff,
	ds1621_probe_l =			0x80,
	ds1621_probe_th_h =			0x50,
	ds1621_probe_th_l =			0x00,
	ds1621_probe_tl_h =			0xa0,
	ds1621_probe_tl_l =			0x00,
	ds1621_probe_conf =			0b00001000,
	ds1621_probe_conf_mask =	0b01111100,
};

static i2c_error_t sensor_ds1621_init(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_device_data_t *data)
{
	uint8_t i2c_buffer[2];
	i2c_error_t error = i2c_error_ok;

	if((i2c_send1_receive(entry->address, ds1621_probe_00, sizeof(i2c_buffer), i2c_buffer) != i2c_error_ok) ||
			((i2c_buffer[0] != ds1621_probe_h) || (i2c_buffer[1] != ds1621_probe_l)) ||
			(i2c_send1_receive(entry->address, ds1621_probe_01, sizeof(i2c_buffer), i2c_buffer) != i2c_error_ok) ||
			((i2c_buffer[0] != ds1621_probe_h) || (i2c_buffer[1] != ds1621_probe_l)) ||
			(i2c_send1_receive(entry->address, ds1621_probe_02, sizeof(i2c_buffer), i2c_buffer) != i2c_error_ok) ||
			((i2c_buffer[0] != ds1621_probe_h) || (i2c_buffer[1] != ds1621_probe_l)) ||
			(i2c_send1_receive(entry->address, ds1621_probe_03, sizeof(i2c_buffer), i2c_buffer) != i2c_error_ok) ||
			((i2c_buffer[0] != ds1621_probe_h) || (i2c_buffer[1] != ds1621_probe_l)) ||
			(i2c_send1_receive(entry->address, ds1621_probe_04, sizeof(i2c_buffer), i2c_buffer) != i2c_error_ok) ||
			((i2c_buffer[0] != ds1621_probe_h) || (i2c_buffer[1] != ds1621_probe_l)) ||
			(i2c_send1_receive(entry->address, ds1621_reg_th, sizeof(i2c_buffer), i2c_buffer) != i2c_error_ok) ||
			((i2c_buffer[0] != ds1621_probe_th_h) || (i2c_buffer[1] != ds1621_probe_th_l)) ||
			(i2c_send1_receive(entry->address, ds1621_reg_tl, sizeof(i2c_buffer), i2c_buffer) != i2c_error_ok) ||
			((i2c_buffer[0] != ds1621_probe_tl_h) || (i2c_buffer[1] != ds1621_probe_tl_l)) ||
			(i2c_send1_receive(entry->address, ds1621_reg_conf, sizeof(i2c_buffer), i2c_buffer) != i2c_error_ok) ||
			((i2c_buffer[0] & ds1621_probe_conf_mask) != ds1621_probe_conf))
	{
		return((error == i2c_error_ok) ? i2c_error_address_nak : error);
	}

	if((error = i2c_send2(entry->address, ds1621_reg_conf, ds1621_reg_conf_cont)) != i2c_error_ok)
		return(error);

	if((error = i2c_send1_receive(entry->address, ds1621_reg_conf, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	if((i2c_buffer[0] & ~ds1621_reg_conf_volatile) != ds1621_reg_conf_cont)
		return(i2c_error_device_error_1);

	if((error = i2c_send1(entry->address, ds1621_reg_start) != i2c_error_ok))
		return(error);

	sensor_register(bus, entry->id);

	return(i2c_error_ok);
}

static i2c_error_t sensor_ds1621_read(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_value_t *value, i2c_sensor_device_data_t *data)
{
	uint8_t i2c_buffer[2];
	i2c_error_t error;
	unsigned int raw_temperature;

	if((error = i2c_send1_receive(entry->address, ds1621_reg_temp, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	value->raw = raw_temperature = (i2c_buffer[0] << 8) | i2c_buffer[1];
	value->cooked = (int16_t)raw_temperature / 256.0;

	return(i2c_error_ok);
}

enum
{
	tmp102_reg_temp =			0x00,
	tmp102_reg_conf =			0x01,
	tmp102_reg_tlow =			0x02,
	tmp102_reg_thigh =			0x03,

	tmp102_reg_conf_os =		0b10000000,
	tmp102_reg_conf_res_9 =		0b00000000,
	tmp102_reg_conf_res_10 =	0b00100000,
	tmp102_reg_conf_res_11 =	0b01000000,
	tmp102_reg_conf_res_12 =	0b01100000,
	tmp102_reg_conf_f_queue =	0b00011000,
	tmp102_reg_conf_pol =		0b00000100,
	tmp102_reg_conf_tm =		0b00000010,
	tmp102_reg_conf_shutdown =	0b00000001,
	tmp102_reg_conf_no_shut =	0b00000000,

	tmp102_reg_conf2_cr1 =		0b10000000,
	tmp102_reg_conf2_cr0 =		0b01000000,
	tmp102_reg_conf2_al =		0b00100000,
	tmp102_reg_conf2_em =		0b00010000,

	tmp102_probe_04 =			0x04,
	tmp102_probe_06 =			0x06,
	tmp102_probe_07 =			0x07,
	tmp102_probe_a1 =			0xa1,
	tmp102_probe_a2 =			0xa2,
	tmp102_probe_aa =			0xaa,
	tmp102_probe_ac =			0xac,

	tmp102_probe_06_h =			0x00,
	tmp102_probe_06_l =			0x00,
	tmp102_probe_07_h =			0x00,
	tmp102_probe_07_l =			0x00,
	tmp102_probe_tl_h =			0x4b,
	tmp102_probe_tl_l =			0x00,
	tmp102_probe_th_h =			0x50,
	tmp102_probe_th_l =			0x00,

	tmp102_probe_conf_h =		0b00000000,
	tmp102_probe_conf_h_mask =	0b10011111,
	tmp102_probe_conf_l =		0b10100000,
	tmp102_probe_conf_l_mask =	0b11111111,
};

static i2c_error_t sensor_tmp102_init(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_device_data_t *data)
{
	uint8_t i2c_buffer[2];
	i2c_error_t error = i2c_error_ok;

	if(((error = i2c_send1_receive(entry->address, tmp102_reg_conf, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok) ||
			(((i2c_buffer[0] & tmp102_probe_conf_h_mask) != tmp102_probe_conf_h) || ((i2c_buffer[1] & tmp102_probe_conf_l_mask) != tmp102_probe_conf_l)) ||
			((error = i2c_send1_receive(entry->address, tmp102_reg_tlow, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok) ||
			((i2c_buffer[0] != tmp102_probe_tl_h) || (i2c_buffer[1] != tmp102_probe_tl_l)) ||
			((error = i2c_send1_receive(entry->address, tmp102_reg_thigh, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok) ||
			((i2c_buffer[0] != tmp102_probe_th_h) || (i2c_buffer[1] != tmp102_probe_th_l)) ||
			((error = i2c_send1_receive(entry->address, tmp102_probe_06, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok) ||
			((i2c_buffer[0] != tmp102_probe_06_h) || (i2c_buffer[1] != tmp102_probe_06_l)) ||
			((error = i2c_send1_receive(entry->address, tmp102_probe_07, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok) ||
			((i2c_buffer[0] != tmp102_probe_07_h) || (i2c_buffer[1] != tmp102_probe_07_l)) ||
			(i2c_send1(entry->address, tmp102_probe_04) != i2c_error_ok) ||
			(i2c_send1(entry->address, tmp102_probe_a1) != i2c_error_ok) ||
			(i2c_send1(entry->address, tmp102_probe_a2) != i2c_error_ok) ||
			(i2c_send1(entry->address, tmp102_probe_aa) != i2c_error_ok) ||
			(i2c_send1(entry->address, tmp102_probe_ac) != i2c_error_ok))
	{
		return((error == i2c_error_ok) ? i2c_error_address_nak : error);
	}

	if((error = i2c_send3(entry->address, tmp102_reg_conf, tmp102_reg_conf_res_12 | tmp102_reg_conf_no_shut, tmp102_reg_conf2_cr1 | tmp102_reg_conf2_al)) != i2c_error_ok)
		return(error);

	if((error = i2c_send1_receive(entry->address, tmp102_reg_conf, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	if((i2c_buffer[0] != (tmp102_reg_conf_res_12 | tmp102_reg_conf_no_shut)) || (i2c_buffer[1] != (tmp102_reg_conf2_cr1 | tmp102_reg_conf2_al)))
		return(i2c_error_device_error_1);

	sensor_register(bus, entry->id);

	return(i2c_error_ok);
}

static i2c_error_t sensor_tmp102_read(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_value_t *value, i2c_sensor_device_data_t *data)
{
	uint8_t i2c_buffer[2];
	i2c_error_t error;
	int16_t raw_temperature;

	if((error = i2c_send1_receive(entry->address, tmp102_reg_temp, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	value->raw = raw_temperature = (i2c_buffer[0] << 8) | i2c_buffer[1];
	value->cooked = raw_temperature / 256.0;

	return(i2c_error_ok);
}

enum
{
	lm75_reg_temp =				0x00,
	lm75_reg_conf =				0x01,
	lm75_reg_thyst =			0x02,
	lm75_reg_tos =				0x03,

	lm75_reg_conf_reserved =	0b11100000,
	lm75_reg_conf_f_queue =		0b00011000,
	lm75_reg_conf_pol =			0b00000100,
	lm75_reg_conf_comp_int =	0b00000010,
	lm75_reg_conf_shutdown =	0b00000001,
	lm75_reg_conf_no_shutdown =	0b00000000,

	lm75_probe_thyst_h =		0x4b,
	lm75_probe_thyst_l =		0x00,
	lm75_probe_tos_1_h =		0x50,
	lm75_probe_tos_1_l =		0x00,
	lm75_probe_tos_2_h =		0x00,
	lm75_probe_tos_2_l =		0x00,
	lm75_probe_conf =			0b00000000,
	lm75_probe_conf_mask =		0b10011111,
};

static i2c_error_t sensor_lm75_init(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_device_data_t *data)
{
	uint8_t i2c_buffer[2];
	i2c_error_t error = i2c_error_ok;

	if(((error = i2c_send1_receive(entry->address, lm75_reg_conf, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok) ||
			((i2c_buffer[0] & lm75_probe_conf_mask) != lm75_probe_conf) ||
			((error = i2c_send1_receive(entry->address, lm75_reg_thyst, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok) ||
			((i2c_buffer[0] != lm75_probe_thyst_h) || (i2c_buffer[1] != lm75_probe_thyst_l)) ||
			((error = i2c_send1_receive(entry->address, lm75_reg_tos, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok) ||
			(((i2c_buffer[0] != lm75_probe_tos_1_h) || (i2c_buffer[1] != lm75_probe_tos_1_l)) && ((i2c_buffer[0] != lm75_probe_tos_2_h) || (i2c_buffer[1] != lm75_probe_tos_2_l))))
	{
		return((error == i2c_error_ok) ? i2c_error_address_nak : error);
	}

	if((error = i2c_send2(entry->address, lm75_reg_conf, lm75_reg_conf_no_shutdown)) != i2c_error_ok)
		return(error);

	if((error = i2c_send1_receive(entry->address, lm75_reg_conf, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	if((i2c_buffer[0] & ~lm75_reg_conf_reserved) != lm75_reg_conf_no_shutdown)
		return(i2c_error_device_error_1);

	sensor_register(bus, entry->id);

	return(i2c_error_ok);
}

static i2c_error_t sensor_lm75_read(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_value_t *value, i2c_sensor_device_data_t *data)
{
	uint8_t i2c_buffer[2];
	i2c_error_t error;
	int16_t raw_temperature;

	if((error = i2c_send1_receive(entry->address, lm75_reg_temp, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	value->raw = raw_temperature = (i2c_buffer[0] << 8) | i2c_buffer[1];
	value->cooked = raw_temperature / 256.0;

	return(i2c_error_ok);
}

enum
{
	mpl3115_reg_out_p =			0x01,
	mpl3115_reg_out_t =			0x04,
	mpl3115_reg_drstatus =		0x06,
	mpl3115_reg_whoami =		0x0c,
	mpl3115_reg_ptdatacfg =		0x13,
	mpl3115_reg_ctrl_reg1 =		0x26,
	mpl3115_reg_ctrl_reg2 =		0x27,

	mpl3115_drstatus_tdr =		(1 << 1),
	mpl3115_drstatus_pdr =		(1 << 2),
	mpl3115_drstatus_ptdr =		(1 << 3),
	mpl3115_drstatus_tow =		(1 << 5),
	mpl3115_drstatus_pow =		(1 << 6),
	mpl3115_drstatus_ptow =		(1 << 7),

	mpl3115_id_mpl3115a2 =		0xc4,

	mpl3115_ptdatacfg_tdefe =	(1 << 0),
	mpl3115_ptdatacfg_pdefe =	(1 << 1),
	mpl3115_ptdatacfg_drem =	(1 << 2),

	mpl3115_ctrl1_sbyb =		(1 << 0),
	mpl3115_ctrl1_ost =			(1 << 1),
	mpl3115_ctrl1_reset =		(1 << 2),
	mpl3115_ctrl1_os_1 =		(0 << 5) | (0 << 4) | (0 << 3),
	mpl3115_ctrl1_os_2 =		(0 << 5) | (0 << 4) | (1 << 3),
	mpl3115_ctrl1_os_4 =		(0 << 5) | (1 << 4) | (0 << 3),
	mpl3115_ctrl1_os_8 =		(0 << 5) | (1 << 4) | (1 << 3),
	mpl3115_ctrl1_os_16 =		(1 << 5) | (0 << 4) | (0 << 3),
	mpl3115_ctrl1_os_32 =		(1 << 5) | (0 << 4) | (1 << 3),
	mpl3115_ctrl1_os_64 =		(1 << 5) | (1 << 4) | (0 << 3),
	mpl3115_ctrl1_os_128 =		(1 << 5) | (1 << 4) | (1 << 3),
	mpl3115_ctrl1_raw =			(1 << 6),
	mpl3115_ctrl1_alt =			(1 << 7),

	mpl3115_ctrl2_st =			(1 << 0) | (1 << 1) | (1 << 2) | (1 << 3),
	mpl3115_ctrl2_alarm =		(1 << 4),
	mpl3115_crtl2_load =		(1 << 5),
};

static i2c_error_t sensor_mpl3115a2_temperature_init(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_device_data_t *data)
{
	i2c_error_t error;
	uint8_t i2c_buffer;

	if((error = i2c_send1_receive(entry->address, mpl3115_reg_whoami, 1, &i2c_buffer)) != i2c_error_ok)
		return(error);

	if(i2c_buffer != mpl3115_id_mpl3115a2)
		return(i2c_error_address_nak);

	i2c_send2(entry->address, mpl3115_reg_ctrl_reg1, mpl3115_ctrl1_reset);

	msleep(1);

	if((error = i2c_send1_receive(entry->address, mpl3115_reg_ctrl_reg1, 1, &i2c_buffer)) != i2c_error_ok)
		return(error);

	if(i2c_buffer != 0x00)
		return(i2c_error_address_nak);

	if((error = i2c_send2(entry->address, mpl3115_reg_ptdatacfg, mpl3115_ptdatacfg_tdefe | mpl3115_ptdatacfg_pdefe)) != i2c_error_ok)
		return(error);

	if((error = i2c_send2(entry->address, mpl3115_reg_ctrl_reg1, mpl3115_ctrl1_os_128)) != i2c_error_ok)
		return(error);

	if((error = i2c_send2(entry->address, mpl3115_reg_ctrl_reg2, (0x00 & mpl3115_ctrl2_st))) != i2c_error_ok) // auto acquisition step = 1 sec
		return(error);

	if((error = i2c_send2(entry->address, mpl3115_reg_ctrl_reg1, mpl3115_ctrl1_os_128 | mpl3115_ctrl1_sbyb)) != i2c_error_ok) // go to "active" auto operation
		return(error);

	if((error = i2c_send1_receive(entry->address, mpl3115_reg_ctrl_reg1, 1, &i2c_buffer)) != i2c_error_ok)
		return(error);

	if(i2c_buffer != (mpl3115_ctrl1_os_128 | mpl3115_ctrl1_sbyb))
		return(i2c_error_device_error_1);

	sensor_register(bus, entry->id);
	sensor_register(bus, i2c_sensor_mpl3115a2_airpressure);

	return(i2c_error_ok);
}

static i2c_error_t sensor_mpl3115a2_temperature_read(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_value_t *value, i2c_sensor_device_data_t *data)
{
	uint8_t i2c_buffer[2];
	i2c_error_t error;

	if((error = i2c_send1_receive(entry->address, mpl3115_reg_out_t, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	value->raw = (i2c_buffer[0] << 8) | (i2c_buffer[1] << 0);
	value->cooked = value->raw / 256;

	return(i2c_error_ok);
}

static i2c_error_t sensor_mpl3115a2_airpressure_read(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_value_t *value, i2c_sensor_device_data_t *data)
{
	uint8_t i2c_buffer[3];
	i2c_error_t error;

	if((error = i2c_send1_receive(entry->address, mpl3115_reg_out_p, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	value->raw = (i2c_buffer[0] << 16 ) | (i2c_buffer[1] << 8) | (i2c_buffer[2] << 0);
	value->cooked = value->raw / 64 / 100;

	return(i2c_error_ok);
}

enum
{
	ccs811_reg_status			= 0x00,
	ccs811_reg_meas_mode		= 0x01,
	ccs811_reg_alg_result		= 0x02,
	ccs811_reg_hw_id			= 0x20,
	ccs811_reg_app_start		= 0xf4,
	ccs811_reg_reset			= 0xff,

	ccs811_status_app_mode		= 0b10000000,
	ccs811_status_app_valid		= 0b00010000,
	ccs811_status_data_ready	= 0b00001000,
	ccs811_status_error			= 0b00000001,

	ccs811_algdata_eco2			= 0x00,
	ccs811_algdata_tvoc			= 0x02,
	ccs811_algdata_status		= 0x04,
	ccs811_algdata_error_id		= 0x05,
	ccs811_raw_data				= 0x06,

	ccs811_mm_0 =				0b00000000, // idle
	ccs811_mm_1 =				0b00010000,	// constant power 1/1s
	ccs811_mm_2 =				0b00100000,	// pulse heat 1/10s
	ccs811_mm_3 =				0b00110000,	// low power pulse heat 1/60s
	ccs811_mm_4 =				0b01000000,	// constant power 4/1s
	ccs811_mm_int_en =			0b00001000,	// enable interrupts
	ccs811_mm_int_thr =			0b00000100,	// use interrupts for threshold crossing

	ccs811_hw_id =				0x81,

	ccs811_reset_seq_0 =		0x11,
	ccs811_reset_seq_1 =		0xe5,
	ccs811_reset_seq_2 =		0x72,
	ccs811_reset_seq_3 =		0x8a,
};

static i2c_error_t sensor_ccs811_init(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_device_data_t *data)
{
	i2c_error_t error;
	uint8_t i2c_buffer[8];

	if((error = i2c_send1_receive(entry->address, ccs811_reg_hw_id, 1, &i2c_buffer[0])) != i2c_error_ok)
		return(error);

	if(i2c_buffer[0] != ccs811_hw_id)
		return(i2c_error_address_nak);

	if((error = i2c_send1_receive(entry->address, ccs811_reg_status, 1, &i2c_buffer[0])) != i2c_error_ok)
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

	if((error = i2c_send1_receive(entry->address, ccs811_reg_status, 1, &i2c_buffer[0])) != i2c_error_ok)
		return(error);

	if(!(i2c_buffer[0] & ccs811_status_app_valid))
		return(i2c_error_device_error_1);

	if((error = i2c_send1(entry->address, ccs811_reg_app_start)) != i2c_error_ok)
		return(error);

	msleep(2);

	if((error = i2c_send2(entry->address, ccs811_reg_meas_mode, ccs811_mm_1)) != i2c_error_ok)
		return(error);

	if((error = i2c_send1_receive(entry->address, ccs811_reg_status, 1, &i2c_buffer[0])) != i2c_error_ok)
		return(error);

	if(i2c_buffer[0] & ccs811_status_error)
		return(i2c_error_device_error_2);

	sensor_register(bus, entry->id);

	return(i2c_error_ok);
}

static i2c_error_t sensor_ccs811_read(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_value_t *value, i2c_sensor_device_data_t *data)
{
	i2c_error_t error;
	uint8_t i2c_buffer[4];

	value->raw = value->cooked = -1;

	if((error = i2c_send1_receive(entry->address, ccs811_reg_alg_result, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	value->raw = (i2c_buffer[ccs811_algdata_eco2 + 0] << 8) | (i2c_buffer[ccs811_algdata_eco2 + 1] << 0);
	value->cooked = 100 - ((value->raw - 400) / 76);

	if(value->cooked < 0)
		value->cooked = 0;

	if(value->cooked > 100)
		value->cooked = 100;

	return(i2c_error_ok);
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

enum
{
	sht30_status_none =				0x00,
	sht30_status_write_checksum =	(1 << 0),
	sht30_status_command_status =	(1 << 1),
	sht30_status_reset_detected =	(1 << 4),
	sht30_status_temp_track_alert =	(1 << 10),
	sht30_status_hum_track_alert =	(1 << 11),
	sht30_status_heater =			(1 << 13),
	sht30_status_alert =			(1 << 15),

	sht30_inter_access_delay = 500,
};

attr_pure static uint8_t sht30_crc(int length, const uint8_t *data)
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

static i2c_error_t sht30_register_access(int address, sht30_cmd_t cmd, int *result1, int *result2)
{
	i2c_error_t error;
	uint8_t i2c_buffer[8];	// 2 + 3 + 3
	uint8_t crc_local, crc_remote;

	i2c_buffer[0] = (cmd & 0xff00) >> 8;
	i2c_buffer[1] = (cmd & 0x00ff) >> 0;

	os_delay_us(sht30_inter_access_delay);

	if((error = i2c_send(address, 2, &i2c_buffer[0])) != i2c_error_ok)
		return(error);

	if(result1)
	{
		os_delay_us(sht30_inter_access_delay);

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

static i2c_error_t sensor_sht30_humidity_init(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_device_data_t *data)
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

	sensor_register(bus, entry->id);
	sensor_register(bus, i2c_sensor_sht30_temperature);

	return(i2c_error_ok);
}

static i2c_sensor_value_t sht30_humidity_cache = { -1, -1 };
static i2c_sensor_value_t sht30_temperature_cache = { 0, 0 };

static i2c_error_t sht30_read(int address, i2c_sensor_value_t *value_humidity, i2c_sensor_value_t *value_temperature)
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

static i2c_error_t sensor_sht30_humidity_read(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_value_t *value, i2c_sensor_device_data_t *data)
{
	return(sht30_read(entry->address, value, (i2c_sensor_value_t *)0));
}

static i2c_error_t sensor_sht30_temperature_read(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_value_t *value, i2c_sensor_device_data_t *data)
{
	return(sht30_read(entry->address, (i2c_sensor_value_t *)0, value));
}

enum
{
	mcp9808_reg_rfu =				0b00000000,
	mcp9808_reg_config =			0b00000001,
	mcp9808_reg_alert_u =			0b00000010,
	mcp9808_reg_alert_l =			0b00000011,
	mcp9808_reg_critical =			0b00000100,
	mcp9808_reg_temperature =		0b00000101,
	mcp9808_reg_manufacturer =		0b00000110,
	mcp9808_reg_device_id =			0b00000111,
	mcp9808_reg_resolution =		0b00001000,

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

	mcp9808_resolution_0_5 =		0b00000000,
	mcp9808_resolution_0_25 =		0b00000001,
	mcp9808_resolution_0_125 =		0b00000010,
	mcp9808_resolution_0_0625 =		0b00000011,

	mcp9808_manufacturer_id_0 =		0x00,
	mcp9808_manufacturer_id_1 =		0x54,
	mcp9808_device_id =				0x04,
};

static i2c_error_t sensor_mcp9808_init(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_device_data_t *data)
{
	i2c_error_t error;
	uint8_t i2c_buffer[2];

	if((error = i2c_send1_receive(entry->address, mcp9808_reg_manufacturer, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	if((i2c_buffer[0] != mcp9808_manufacturer_id_0) || (i2c_buffer[1] != mcp9808_manufacturer_id_1))
		return(i2c_error_address_nak);

	if((error = i2c_send1_receive(entry->address, mcp9808_reg_device_id, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	if((i2c_buffer[0] != mcp9808_device_id))
		return(i2c_error_address_nak);

	if((error = i2c_send2(entry->address, mcp9808_reg_config, mcp9808_config_int_clear)) != i2c_error_ok)
		return(error);

	if((error = i2c_send2(entry->address, mcp9808_reg_resolution, mcp9808_resolution_0_0625)) != i2c_error_ok)
		return(error);

	sensor_register(bus, entry->id);

	return(i2c_error_ok);
}

static i2c_error_t sensor_mcp9808_read(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_value_t *value, i2c_sensor_device_data_t *data)
{
	i2c_error_t error;
	uint8_t i2c_buffer[2];
	unsigned int raw;

	if((error = i2c_send1_receive(entry->address, mcp9808_reg_temperature, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	raw = (i2c_buffer[0] << 8) | (i2c_buffer[1] << 0);
	value->raw = raw;
	value->cooked = (raw & 0x0fff) / 16.0;

	if(raw & (1 << 12))
		value->cooked = 256 - value->cooked;

	return(i2c_error_ok);
}

typedef enum
{
	hdc1080_action_humidity,
	hdc1080_action_temperature,
} hdc1080_action_t;

enum
{
	hdc1080_reg_data_temp =	0x00,
	hdc1080_reg_data_hum =	0x01,
	hdc1080_reg_conf =		0x02,
	hdc1080_reg_serial1 =	0xfb,
	hdc1080_reg_serial2 =	0xfc,
	hdc1080_reg_serial3 =	0xfd,
	hdc1080_reg_man_id =	0xfe,
	hdc1080_reg_dev_id =	0xff,

	hdc1080_man_id_1 =		0x54,
	hdc1080_man_id_2 =		0x49,
	hdc1080_dev_id_1 =		0x10,
	hdc1080_dev_id_2 =		0x50,

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

	hdc1080_max_attempts = 16
};

static i2c_error_t sensor_hdc1080_read(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_value_t *value, hdc1080_action_t action, i2c_sensor_device_data_t *data)
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

static i2c_error_t sensor_hdc1080_humidity_init(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_device_data_t *data)
{
	uint8_t i2c_buffer[2];
	i2c_error_t error;

	if((error = i2c_send1_receive(entry->address, hdc1080_reg_man_id, 2, i2c_buffer)) != i2c_error_ok)
		return(error);

	if((i2c_buffer[0] != hdc1080_man_id_1) || (i2c_buffer[1] != hdc1080_man_id_2))
		return(i2c_error_address_nak);

	if((error = i2c_send1_receive(entry->address, hdc1080_reg_dev_id, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	if((i2c_buffer[0] != hdc1080_dev_id_1) || (i2c_buffer[1] != hdc1080_dev_id_2))
		return(i2c_error_device_error_2);

	//if((error = i2c_send1_receive(entry->address, 0xff, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok) // FIXME?
		//return(error);

	if((error = i2c_send2(entry->address, hdc1080_reg_conf, hdc1080_conf_rst)) != i2c_error_ok)
		return(error);

	sensor_register(bus, entry->id);
	sensor_register(bus, i2c_sensor_hdc1080_temperature);

	return(i2c_error_ok);
}

static i2c_error_t sensor_hdc1080_temperature_read(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_value_t *value, i2c_sensor_device_data_t *data)
{
	return(sensor_hdc1080_read(bus, entry, value, hdc1080_action_temperature, data));
}

static i2c_error_t sensor_hdc1080_humidity_read(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_value_t *value, i2c_sensor_device_data_t *data)
{
	return(sensor_hdc1080_read(bus, entry, value, hdc1080_action_humidity, data));
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

	htu21_user_reg_rh12_temp14 =			0b00000000,
	htu21_user_reg_rh8_temp12 =				0b00000001,
	htu21_user_reg_rh10_temp13 =			0b10000000,
	htu21_user_reg_rh11_temp11 =			0b10000001,
	htu21_user_reg_bat_stat =				0b01000000,
	htu21_user_reg_reserved =				0b00111000,
	htu21_user_reg_heater_enable =			0b00000100,
	htu21_user_reg_otp_reload_disable =		0b00000010,

	htu21_status_mask =						0b00000011,
	htu21_status_measure_temperature =		0b00000000,
	htu21_status_measure_humidity =			0b00000010,

	htu21_delay_reset =						2,
};

attr_pure static uint8_t htu21_crc(int length, const uint8_t *data)
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

static struct
{
	struct
	{
		unsigned int temperature;
		unsigned int humidity;
	} adc;
} htu21;

static i2c_error_t sensor_htu21_init(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_device_data_t *data)
{
	i2c_error_t error;
	uint8_t i2c_buffer[1];

	msleep(1);

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

	if((error = i2c_send1(entry->address, htu21_cmd_meas_temp_no_hold_master)) != i2c_error_ok)
		return(error);

	sensor_register(bus, entry->id);
	sensor_register(bus, i2c_sensor_htu21_humidity);

	return(i2c_error_ok);
}

static void sensor_htu21_periodic(const struct i2c_sensor_device_table_entry_T *entry, i2c_sensor_device_data_t *data)
{
	i2c_error_t error;
	uint8_t	i2c_buffer[4];
	uint8_t crc1, crc2;
	unsigned int result;

	if((error = i2c_receive(entry->address, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return;

	crc1 = i2c_buffer[2];
	crc2 = htu21_crc(2, &i2c_buffer[0]);

	if(crc1 != crc2)
	{
		log("htu21: crc invalid\n");
		goto error;
	}

	result = unsigned_16(i2c_buffer[0], i2c_buffer[1]);
	result &= ~htu21_status_mask;

	if(i2c_buffer[1] & (htu21_status_measure_humidity))
	{
		htu21.adc.humidity = result;
		i2c_send1(entry->address, htu21_cmd_meas_temp_no_hold_master);
	}
	else
	{
		htu21.adc.temperature = result;
		i2c_send1(entry->address, htu21_cmd_meas_hum_no_hold_master);
	}

	return;

error:
	i2c_send1(entry->address, htu21_cmd_meas_temp_no_hold_master);
}

static i2c_error_t sensor_htu21_temperature_read(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_value_t *value, i2c_sensor_device_data_t *data)
{
	if(htu21.adc.temperature == 0)
		return(i2c_error_device_error_1);

	value->raw = htu21.adc.temperature;
	value->cooked = ((htu21.adc.temperature * 175.72) / 65536) - 46.85;

	return(i2c_error_ok);
}

static i2c_error_t sensor_htu21_humidity_read(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_value_t *value, i2c_sensor_device_data_t *data)
{
	double temperature, humidity;

	if(htu21.adc.humidity == 0)
		return(i2c_error_device_error_1);

	temperature = ((htu21.adc.temperature * 175.72) / 65536) - 46.85;
	humidity = (((htu21.adc.humidity * 125.0) / 65536) - 6) + ((25 - temperature) * -0.10); // FIXME, TempCoeff guessed

	if(humidity < 0)
		humidity = 0;

	if(humidity > 100)
		humidity = 100;

	value->raw = htu21.adc.humidity;
	value->cooked = humidity;

	return(i2c_error_ok);
}

enum
{
	bme680_reg_eas_status_0 =	0x1d,

	bme680_reg_press_msb =		0x1f,
	bme680_reg_press_lsb =		0x20,
	bme680_reg_press_xlsb =		0x21,

	bme680_reg_temp_msb =		0x22,
	bme680_reg_temp_lsb =		0x23,
	bme680_reg_temp_xlsb =		0x24,

	bme680_reg_hum_msb =		0x25,
	bme680_reg_hum_lsb =		0x26,

	bme680_reg_ctrl_gas_0 =		0x70,
	bme680_reg_ctrl_hum =		0x72,
	bme680_reg_status =			0x73,
	bme680_reg_ctrl_meas =		0x74,
	bme680_reg_config =			0x75,
	bme680_reg_calibration_1 =	0x89,
	bme680_reg_id =				0xd0,
	bme680_reg_reset =			0xe0,
	bme680_reg_calibration_2 =	0xe1,

	bme680_reg_eas_status_0_new_data_0 =		0b10000000,
	bme680_reg_eas_status_0_measuring =			0b00100000,

	bme680_reg_ctrl_gas_0_heat_on =				0b00000000,
	bme680_reg_ctrl_gas_0_heat_off =			0b00001000,

	bme680_reg_ctrl_gas_1_run_gas =				0b00010000,

	bme680_reg_ctrl_hum_osrh_h_skip =			0b00000000,
	bme680_reg_ctrl_hum_osrh_h_1 =				0b00000001,
	bme680_reg_ctrl_hum_osrh_h_2 =				0b00000010,
	bme680_reg_ctrl_hum_osrh_h_4 =				0b00000011,
	bme680_reg_ctrl_hum_osrh_h_8 =				0b00000100,
	bme680_reg_ctrl_hum_osrh_h_16 =				0b00000101,

	bme680_reg_ctrl_meas_osrs_t_skip =			0b00000000,
	bme680_reg_ctrl_meas_osrs_t_1 =				0b00100000,
	bme680_reg_ctrl_meas_osrs_t_2 =				0b01000000,
	bme680_reg_ctrl_meas_osrs_t_4 =				0b01100000,
	bme680_reg_ctrl_meas_osrs_t_8 =				0b10000000,
	bme680_reg_ctrl_meas_osrs_t_16 =			0b10100000,

	bme680_reg_ctrl_meas_osrs_mask =			0b00011100,
	bme680_reg_ctrl_meas_osrs_p_skip =			0b00000000,
	bme680_reg_ctrl_meas_osrs_p_1 =				0b00000100,
	bme680_reg_ctrl_meas_osrs_p_2 =				0b00001000,
	bme680_reg_ctrl_meas_osrs_p_4 =				0b00001100,
	bme680_reg_ctrl_meas_osrs_p_8 =				0b00010000,

	bme680_reg_ctrl_meas_sleep =				0b00000000,
	bme680_reg_ctrl_meas_forced =				0b00000001,

	bme680_reg_config_filter_mask =				0b00011100,
	bme680_reg_config_filter_0 =				0b00000000,
	bme680_reg_config_filter_1 =				0b00000100,
	bme680_reg_config_filter_3 =				0b00001000,
	bme680_reg_config_filter_7 =				0b00001100,
	bme680_reg_config_filter_15 =				0b00010000,
	bme680_reg_config_filter_31 =				0b00010100,
	bme680_reg_config_filter_63 =				0b00011000,
	bme680_reg_config_filter_127 =				0b00011100,

	bme680_reg_id_bme680 =					0x61,

	bme680_reg_reset_value =				0xb6,

	bme680_calibration_1_size =				25,
	bme680_calibration_2_size =				16,

	bme680_calibration_offset_t2_lsb =		1,
	bme680_calibration_offset_t2_msb =		2,
	bme680_calibration_offset_t3 =			3,

	bme680_calibration_offset_p1_lsb =		5,
	bme680_calibration_offset_p1_msb =		6,
	bme680_calibration_offset_p2_lsb =		7,
	bme680_calibration_offset_p2_msb =		8,
	bme680_calibration_offset_p3 =			9,
	bme680_calibration_offset_p4_lsb =		11,
	bme680_calibration_offset_p4_msb =		12,
	bme680_calibration_offset_p5_lsb =		13,
	bme680_calibration_offset_p5_msb =		14,
	bme680_calibration_offset_p7 =			15,
	bme680_calibration_offset_p6 =			16,
	bme680_calibration_offset_p8_lsb =		19,
	bme680_calibration_offset_p8_msb =		20,
	bme680_calibration_offset_p9_lsb =		21,
	bme680_calibration_offset_p9_msb =		22,
	bme680_calibration_offset_p10 =			23,

	bme680_calibration_offset_h2_msb =		25,
	bme680_calibration_offset_h2_lsb =		26,
	bme680_calibration_offset_h1_lsb =		26,
	bme680_calibration_offset_h1_msb =		27,
	bme680_calibration_offset_h3 =			28,
	bme680_calibration_offset_h4 =			29,
	bme680_calibration_offset_h5 =			30,
	bme680_calibration_offset_h6 =			31,
	bme680_calibration_offset_h7 =			32,

	bme680_calibration_offset_t1_lsb =		33,
	bme680_calibration_offset_t1_msb =		34,
};
typedef struct
{
	struct
	{
		unsigned int	t1;
		int				t2;
		int				t3;
	} temperature;

	struct
	{
		unsigned int	p1;
		int				p2;
		int				p3;
		int				p4;
		int				p5;
		int				p6;
		int				p7;
		int				p8;
		int				p9;
		unsigned int	p10;
	} pressure;

	struct
	{
		unsigned int	h1;
		unsigned int	h2;
		int				h3;
		int				h4;
		int				h5;
		unsigned int	h6;
		int				h7;
	} humidity;

} bme680_calibration_parameters_t;

static bme680_calibration_parameters_t bme680_calibration_parameters;

static i2c_error_t sensor_bme680_init(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_device_data_t *data)
{
	i2c_error_t error;
	uint8_t i2c_buffer[1];
	uint8_t calibration[bme680_calibration_1_size + bme680_calibration_2_size];

	if((error = i2c_send1_receive(entry->address, bme680_reg_id, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	if(i2c_buffer[0] != bme680_reg_id_bme680)
		return(i2c_error_address_nak);

	if((error = i2c_send2(entry->address, bme680_reg_reset, bme680_reg_reset_value)) != i2c_error_ok)
		return(error);

	msleep(1);

	if((error = i2c_send2(entry->address, bme680_reg_ctrl_meas, bme680_reg_ctrl_meas_sleep)) != i2c_error_ok)
		return(error);

	if((error = i2c_receive(entry->address, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	if(i2c_buffer[0] != 0x00)
		return(i2c_error_address_nak);

	if((error = i2c_send1_receive(entry->address, bme680_reg_calibration_1, bme680_calibration_1_size, &calibration[0])) != i2c_error_ok)
		return(error);

	if((error = i2c_send1_receive(entry->address, bme680_reg_calibration_2, bme680_calibration_2_size, &calibration[bme680_calibration_1_size])) != i2c_error_ok)
		return(error);

	bme680_calibration_parameters.temperature.t1 =	unsigned_16(calibration[bme680_calibration_offset_t1_msb], calibration[bme680_calibration_offset_t1_lsb]);
	bme680_calibration_parameters.temperature.t2 =	signed_16(calibration[bme680_calibration_offset_t2_msb], calibration[bme680_calibration_offset_t2_lsb]);
	bme680_calibration_parameters.temperature.t3 =	signed_8(calibration[bme680_calibration_offset_t3]);

	bme680_calibration_parameters.pressure.p1 =		unsigned_16(calibration[bme680_calibration_offset_p1_msb], calibration[bme680_calibration_offset_p1_lsb]);
	bme680_calibration_parameters.pressure.p2 =		signed_16(calibration[bme680_calibration_offset_p2_msb], calibration[bme680_calibration_offset_p2_lsb]);
	bme680_calibration_parameters.pressure.p3 =		signed_8(calibration[bme680_calibration_offset_p3]);
	bme680_calibration_parameters.pressure.p4 =		signed_16(calibration[bme680_calibration_offset_p4_msb], calibration[bme680_calibration_offset_p4_lsb]);
	bme680_calibration_parameters.pressure.p5 =		signed_16(calibration[bme680_calibration_offset_p5_msb], calibration[bme680_calibration_offset_p5_lsb]);
	bme680_calibration_parameters.pressure.p6 =		signed_8(calibration[bme680_calibration_offset_p6]);
	bme680_calibration_parameters.pressure.p7 =		signed_8(calibration[bme680_calibration_offset_p7]);
	bme680_calibration_parameters.pressure.p8 =		signed_16(calibration[bme680_calibration_offset_p8_msb], calibration[bme680_calibration_offset_p8_lsb]);
	bme680_calibration_parameters.pressure.p9 =		signed_16(calibration[bme680_calibration_offset_p9_msb], calibration[bme680_calibration_offset_p9_lsb]);
	bme680_calibration_parameters.pressure.p10 =	unsigned_8(calibration[bme680_calibration_offset_p10]);

	bme680_calibration_parameters.humidity.h1 =		unsigned_12(calibration[bme680_calibration_offset_h1_msb], (calibration[bme680_calibration_offset_h1_lsb] & 0x0f) >> 0);
	bme680_calibration_parameters.humidity.h2 =		unsigned_12(calibration[bme680_calibration_offset_h2_msb], (calibration[bme680_calibration_offset_h2_lsb] & 0xf0) >> 4);
	bme680_calibration_parameters.humidity.h3 =		signed_8(calibration[bme680_calibration_offset_h3]);
	bme680_calibration_parameters.humidity.h4 =		signed_8(calibration[bme680_calibration_offset_h4]);
	bme680_calibration_parameters.humidity.h5 =		signed_8(calibration[bme680_calibration_offset_h5]);
	bme680_calibration_parameters.humidity.h6 =		unsigned_8(calibration[bme680_calibration_offset_h6]);
	bme680_calibration_parameters.humidity.h7 =		signed_8(calibration[bme680_calibration_offset_h7]);

	if((error = i2c_send2(entry->address, bme680_reg_config, bme680_reg_config_filter_127)) != i2c_error_ok)
		return(error);

	if((error = i2c_send2(entry->address, bme680_reg_ctrl_gas_0, bme680_reg_ctrl_gas_0_heat_off)) != i2c_error_ok)
		return(error);

	sensor_register(bus, entry->id);
	sensor_register(bus, i2c_sensor_bme680_humidity);
	sensor_register(bus, i2c_sensor_bme680_airpressure);

	return(i2c_error_ok);
}

static void sensor_bme680_periodic(const struct i2c_sensor_device_table_entry_T *entry, i2c_sensor_device_data_t *data)
{
	uint8_t i2c_buffer[4];

	if(i2c_send1_receive(entry->address, bme680_reg_ctrl_meas, 1, i2c_buffer) != i2c_error_ok)
		return;

	if(i2c_buffer[0] & bme680_reg_ctrl_meas_forced)
		return;

	if(i2c_send2(entry->address, bme680_reg_ctrl_hum, bme680_reg_ctrl_hum_osrh_h_16) != i2c_error_ok)
		return;

	if(i2c_send2(entry->address, bme680_reg_ctrl_meas, bme680_reg_ctrl_meas_osrs_t_16 | bme680_reg_ctrl_meas_osrs_p_8 | bme680_reg_ctrl_meas_forced) != i2c_error_ok)
		return;
}

static i2c_error_t sensor_bme680_temperature_read(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_value_t *value, i2c_sensor_device_data_t *data)
{
	i2c_error_t error;
	uint8_t i2c_buffer[4];
	unsigned int adc_temperature;
	double t_fine, t1_scaled;

	if((error = i2c_send1_receive(entry->address, bme680_reg_eas_status_0, 1, i2c_buffer)) != i2c_error_ok)
		return(error);

	if((error = i2c_send1_receive(entry->address, bme680_reg_temp_msb, 3, i2c_buffer)) != i2c_error_ok)
		return(error);

	adc_temperature = unsigned_20(i2c_buffer[0], i2c_buffer[1], i2c_buffer[2]);

	t1_scaled	= (adc_temperature / 131072.0) - (bme680_calibration_parameters.temperature.t1 / 8192.0);
	t_fine		= ((adc_temperature / 16384.0) - (bme680_calibration_parameters.temperature.t1 / 1024.0)) * bme680_calibration_parameters.temperature.t2 + (t1_scaled * t1_scaled * bme680_calibration_parameters.temperature.t3 * 16.0);

	value->raw		= t_fine;
	value->cooked	= t_fine / 5120.0;

	return(i2c_error_ok);
}

static i2c_error_t sensor_bme680_humidity_read(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_value_t *value, i2c_sensor_device_data_t *data)
{
	i2c_error_t error;
	uint8_t i2c_buffer[4];
	unsigned int adc_humidity, adc_temperature;
	double var1, var2, var3, var4;
	double t_fine, t1_scaled, temperature, humidity;

	if((error = i2c_send1_receive(entry->address, bme680_reg_eas_status_0, 1, i2c_buffer)) != i2c_error_ok)
		return(error);

	if((error = i2c_send1_receive(entry->address, bme680_reg_temp_msb, 3, i2c_buffer)) != i2c_error_ok)
		return(error);

	adc_temperature = unsigned_20(i2c_buffer[0], i2c_buffer[1], i2c_buffer[2]);

	if((error = i2c_send1_receive(entry->address, bme680_reg_hum_msb, 2, i2c_buffer)) != i2c_error_ok)
		return(error);

	adc_humidity = unsigned_16(i2c_buffer[0], i2c_buffer[1]);

	t1_scaled	= (adc_temperature / 131072.0) - (bme680_calibration_parameters.temperature.t1 / 8192.0);
	t_fine		= ((adc_temperature / 16384.0) - (bme680_calibration_parameters.temperature.t1 / 1024.0)) * bme680_calibration_parameters.temperature.t2 + (t1_scaled * t1_scaled * bme680_calibration_parameters.temperature.t3 * 16.0);
	temperature	= t_fine / 5120.0;

	var1 = adc_humidity - ((bme680_calibration_parameters.humidity.h1 * 16) + ((bme680_calibration_parameters.humidity.h3 / 2.0) * temperature));
	var2 = var1 * ((bme680_calibration_parameters.humidity.h2 / 262144.0) * (1 + ((bme680_calibration_parameters.humidity.h4 / 16384.0) * temperature) + ((bme680_calibration_parameters.humidity.h5 / 1048576.0) * temperature * temperature)));
	var3 = bme680_calibration_parameters.humidity.h6 / 16384.0;
	var4 = bme680_calibration_parameters.humidity.h7 / 2097152.0;

	humidity = var2 + ((var3 + (var4 * temperature)) * var2 * var2);

	if(humidity > 100.0)
		humidity = 100.0;
	if(humidity < 0.0)
		humidity = 0.0;

	value->raw = adc_humidity;
	value->cooked = humidity;

	return(i2c_error_ok);
}

static i2c_error_t sensor_bme680_airpressure_read(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_value_t *value, i2c_sensor_device_data_t *data)
{
	i2c_error_t error;
	uint8_t i2c_buffer[4];
	unsigned int adc_temperature, adc_pressure;
	double var1, var2, var3;
	double pressure, pressure_256;
	double t_fine, t1_scaled;

	if((error = i2c_send1_receive(entry->address, bme680_reg_eas_status_0, 1, i2c_buffer)) != i2c_error_ok)
		return(error);

	if((error = i2c_send1_receive(entry->address, bme680_reg_temp_msb, 3, i2c_buffer)) != i2c_error_ok)
		return(error);

	adc_temperature = unsigned_20(i2c_buffer[0], i2c_buffer[1], i2c_buffer[2]);

	if((error = i2c_send1_receive(entry->address, bme680_reg_press_msb, 3, i2c_buffer)) != i2c_error_ok)
		return(error);

	adc_pressure = unsigned_20(i2c_buffer[0], i2c_buffer[1], i2c_buffer[2]);

	t1_scaled	= (adc_temperature / 131072.0) - (bme680_calibration_parameters.temperature.t1 / 8192.0);
	t_fine		= ((adc_temperature / 16384.0) - (bme680_calibration_parameters.temperature.t1 / 1024.0)) * bme680_calibration_parameters.temperature.t2 + (t1_scaled * t1_scaled * bme680_calibration_parameters.temperature.t3 * 16.0);

	var1 = (t_fine / 2.0) - 64000;
	var2 = var1 * var1 * bme680_calibration_parameters.pressure.p6 / 131072.0;
	var2 = var2 + (var1 * bme680_calibration_parameters.pressure.p5 * 2);
	var2 = (var2 / 4) + (bme680_calibration_parameters.pressure.p4 * 65536);
	var1 = (((bme680_calibration_parameters.pressure.p3 * var1 * var1) / 16384.0) + (bme680_calibration_parameters.pressure.p2 * var1)) / 524288.0;
	var1 = (1 + (var1 / 32768.0)) * bme680_calibration_parameters.pressure.p1;
	pressure = 1048576 - adc_pressure;

	value->raw		= adc_pressure;
	value->cooked	= 0;

	if((int)var1 != 0)
	{
		pressure = ((pressure - (var2 / 4096.0)) * 6250) / var1;
		pressure_256 = pressure / 256.0;
		var1 = (bme680_calibration_parameters.pressure.p9 * pressure * pressure) / 2147483648.0;
		var2 = pressure * (bme680_calibration_parameters.pressure.p8 / 32768.0);
		var3 = pressure_256 * pressure_256 * pressure_256 * (bme680_calibration_parameters.pressure.p10 / 131072.0);
		value->cooked = (pressure + (var1 + var2 + var3 + (bme680_calibration_parameters.pressure.p7 * 128)) / 16.0) / 100.0;
	}
	else
		return(i2c_error_overflow);

	return(i2c_error_ok);
}

enum
{
	bmx280_reg_id =						0xd0,
	bmx280_reg_reset =					0xe0,
	bmx280_reg_ctrl_hum =				0xf2,
	bmx280_reg_status =					0xf3,
	bmx280_reg_ctrl_meas =				0xf4,
	bmx280_reg_config =					0xf5,
	bmx280_reg_adc =					0xf7,
	bmx280_reg_adc_pressure_msb =		0xf7,
	bmx280_reg_adc_pressure_lsb =		0xf8,
	bmx280_reg_adc_pressure_xlsb =		0xf9,
	bmx280_reg_adc_temperature_msb =	0xfa,
	bmx280_reg_adc_temperature_lsb =	0xfb,
	bmx280_reg_adc_temperature_xlsb =	0xfc,
	bmx280_reg_adc_humidity_msb =		0xfd,
	bmx280_reg_adc_humidity_lsb =		0xfe,

	bmx280_reg_id_bmp280 =		0x58,
	bmx280_reg_id_bme280 =		0x60,

	bmx280_reg_reset_value =	0xb6,

	bmx280_reg_ctrl_hum_osrs_h_skip =	0b00000000,
	bmx280_reg_ctrl_hum_osrs_h_1 =		0b00000001,
	bmx280_reg_ctrl_hum_osrs_h_2 =		0b00000010,
	bmx280_reg_ctrl_hum_osrs_h_4 =		0b00000011,
	bmx280_reg_ctrl_hum_osrs_h_8 =		0b00000100,
	bmx280_reg_ctrl_hum_osrs_h_16 =		0b00000101,

	bmx280_reg_status_measuring =		0b00001000,
	bmx280_reg_status_im_update =		0b00000001,

	bmx280_reg_ctrl_meas_osrs_t_skip =	0b00000000,
	bmx280_reg_ctrl_meas_osrs_t_1 =		0b00100000,
	bmx280_reg_ctrl_meas_osrs_t_2 =		0b01000000,
	bmx280_reg_ctrl_meas_osrs_t_4 =		0b01100000,
	bmx280_reg_ctrl_meas_osrs_t_8 =		0b10000000,
	bmx280_reg_ctrl_meas_osrs_t_16 =	0b10100000,
	bmx280_reg_ctrl_meas_osrs_p_skip =	0b00000000,
	bmx280_reg_ctrl_meas_osrs_p_1 =		0b00000100,
	bmx280_reg_ctrl_meas_osrs_p_2 =		0b00001000,
	bmx280_reg_ctrl_meas_osrs_p_4 =		0b00001100,
	bmx280_reg_ctrl_meas_osrs_p_8 =		0b00010000,
	bmx280_reg_ctrl_meas_osrs_p_16 =	0b00010100,
	bmx280_reg_ctrl_meas_mode_mask =	0b00000011,
	bmx280_reg_ctrl_meas_mode_sleep =	0b00000000,
	bmx280_reg_ctrl_meas_mode_forced =	0b00000010,
	bmx280_reg_ctrl_meas_mode_normal =	0b00000011,

	bmx280_reg_config_t_sb_05 =			0b00000000,
	bmx280_reg_config_t_sb_62 =			0b00100000,
	bmx280_reg_config_t_sb_125 =		0b01000000,
	bmx280_reg_config_t_sb_250 =		0b01100000,
	bmx280_reg_config_t_sb_500 =		0b10000000,
	bmx280_reg_config_t_sb_1000 =		0b10100000,
	bmx280_reg_config_t_sb_10000 =		0b11000000,
	bmx280_reg_config_t_sb_20000 =		0b11100000,
	bmx280_reg_config_filter_off =		0b00000000,
	bmx280_reg_config_filter_2 =		0b00000100,
	bmx280_reg_config_filter_4 =		0b00001000,
	bmx280_reg_config_filter_8 =		0b00001100,
	bmx280_reg_config_filter_16 =		0b00010000,
	bmx280_reg_config_spi3w_en =		0b00000001,
};

static struct
{
	unsigned	int	dig_T1;		//	88/89
				int	dig_T2;		//	8a/8b
				int	dig_T3;		//	8c/8d
	unsigned	int	dig_P1;		//	8e/8f
				int	dig_P2;		//	90/91
				int	dig_P3;		//	92/93
				int	dig_P4;		//	94/95
				int	dig_P5;		//	96/97
				int	dig_P6;		//	98/99
				int	dig_P7;		//	9a/9b
				int dig_P8;		//	9c/9d
				int	dig_P9;		//	9e/9f
	unsigned	int	dig_H1;		//	a1
				int	dig_H2;		//	e1/e2
	unsigned	int	dig_H3;		//	e3
				int	dig_H4;		//	e4/e5[3:0]
				int	dig_H5;		//	e5[7:4]/e6
				int	dig_H6;		//	e7
} bmx280;

static i2c_error_t bmx280_init(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_device_data_t *data, uint8_t device_id)
{
	i2c_error_t		error;
	uint8_t			i2c_buffer[4];
	unsigned int	e4, e5, e6;

	if((error = i2c_send1_receive(entry->address, bmx280_reg_id, 1, i2c_buffer)) != i2c_error_ok)
		return(error);

	if(i2c_buffer[0] != device_id)
		return(i2c_error_address_nak);

	if((error = i2c_send2(entry->address, bmx280_reg_reset, bmx280_reg_reset_value)) != i2c_error_ok)
		return(error);

	msleep(1);

	if((error = i2c_send1_receive(entry->address, bmx280_reg_reset, 1, i2c_buffer)) != i2c_error_ok)
		return(error);

	if(i2c_buffer[0] != 0x00)
		return(error);

	/* read calibration data */

	if((error = i2c_send1_receive(entry->address, 0x88, 2, i2c_buffer)) != i2c_error_ok)
		return(error);
	bmx280.dig_T1 = unsigned_16(i2c_buffer[1], i2c_buffer[0]);

	if((error = i2c_send1_receive(entry->address, 0x8a, 2, i2c_buffer)) != i2c_error_ok)
		return(error);
	bmx280.dig_T2 = signed_16(i2c_buffer[1], i2c_buffer[0]);

	if((error = i2c_send1_receive(entry->address, 0x8c, 2, i2c_buffer)) != i2c_error_ok)
		return(error);
	bmx280.dig_T3 = signed_16(i2c_buffer[1], i2c_buffer[0]);

	if((error = i2c_send1_receive(entry->address, 0x8e, 2, i2c_buffer)) != i2c_error_ok)
		return(error);
	bmx280.dig_P1 = unsigned_16(i2c_buffer[1], i2c_buffer[0]);

	if((error = i2c_send1_receive(entry->address, 0x90, 2, i2c_buffer)) != i2c_error_ok)
		return(error);
	bmx280.dig_P2 = signed_16(i2c_buffer[1], i2c_buffer[0]);

	if((error = i2c_send1_receive(entry->address, 0x92, 2, i2c_buffer)) != i2c_error_ok)
		return(error);
	bmx280.dig_P3 = signed_16(i2c_buffer[1], i2c_buffer[0]);

	if((error = i2c_send1_receive(entry->address, 0x94, 2, i2c_buffer)) != i2c_error_ok)
		return(error);
	bmx280.dig_P4 = signed_16(i2c_buffer[1], i2c_buffer[0]);

	if((error = i2c_send1_receive(entry->address, 0x96, 2, i2c_buffer)) != i2c_error_ok)
		return(error);
	bmx280.dig_P5 = signed_16(i2c_buffer[1], i2c_buffer[0]);

	if((error = i2c_send1_receive(entry->address, 0x98, 2, i2c_buffer)) != i2c_error_ok)
		return(error);
	bmx280.dig_P6 = signed_16(i2c_buffer[1], i2c_buffer[0]);

	if((error = i2c_send1_receive(entry->address, 0x9a, 2, i2c_buffer)) != i2c_error_ok)
		return(error);
	bmx280.dig_P7 = signed_16(i2c_buffer[1], i2c_buffer[0]);

	if((error = i2c_send1_receive(entry->address, 0x9c, 2, i2c_buffer)) != i2c_error_ok)
		return(error);
	bmx280.dig_P8 = signed_16(i2c_buffer[1], i2c_buffer[0]);

	if((error = i2c_send1_receive(entry->address, 0x9e, 2, i2c_buffer)) != i2c_error_ok)
		return(error);
	bmx280.dig_P9 = signed_16(i2c_buffer[1], i2c_buffer[0]);

	if((error = i2c_send1_receive(entry->address, 0xa1, 1, i2c_buffer)) != i2c_error_ok)
		return(error);
	bmx280.dig_H1 = unsigned_8(i2c_buffer[0]);

	if((error = i2c_send1_receive(entry->address, 0xe1, 2, i2c_buffer)) != i2c_error_ok)
		return(error);
	bmx280.dig_H2 = signed_16(i2c_buffer[1], i2c_buffer[0]);

	if((error = i2c_send1_receive(entry->address, 0xe3, 1, i2c_buffer)) != i2c_error_ok)
		return(error);
	bmx280.dig_H3 = unsigned_8(i2c_buffer[0]);

	if((error = i2c_send1_receive(entry->address, 0xe4, 1, i2c_buffer)) != i2c_error_ok)
		return(error);
	e4 = unsigned_8(i2c_buffer[0]);

	if((error = i2c_send1_receive(entry->address, 0xe5, 1, i2c_buffer)) != i2c_error_ok)
		return(error);
	e5 = unsigned_8(i2c_buffer[0]);

	if((error = i2c_send1_receive(entry->address, 0xe6, 1, i2c_buffer)) != i2c_error_ok)
		return(error);
	e6 = unsigned_8(i2c_buffer[0]);

	bmx280.dig_H4 = ((e4 & 0xff) << 4) | ((e5 & 0x0f) >> 0);
	bmx280.dig_H5 = ((e6 & 0xff) << 4) | ((e5 & 0xf0) >> 4);

	if((error = i2c_send1_receive(entry->address, 0xe7, 1, i2c_buffer)) != i2c_error_ok)
		return(error);
	bmx280.dig_H6 = unsigned_8(i2c_buffer[0]);

	sensor_register(bus, entry->id);

	if(device_id ==	bmx280_reg_id_bmp280)
		sensor_register(bus, i2c_sensor_bmp280_airpressure);

	if(device_id == bmx280_reg_id_bme280)
	{
		sensor_register(bus, i2c_sensor_bme280_humidity);
		sensor_register(bus, i2c_sensor_bme280_airpressure);
	}

	return(i2c_error_ok);
}

static void bmx280_periodic(const struct i2c_sensor_device_table_entry_T *entry, i2c_sensor_device_data_t *data)
{
	uint8_t i2c_buffer[1];

	if(i2c_send1_receive(entry->address, bmx280_reg_ctrl_meas, 1, i2c_buffer) != i2c_error_ok)
		return;

	if((i2c_buffer[0] & bmx280_reg_ctrl_meas_mode_mask) != bmx280_reg_ctrl_meas_mode_sleep)
		return;

	if(i2c_send2(entry->address, bmx280_reg_ctrl_hum, bmx280_reg_ctrl_hum_osrs_h_16))
		return;

	if(i2c_send2(entry->address, bmx280_reg_config, bmx280_reg_config_filter_2))
		return;

	if(i2c_send2(entry->address, bmx280_reg_ctrl_meas, bmx280_reg_ctrl_meas_osrs_t_16 | bmx280_reg_ctrl_meas_osrs_p_16 | bmx280_reg_ctrl_meas_mode_forced))
		return;
}

static i2c_error_t bmx280_read_temperature(const i2c_sensor_device_table_entry_t *entry, i2c_sensor_value_t *value, i2c_sensor_device_data_t *data)
{
	i2c_error_t		error;
	uint8_t			i2c_buffer[8];
	int				adc_T;
	double			var1, var2;

	if((error = i2c_send1_receive(entry->address, bmx280_reg_adc, 8, i2c_buffer)) != i2c_error_ok)
		return(error);

	adc_T = ((i2c_buffer[3] << 16) | (i2c_buffer[4] << 8) | (i2c_buffer[5] << 0)) >> 4;

	var1 = (adc_T / 16384.0 - bmx280.dig_T1 / 1024.0) * bmx280.dig_T2;
	var2 = (adc_T / 131072.0 - bmx280.dig_T1 / 8192.0) * (adc_T / 131072.0 - bmx280.dig_T1 / 8192.0) * bmx280.dig_T3;

	value->raw = adc_T;
	value->cooked = (var1 + var2) / 5120.0;

	return(i2c_error_ok);
}

static i2c_error_t bmx280_read_airpressure(const i2c_sensor_device_table_entry_t *entry, i2c_sensor_value_t *value, i2c_sensor_device_data_t *data)
{
	i2c_error_t		error;
	uint8_t			i2c_buffer[8];
	int				adc_T, adc_P, t_fine;
	double			var1, var2, pressure;

	if((error = i2c_send1_receive(entry->address, bmx280_reg_adc, 8, i2c_buffer)) != i2c_error_ok)
		return(error);

	adc_P	= ((i2c_buffer[0] << 16) | (i2c_buffer[1] << 8) | (i2c_buffer[2] << 0)) >> 4;
	adc_T	= ((i2c_buffer[3] << 16) | (i2c_buffer[4] << 8) | (i2c_buffer[5] << 0)) >> 4;

	var1 = (adc_T / 16384.0 - bmx280.dig_T1 / 1024.0) * bmx280.dig_T2;
	var2 = (adc_T / 131072.0 - bmx280.dig_T1 / 8192.0) * (adc_T / 131072.0 - bmx280.dig_T1 / 8192.0) * bmx280.dig_T3;
	t_fine = var1 + var2;

	var1 = (t_fine / 2.0) - 64000.0;
	var2 = var1 * var1 * bmx280.dig_P6 / 32768.0;
	var2 = var2 + var1 * bmx280.dig_P5 * 2.0;
	var2 = (var2 / 4.0) + (bmx280.dig_P4 * 65536.0);
	var1 = (bmx280.dig_P3 * var1 * var1 / 524288.0 + bmx280.dig_P2 * var1) / 524288.0;
	var1 = (1.0 + var1 / 32768.0) * bmx280.dig_P1;

	if((int)var1 == 0)
		pressure = 0;
	else
	{
		pressure = 1048576.0 - adc_P;
		pressure = (pressure - (var2 / 4096.0)) * 6250.0 / var1;
		var1 = bmx280.dig_P9 * pressure * pressure / 2147483648.0;
		var2 = pressure * bmx280.dig_P8 / 32768.0;
		pressure = pressure + (var1 + var2 + bmx280.dig_P7) / 16.0;
	}

	value->raw = adc_P;
	value->cooked = pressure / 100.0;

	return(i2c_error_ok);
}

static i2c_error_t bmx280_read_humidity(const i2c_sensor_device_table_entry_t *entry, i2c_sensor_value_t *value, i2c_sensor_device_data_t *data)
{
	i2c_error_t		error;
	uint8_t			i2c_buffer[8];
	int				adc_T, adc_H;
	double			t_fine, var1, var2, humidity;

	if((error = i2c_send1_receive(entry->address, bmx280_reg_adc, 8, i2c_buffer)) != i2c_error_ok)
		return(error);

	adc_T	= ((i2c_buffer[3] << 16) |	(i2c_buffer[4] << 8) | (i2c_buffer[5] << 0)) >> 4;
	adc_H	= (							(i2c_buffer[6] << 8) | (i2c_buffer[7] << 0)) >> 0;

	var1 = (adc_T / 16384.0 - bmx280.dig_T1 / 1024.0) * bmx280.dig_T2;
	var2 = (adc_T / 131072.0 - bmx280.dig_T1 / 8192.0) * (adc_T / 131072.0 - bmx280.dig_T1 / 8192.0) * bmx280.dig_T3;
	t_fine = var1 + var2 - 76800;

	humidity = (adc_H - (bmx280.dig_H4 * 64.0 + bmx280.dig_H5 / 16384.0 * t_fine)) * (bmx280.dig_H2 / 65536.0 * (1.0 + bmx280.dig_H6 / 67108864.0 * t_fine * (1.0 + bmx280.dig_H3 / 67108864.0 * t_fine)));
	humidity = humidity * (1.0 - bmx280.dig_H1 * humidity / 524288.0);

	if(humidity > 100.0)
		humidity = 100.0;

	if(humidity < 0.0)
		humidity = 0.0;

	value->raw = adc_H;
	value->cooked = humidity;

	return(i2c_error_ok);
}

static i2c_error_t sensor_bme280_init(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_device_data_t *data)
{
	return(bmx280_init(bus, entry, data, bmx280_reg_id_bme280));
}

static void sensor_bme280_periodic(const struct i2c_sensor_device_table_entry_T *entry, i2c_sensor_device_data_t *data)
{
	return(bmx280_periodic(entry, data));
}

static i2c_error_t sensor_bme280_read_temperature(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_value_t *value, i2c_sensor_device_data_t *data)
{
	return(bmx280_read_temperature(entry, value, data));
}

static i2c_error_t sensor_bme280_read_airpressure(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_value_t *value, i2c_sensor_device_data_t *data)
{
	return(bmx280_read_airpressure(entry, value, data));
}

static i2c_error_t sensor_bme280_read_humidity(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_value_t *value, i2c_sensor_device_data_t *data)
{
	return(bmx280_read_humidity(entry, value, data));
}

static i2c_error_t sensor_bmp280_init(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_device_data_t *data)
{
	return(bmx280_init(bus, entry, data, bmx280_reg_id_bmp280));
}

static void sensor_bmp280_periodic(const struct i2c_sensor_device_table_entry_T *entry, i2c_sensor_device_data_t *data)
{
	return(bmx280_periodic(entry, data));
}

static i2c_error_t sensor_bmp280_read_temperature(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_value_t *value, i2c_sensor_device_data_t *data)
{
	return(bmx280_read_temperature(entry, value, data));
}

static i2c_error_t sensor_bmp280_read_airpressure(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_value_t *value, i2c_sensor_device_data_t *data)
{
	return(bmx280_read_airpressure(entry, value, data));
}

enum
{
	bmp085_reg_id =			0xd0,
	bmp085_reg_soft_reset =	0xe0,
	bmp085_reg_ctrl_meas =	0xf4,
	bmp085_reg_out_msb =	0xf6,
	bmp085_reg_out_lsb =	0xf7,
	bmp085_reg_out_xlsb =	0xf8,

	bmp085_reg_id_value_bmp085 =	0x55,
	bmp085_reg_soft_reset_value =	0xb6,

	bmp085_reg_ctrl_meas_os_1 =				0b00000000,
	bmp085_reg_ctrl_meas_os_2 =				0b01000000,
	bmp085_reg_ctrl_meas_os_4 =				0b10000000,
	bmp085_reg_ctrl_meas_os_8 =				0b11000000,
	bmp085_reg_ctrl_meas_sco =				0b00100000,
	bmp085_reg_ctrl_meas_temperature =		0b00001110,
	bmp085_reg_ctrl_meas_pressure =			0b00010100,
	bmp085_reg_ctrl_meas_mode_temperature =	0x0a,
	bmp085_reg_ctrl_meas_mode_pressure =	0xd0,

	bmp085_oversampling = 3,	// (1 << 3) == 8, use bmp085_reg_ctrl_meas_os_8
};

static struct
{
	int				ac1;
	int				ac2;
	int				ac3;
	unsigned int	ac4;
	unsigned int	ac5;
	unsigned int	ac6;
	int				b1;
	int				b2;
	int				mc;
	int				md;
	unsigned int	adc_temperature;
	unsigned int	adc_pressure;
} bmp085_calibration_parameters;

static i2c_error_t sensor_bmp085_init(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_device_data_t *data)
{
	i2c_error_t error;
	uint8_t i2c_buffer[2];

	if((error = i2c_send1_receive(entry->address, bmp085_reg_id, 1, i2c_buffer)) != i2c_error_ok)
		return(error);

	if(i2c_buffer[0] != bmp085_reg_id_value_bmp085)
		return(i2c_error_address_nak);

	if((error = i2c_send2(entry->address, bmp085_reg_soft_reset, bmp085_reg_soft_reset_value)) != i2c_error_ok)
		return(error);

	msleep(1);

	if((error = i2c_send1_receive(entry->address, 0xaa, 2, i2c_buffer)) != i2c_error_ok)
		return(error);

	bmp085_calibration_parameters.ac1 = signed_16(i2c_buffer[0], i2c_buffer[1]);

	if((error = i2c_send1_receive(entry->address, 0xac, 2, i2c_buffer)) != i2c_error_ok)
		return(error);

	bmp085_calibration_parameters.ac2 = signed_16(i2c_buffer[0], i2c_buffer[1]);

	if((error = i2c_send1_receive(entry->address, 0xae, 2, i2c_buffer)) != i2c_error_ok)
		return(error);

	bmp085_calibration_parameters.ac3 = signed_16(i2c_buffer[0], i2c_buffer[1]);

	if((error = i2c_send1_receive(entry->address, 0xb0, 2, i2c_buffer)) != i2c_error_ok)
		return(error);

	bmp085_calibration_parameters.ac4 = unsigned_16(i2c_buffer[0], i2c_buffer[1]);

	if((error = i2c_send1_receive(entry->address, 0xb2, 2, i2c_buffer)) != i2c_error_ok)
		return(error);

	bmp085_calibration_parameters.ac5 = unsigned_16(i2c_buffer[0], i2c_buffer[1]);

	if((error = i2c_send1_receive(entry->address, 0xb4, 2, i2c_buffer)) != i2c_error_ok)
		return(error);

	bmp085_calibration_parameters.ac6 = unsigned_16(i2c_buffer[0], i2c_buffer[1]);

	if((error = i2c_send1_receive(entry->address, 0xb6, 2, i2c_buffer)) != i2c_error_ok)
		return(error);

	bmp085_calibration_parameters.b1 = signed_16(i2c_buffer[0], i2c_buffer[1]);

	if((error = i2c_send1_receive(entry->address, 0xb8, 2, i2c_buffer)) != i2c_error_ok)
		return(error);

	bmp085_calibration_parameters.b2 = signed_16(i2c_buffer[0], i2c_buffer[1]);

	if((error = i2c_send1_receive(entry->address, 0xbc, 2, i2c_buffer)) != i2c_error_ok)
		return(error);

	bmp085_calibration_parameters.mc = signed_16(i2c_buffer[0], i2c_buffer[1]);

	if((error = i2c_send1_receive(entry->address, 0xbe, 2, i2c_buffer)) != i2c_error_ok)
		return(error);

	bmp085_calibration_parameters.md = signed_16(i2c_buffer[0], i2c_buffer[1]);

	sensor_register(bus, entry->id);
	sensor_register(bus, i2c_sensor_bmp085_airpressure);

	return(i2c_error_ok);
}

static void sensor_bmp085_periodic(const struct i2c_sensor_device_table_entry_T *entry, i2c_sensor_device_data_t *data)
{
	uint8_t i2c_buffer[4];
	uint8_t mode, meas_value;

	if(i2c_send1_receive(entry->address, bmp085_reg_ctrl_meas, 1, i2c_buffer) != i2c_error_ok)
		return;

	mode = i2c_buffer[0];

	if(mode & bmp085_reg_ctrl_meas_sco)
		return;

	if(i2c_send1_receive(entry->address, bmp085_reg_out_msb, 3, i2c_buffer) != i2c_error_ok)
		return;

	if(mode == bmp085_reg_ctrl_meas_mode_temperature)
	{
		bmp085_calibration_parameters.adc_temperature = unsigned_16(i2c_buffer[0], i2c_buffer[1]);
		meas_value = bmp085_reg_ctrl_meas_pressure | bmp085_reg_ctrl_meas_os_8;
	}
	else
	{
		bmp085_calibration_parameters.adc_pressure = (i2c_buffer[0] << 16) | (i2c_buffer[1] << 8) | i2c_buffer[2];
		meas_value = bmp085_reg_ctrl_meas_temperature;
	}

	i2c_send2(entry->address, bmp085_reg_ctrl_meas, meas_value | bmp085_reg_ctrl_meas_sco);
}

static i2c_error_t sensor_bmp085_read_temperature(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_value_t *value, i2c_sensor_device_data_t *data)
{
	int	x1, x2, b5;
	unsigned ut = bmp085_calibration_parameters.adc_temperature;

	if((bmp085_calibration_parameters.adc_temperature == 0) || (bmp085_calibration_parameters.adc_pressure == 0))
		return(i2c_error_device_error_1);

	x1 = ((ut - bmp085_calibration_parameters.ac6) * bmp085_calibration_parameters.ac5) / (1 << 15);

	if((x1 + bmp085_calibration_parameters.md) == 0)
		return(i2c_error_device_error_1);

	x2 = (bmp085_calibration_parameters.mc * (1 << 11)) / (x1 + bmp085_calibration_parameters.md);

	b5 = x1 + x2;

	value->raw		= ut;
	value->cooked	= (b5 + 8.0) / 160.0;

	return(i2c_error_ok);
}

static i2c_error_t sensor_bmp085_read_airpressure(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_value_t *value, i2c_sensor_device_data_t *data)
{
	unsigned int b4, b7;
	int	p, x1, x2, x3, b3, b5, b6;
	unsigned int ut = bmp085_calibration_parameters.adc_temperature;
	int	up = bmp085_calibration_parameters.adc_pressure;

	if((bmp085_calibration_parameters.adc_temperature == 0) || (bmp085_calibration_parameters.adc_pressure == 0))
		return(i2c_error_device_error_1);

	x1 = ((ut - bmp085_calibration_parameters.ac6) * bmp085_calibration_parameters.ac5) / (1 << 15);

	if((x1 + bmp085_calibration_parameters.md) == 0)
		return(i2c_error_device_error_1);

	x2 = (bmp085_calibration_parameters.mc * (1 << 11)) / (x1 + bmp085_calibration_parameters.md);

	b5 = x1 + x2;

	up = up >> (8 - bmp085_oversampling);

	b6	= b5 - 4000;
	x1	= (bmp085_calibration_parameters.b2 * ((b6 * b6) / (1 << 12))) / (1 << 11);
	x2	= (bmp085_calibration_parameters.ac2 * b6) / (1 << 11);
	x3	= x1 + x2;
	b3	= (((bmp085_calibration_parameters.ac1 * 4 + x3) << bmp085_oversampling) + 2) / 4;
	x1	= (bmp085_calibration_parameters.ac3 * b6) / (1 << 13);
	x2	= (bmp085_calibration_parameters.b1 * ((b6 * b6) / (1 << 12))) / (1 << 16);
	x3	= (x1 + x2 + 2) / (1 << 2);
	b4	= (bmp085_calibration_parameters.ac4 * (x3 + 32768)) / (1 << 15);
	b7	= (up - b3) * (50000 >> bmp085_oversampling);

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

	value->raw = up;
	value->cooked = p / 100.0;

	return(i2c_error_ok);
}

typedef enum
{
	am2320_action_humidity,
	am2320_action_temperature,
} am2320_action_t;

static i2c_sensor_value_t sensor_am2320_cached_temperature;
static i2c_sensor_value_t sensor_am2320_cached_humidity;

attr_pure static uint16_t am2320_crc(int length, const uint8_t *data)
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

static i2c_error_t sensor_am2320_read_registers(int address, int offset, int length, uint8_t *values)
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

static i2c_error_t sensor_am2320_read(int address, i2c_sensor_value_t *value, am2320_action_t action, i2c_sensor_device_data_t *data)
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

	switch(action)
	{
		case(am2320_action_humidity):		{ *value = sensor_am2320_cached_humidity; break; }
		case(am2320_action_temperature):	{ *value = sensor_am2320_cached_temperature; break; }
		default:							{ break; }
	}

	return(i2c_error_ok);
}

static i2c_error_t sensor_am2320_humidity_init(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_device_data_t *data)
{
	i2c_error_t	error;
	uint8_t		values[2];

	//	0x08	start address: device id
	//	0x02	length

	if((error = sensor_am2320_read_registers(entry->address, 0x08, 0x02, values)) != i2c_error_ok)
		return(error);

	sensor_register(bus, entry->id);
	sensor_register(bus, i2c_sensor_am2320_temperature);

	return(i2c_error_ok);
}

static i2c_error_t sensor_am2320_humidity_read(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_value_t *value, i2c_sensor_device_data_t *data)
{
	return(sensor_am2320_read(entry->address, value, am2320_action_humidity, data));
}

static i2c_error_t sensor_am2320_temperature_read(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_value_t *value, i2c_sensor_device_data_t *data)
{
	return(sensor_am2320_read(entry->address, value, am2320_action_temperature, data));
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

static i2c_error_t sensor_hih6130_read(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_value_t *value, hih6130_action_t action, i2c_sensor_device_data_t *data)
{
	uint8_t i2c_buffer[4];
	i2c_error_t error;

	value->cooked = value->raw = -1;

	if((error = i2c_send(entry->address, 0, 0)) != i2c_error_ok)
		return(error);

	msleep(50);

	if((error = i2c_receive(entry->address, 4, i2c_buffer) != i2c_error_ok) || ((i2c_buffer[0] & hih6130_status_mask) != hih6130_status_normal))
		return((error == i2c_error_ok) ? i2c_error_device_error_1 : error);

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

static i2c_error_t sensor_hih6130_humidity_init(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_device_data_t *data)
{
	uint8_t i2c_buffer[4];
	i2c_error_t error;

	if((error = i2c_receive(entry->address, sizeof(i2c_buffer), i2c_buffer)) != i2c_error_ok)
		return(error);

	sensor_register(bus, entry->id);
	sensor_register(bus, i2c_sensor_hih6130_temperature);

	return(i2c_error_ok);
}

static i2c_error_t sensor_hih6130_temperature_read(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_value_t *value, i2c_sensor_device_data_t *data)
{
	return(sensor_hih6130_read(bus, entry, value, hih6130_action_temperature, data));
}

static i2c_error_t sensor_hih6130_humidity_read(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_value_t *value, i2c_sensor_device_data_t *data)
{
	return(sensor_hih6130_read(bus, entry, value, hih6130_action_humidity, data));
}

static i2c_error_t sensor_digipicco_humidity_init(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_device_data_t *data)
{
	i2c_error_t error;
	uint8_t	i2cbuffer[4] = { 0, 0, 0, 0 };

	if((error = i2c_receive(entry->address, 4, i2cbuffer)) != i2c_error_ok)
		return(error);

	return(i2c_error_ok);
}

static i2c_error_t sensor_digipicco_humidity_read(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_value_t *value, i2c_sensor_device_data_t *data)
{
	i2c_error_t error;
	uint8_t	i2cbuffer[4];

	if((error = i2c_receive(entry->address, 4, i2cbuffer)) != i2c_error_ok)
		return(error);

	value->raw = ((uint16_t)i2cbuffer[0] << 8) | (uint16_t)i2cbuffer[1];
	value->cooked = (value->raw * 100.0) / 32768.0;

	return(i2c_error_ok);
}

static i2c_error_t sensor_digipicco_temperature_read(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_value_t *value, i2c_sensor_device_data_t *data)
{
	i2c_error_t error;
	uint8_t	i2cbuffer[4];

	if((error = i2c_receive(entry->address, 4, i2cbuffer)) != i2c_error_ok)
		return(error);

	value->raw = ((uint16_t)i2cbuffer[2] << 8) | (uint16_t)i2cbuffer[3];
	value->cooked = ((value->raw * 165.0) / 32767.0) - 40.5;

	return(i2c_error_ok);
}

roflash static const i2c_sensor_device_table_entry_t device_table[] =
{
	{
		i2c_sensor_opt3001, 0x45, 2, 0,
		"opt3001", "visible light", "lx",
		sensor_opt3001_init,
		sensor_opt3001_read,
		(void *)0,
	},
	{
		i2c_sensor_veml6075_uvindex, 0x10, 1, 0,
		"veml6075", "ultraviolet light index", "",
		sensor_veml6075_uvindex_init,
		sensor_veml6075_uvindex_read,
		(void *)0,
	},
	{
		i2c_sensor_tmd2771, 0x39, 2, 0,
		"tmd2771", "visible light", "lx",
		sensor_tmd2771_init,
		sensor_tmd2771_read,
		(void *)0,
	},
	{
		i2c_sensor_apds9930, 0x39, 2, 0,
		"apds9930", "visible light", "lx",
		sensor_apds9930_init,
		sensor_apds9930_read,
		(void *)0,
	},
	{
		i2c_sensor_apds9960, 0x39, 2, 0,
		"apds9960", "visible light", "lx",
		sensor_apds9960_init,
		sensor_apds9960_read,
		(void *)0,
	},
	{
		i2c_sensor_si114x_ultraviolet, 0x60, 1, 0,
		"si114x", "ultraviolet light index", "",
		sensor_si114x_ultraviolet_init,
		sensor_si114x_ultraviolet_read,
		(void *)0,
	},
	{
		i2c_sensor_si114x_visible_light, 0x60, 1, 1,
		"si114x", "visible light", "lx",
		(void *)0,
		sensor_si114x_visible_light_read,
		(void *)0,
	},
	{
		i2c_sensor_si114x_infrared, 0x60, 0, 1,
		"si114x", "infrared light", "%",
		(void *)0,
		sensor_si114x_infrared_read,
		(void *)0,
	},
	{
		i2c_sensor_si114x_temperature, 0x60, 1, 1,
		"si114x", "temperature", "C",
		(void *)0,
		sensor_si114x_temperature_read,
		(void *)0,
	},
	{
		i2c_sensor_max44009, 0x4a, 2, 0,
		"max44009", "visible light", "lx",
		sensor_max44009_init,
		sensor_max44009_read,
		(void *)0,
	},
	{
		i2c_sensor_veml6070_38, 0x38, 1, 0,
		"veml6070", "ultraviolet light index #0", "",
		sensor_veml6070_38_init,
		sensor_veml6070_38_read,
		(void *)0,
	},
	{
		i2c_sensor_veml6070_39, 0x39, 1, 0,
		"veml6070", "ultraviolet light index #1", "",
		sensor_veml6070_39_init,
		sensor_veml6070_39_read,
		(void *)0,
	},
	{
		i2c_sensor_tsl2561_39, 0x39, 2, 0,
		"tsl2561 #0", "visible light", "lx",
		sensor_tsl2561_init,
		sensor_tsl2561_read,
		(void *)0,
	},
	{
		i2c_sensor_tsl2561_29, 0x29, 2, 0,
		"tsl2561 #1", "visible light", "lx",
		sensor_tsl2561_init,
		sensor_tsl2561_read,
		(void *)0,
	},
	{
		i2c_sensor_tsl2550, 0x39, 2, 0,
		"tsl2550", "visible light", "lx",
		sensor_tsl2550_init,
		sensor_tsl2550_read,
		(void *)0,
	},
	{
		i2c_sensor_bh1750, 0x23, 2, 0,
		"bh1750", "visible light", "lx",
		sensor_bh1750_init,
		sensor_bh1750_read,
		(void *)0,
	},
	{
		i2c_sensor_tmp75_48, 0x48, 2, 0,
		"tmp75 #0", "temperature", "C",
		sensor_tmp75_init,
		sensor_tmp75_read,
		(void *)0,
	},
	{
		i2c_sensor_tmp75_49, 0x49, 2, 0,
		"tmp75 #1", "temperature", "C",
		sensor_tmp75_init,
		sensor_tmp75_read,
		(void *)0,
	},
	{
		i2c_sensor_tmp75_4a, 0x4a, 2, 0,
		"tmp75 #2", "temperature", "C",
		sensor_tmp75_init,
		sensor_tmp75_read,
		(void *)0,
	},
	{
		i2c_sensor_tmp75_4f, 0x4f, 2, 0,
		"tmp75 #3", "temperature", "C",
		sensor_tmp75_init,
		sensor_tmp75_read,
		(void *)0,
	},
	{
		i2c_sensor_ds7505_48, 0x48, 2, 0,
		"ds7505 #0", "temperature", "C",
		sensor_ds7505_init,
		sensor_ds7505_read,
		(void *)0,
	},
	{
		i2c_sensor_ds7505_49, 0x49, 2, 0,
		"ds7505 #1", "temperature", "C",
		sensor_ds7505_init,
		sensor_ds7505_read,
		(void *)0,
	},
	{
		i2c_sensor_ds7505_4a, 0x4a, 2, 0,
		"ds7505 #2", "temperature", "C",
		sensor_ds7505_init,
		sensor_ds7505_read,
		(void *)0,
	},
	{
		i2c_sensor_ds7505_4f, 0x4f, 2, 0,
		"ds7505 #3", "temperature", "C",
		sensor_ds7505_init,
		sensor_ds7505_read,
		(void *)0,
	},
	{
		i2c_sensor_ds1631_48, 0x48, 2, 0,
		"ds1631 #0", "temperature", "C",
		sensor_ds1631_init,
		sensor_ds1631_read,
		(void *)0,
	},
	{
		i2c_sensor_ds1631_49, 0x49, 2, 0,
		"ds1631 #1", "temperature", "C",
		sensor_ds1631_init,
		sensor_ds1631_read,
		(void *)0,
	},
	{
		i2c_sensor_ds1631_4a, 0x4a, 2, 0,
		"ds1631 #2", "temperature", "C",
		sensor_ds1631_init,
		sensor_ds1631_read,
		(void *)0,
	},
	{
		i2c_sensor_ds1631_4f, 0x4f, 2, 0,
		"ds1631 #3", "temperature", "C",
		sensor_ds1631_init,
		sensor_ds1631_read,
		(void *)0,
	},
	{
		i2c_sensor_ds1621_48, 0x48, 1, 0,
		"ds1621 #0", "temperature", "C",
		sensor_ds1621_init,
		sensor_ds1621_read,
		(void *)0,
	},
	{
		i2c_sensor_ds1621_49, 0x49, 1, 0,
		"ds1621 #1", "temperature", "C",
		sensor_ds1621_init,
		sensor_ds1621_read,
		(void *)0,
	},
	{
		i2c_sensor_ds1621_4a, 0x4a, 1, 0,
		"ds1621 #2", "temperature", "C",
		sensor_ds1621_init,
		sensor_ds1621_read,
		(void *)0,
	},
	{
		i2c_sensor_ds1621_4f, 0x4f, 1, 0,
		"ds1621 #3", "temperature", "C",
		sensor_ds1621_init,
		sensor_ds1621_read,
		(void *)0,
	},
	{
		i2c_sensor_tmp102_48, 0x48, 2, 0,
		"tmp102 #0", "temperature", "C",
		sensor_tmp102_init,
		sensor_tmp102_read,
		(void *)0,
	},
	{
		i2c_sensor_tmp102_49, 0x49, 2, 0,
		"tmp102 #1", "temperature", "C",
		sensor_tmp102_init,
		sensor_tmp102_read,
		(void *)0,
	},
	{
		i2c_sensor_lm75_48, 0x48, 2, 0,
		"lm75 #0", "temperature", "C",
		sensor_lm75_init,
		sensor_lm75_read,
		(void *)0,
	},
	{
		i2c_sensor_lm75_49, 0x49, 2, 0,
		"lm75 #1", "temperature", "C",
		sensor_lm75_init,
		sensor_lm75_read,
		(void *)0,
	},
	{
		i2c_sensor_lm75_4a, 0x4a, 2, 0,
		"lm75 #2", "temperature", "C",
		sensor_lm75_init,
		sensor_lm75_read,
		(void *)0,
	},
	{
		i2c_sensor_lm75_4f, 0x4f, 2, 0,
		"lm75 #3", "temperature", "C",
		sensor_lm75_init,
		sensor_lm75_read,
		(void *)0,
	},
	{
		i2c_sensor_mpl3115a2_temperature, 0x60, 2, 0,
		"mpl3115a2", "temperature", "C",
		sensor_mpl3115a2_temperature_init,
		sensor_mpl3115a2_temperature_read,
		(void *)0,
	},
	{
		i2c_sensor_mpl3115a2_airpressure, 0x60, 2, 1,
		"mpl3115a2", "air pressure", "hPa",
		(void *)0,
		sensor_mpl3115a2_airpressure_read,
		(void *)0,
	},
	{
		i2c_sensor_ccs811, 0x5a, 0, 0,
		"ccs811", "air quality", "%",
		sensor_ccs811_init,
		sensor_ccs811_read,
		(void *)0,
	},
	{
		i2c_sensor_sht30_humidity, 0x44, 0, 0,
		"sht30", "humidity", "",
		sensor_sht30_humidity_init,
		sensor_sht30_humidity_read,
		(void *)0,
	},
	{
		i2c_sensor_sht30_temperature, 0x44, 2, 1,
		"sht30", "temperature", "C",
		(void *)0,
		sensor_sht30_temperature_read,
		(void *)0,
	},
	{
		i2c_sensor_mcp9808_temperature, 0x18, 2, 0,
		"mcp9808", "temperature", "C",
		sensor_mcp9808_init,
		sensor_mcp9808_read,
		(void *)0,
	},
	{
		i2c_sensor_hdc1080_humidity, 0x40, 0, 0,
		"hdc1080", "humidity", "",
		sensor_hdc1080_humidity_init,
		sensor_hdc1080_humidity_read,
		(void *)0,
	},
	{
		i2c_sensor_hdc1080_temperature, 0x40, 2, 1,
		"hdc1080", "temperature", "C",
		(void *)0,
		sensor_hdc1080_temperature_read,
		(void *)0,
	},
	{
		i2c_sensor_htu21_temperature, 0x40, 2, 0,
		"htu21", "temperature", "C",
		sensor_htu21_init,
		sensor_htu21_temperature_read,
		sensor_htu21_periodic,
	},
	{
		i2c_sensor_htu21_humidity, 0x40, 2, 1,
		"htu21", "humidity", "%",
		(void *)0,
		sensor_htu21_humidity_read,
		(void *)0,
	},
	{
		i2c_sensor_bme680_temperature, 0x76, 2, 0,
		"bme680", "temperature", "C",
		sensor_bme680_init,
		sensor_bme680_temperature_read,
		sensor_bme680_periodic,
	},
	{
		i2c_sensor_bme680_humidity, 0x76, 0, 1,
		"bme680", "humidity", "%",
		(void *)0,
		sensor_bme680_humidity_read,
		(void *)0,
	},
	{
		i2c_sensor_bme680_airpressure, 0x76, 2, 1,
		"bme680", "air pressure", "hPa",
		(void *)0,
		sensor_bme680_airpressure_read,
		(void *)0,
	},
	{
		i2c_sensor_bme280_temperature, 0x76, 2, 0,
		"bme280", "temperature", "C",
		sensor_bme280_init,
		sensor_bme280_read_temperature,
		sensor_bme280_periodic,
	},
	{
		i2c_sensor_bme280_humidity, 0x76, 1, 1,
		"bme280", "humidity", "%",
		(void *)0,
		sensor_bme280_read_humidity,
		(void *)0,
	},
	{
		i2c_sensor_bme280_airpressure, 0x76, 2, 1,
		"bme280", "air pressure", "hPa",
		(void *)0,
		sensor_bme280_read_airpressure,
		(void *)0,
	},
	{
		i2c_sensor_bmp280_temperature, 0x76, 2, 0,
		"bmp280", "temperature", "C",
		sensor_bmp280_init,
		sensor_bmp280_read_temperature,
		sensor_bmp280_periodic,
	},
	{
		i2c_sensor_bmp280_airpressure, 0x76, 2, 1,
		"bmp280", "air pressure", "hPa",
		(void *)0,
		sensor_bmp280_read_airpressure,
		(void *)0,
	},
	{
		i2c_sensor_bmp085_temperature, 0x77, 2, 0,
		"bmp085/bmp180", "temperature", "C",
		sensor_bmp085_init,
		sensor_bmp085_read_temperature,
		sensor_bmp085_periodic,
	},
	{
		i2c_sensor_bmp085_airpressure, 0x77, 2, 1,
		"bmp085/bmp180", "air pressure", "hPa",
		(void *)0,
		sensor_bmp085_read_airpressure,
		(void *)0,
	},
	{
		i2c_sensor_am2320_humidity, 0x5c, 0, 0,
		"am2320/1/2", "humidity", "%",
		sensor_am2320_humidity_init,
		sensor_am2320_humidity_read,
		(void *)0,
	},
	{
		i2c_sensor_am2320_temperature, 0x5c, 2, 1,
		"am2320/1/2", "temperature", "C",
		(void *)0,
		sensor_am2320_temperature_read,
		(void *)0,
	},
	{
		i2c_sensor_hih6130_humidity, 0x27, 0, 0,
		"hih6130", "humidity", "",
		sensor_hih6130_humidity_init,
		sensor_hih6130_humidity_read,
		(void *)0,
	},
	{
		i2c_sensor_hih6130_temperature, 0x27, 2, 1,
		"hih6130", "temperature", "C",
		(void *)0,
		sensor_hih6130_temperature_read,
		(void *)0,
	},
	{
		i2c_sensor_digipicco_humidity, 0x78, 0, 0,
		"digipicco", "humidity", "%",
		sensor_digipicco_humidity_init,
		sensor_digipicco_humidity_read,
		(void *)0,
	},
	{
		i2c_sensor_digipicco_temperature, 0x78, 2, 1,
		"digipicco", "temperature", "C",
		(void *)0,
		sensor_digipicco_temperature_read,
		(void *)0,
	},
};

static i2c_sensor_info_t sensor_info =
{
	.init_started = 0,
	.init_finished = 0,
};

void i2c_sensor_get_info(i2c_sensor_info_t *sensor_info_ptr)
{
	*sensor_info_ptr = sensor_info;
}

i2c_error_t i2c_sensor_init(int bus, i2c_sensor_t sensor)
{
	const i2c_sensor_device_table_entry_t *device_table_entry, *device_table_check_entry;
	unsigned int device_table_check_index;
	i2c_error_t error;

	if(sensor >= i2c_sensor_size)
		return(i2c_error_out_of_range);

	device_table_entry = &device_table[sensor];

	if(device_table_entry->id != sensor)
	{
		log("*** i2c_sensor_init: sensor id != index: %u, %u\n", device_table_entry->id, sensor);
		return(i2c_error_out_of_range);
	}

	if(device_table_entry->flags & sdte_secondary)
		return(i2c_error_init_secondary);

	sensor_deregister(bus, sensor);

	if(!device_table_entry->init_fn)
		return(i2c_error_disabled);

	for(device_table_check_index = 0, device_table_check_entry = device_table; device_table_check_index < i2c_sensor_size; device_table_check_index++, device_table_check_entry++)
	{
		if(device_table_check_entry->init_fn && !(device_table_check_entry->flags & sdte_secondary) &&
				(device_table_check_entry->address == device_table_entry->address))
		{
			if(i2c_sensor_registered(0, device_table_check_entry->id))
				return(i2c_error_in_use_on_bus_0);

			if(i2c_sensor_registered(bus, device_table_check_entry->id))
				return(i2c_error_in_use);
		}
	}

	if((error = i2c_select_bus(bus)) != i2c_error_ok)
	{
		i2c_select_bus(0);
		return(error);
	}

	if((error = device_table_entry->init_fn(bus, device_table_entry, &device_data[sensor])) != i2c_error_ok)
	{
		i2c_select_bus(0);
		return(error);
	}

	i2c_select_bus(0);
	return(i2c_error_ok);
}

bool i2c_sensors_init(void)
{
	i2c_info_t i2c_info;
	unsigned int buses;

	sensor_info.init_called++;

	if(!sensor_info.init_started)
	{
		sensor_info.init_started_us = time_get_us();
		sensor_info.init_started = 1;
	}

	if(sensor_info.init_finished)
		return(false);

	switch(i2c_sensor_init(sensor_info.init_current_bus, sensor_info.init_current_sensor))
	{
		case(i2c_error_ok):
		{
			sensor_info.init_succeeded++;
			break;
		}

		case(i2c_error_in_use_on_bus_0):
		{
			sensor_info.init_skip_found_on_bus_0++;
			break;
		}

		case(i2c_error_disabled):
		{
			sensor_info.init_skip_disabled++;
			break;
		}

		case(i2c_error_init_secondary):
		{
			sensor_info.init_skip_secondary++;
			break;
		}

		case(i2c_error_in_use):
		{
			sensor_info.init_skip_duplicate_address++;
			break;
		}

		default:
		{
			sensor_info.init_failed++;
			break;
		}
	}

	i2c_get_info(&i2c_info);
	buses = i2c_info.buses;

	if(++sensor_info.init_current_sensor >= i2c_sensor_size)
	{
		sensor_info.init_current_sensor = 0;
		sensor_info.init_current_bus++;

		if(sensor_info.init_current_bus >= buses)
		{
			sensor_info.init_current_sensor = 0;
			sensor_info.init_current_bus = 0;
			sensor_info.init_finished = 1;
			sensor_info.init_finished_us = time_get_us();
			return(false);
		}
	}

	return(true);
}

void i2c_sensors_periodic(void)
{
	i2c_info_t i2c_info;
	unsigned int buses;
	const i2c_sensor_device_table_entry_t *device_table_entry;

	sensor_info.periodic_called++;

	if(!sensor_info.init_finished)
		return;

	device_table_entry = &device_table[sensor_info.periodic_current_sensor];

	if(device_table_entry->id != sensor_info.periodic_current_sensor)
		return;

	if(i2c_sensor_registered(sensor_info.periodic_current_bus, sensor_info.periodic_current_sensor) && !(device_table_entry->flags & sdte_secondary) && device_table_entry->periodic_fn)
	{
		if(i2c_select_bus(sensor_info.periodic_current_bus) != i2c_error_ok)
		{
			sensor_info.periodic_current_sensor = 0;
			sensor_info.periodic_current_bus = 0;
		}
		else
		{
			sensor_info.periodic_sensor_called++;
			device_table_entry->periodic_fn(device_table_entry, &device_data[sensor_info.periodic_current_sensor]);
		}

		i2c_select_bus(0);
	}

	i2c_get_info(&i2c_info);
	buses = i2c_info.buses;

	if(++sensor_info.periodic_current_sensor >= i2c_sensor_size)
	{
		sensor_info.periodic_current_sensor = 0;
		sensor_info.periodic_current_bus++;

		if(sensor_info.periodic_current_bus >= buses)
		{
			sensor_info.periodic_current_sensor = 0;
			sensor_info.periodic_current_bus = 0;
			sensor_info.periodic_wrapped++;
		}
	}
}

bool i2c_sensor_read(string_t *dst, int bus, i2c_sensor_t sensor, bool verbose, bool html)
{
	const i2c_sensor_device_table_entry_t *entry;
	i2c_error_t error;
	i2c_sensor_value_t value;
	int current;
	int int_factor, int_offset;
	double extracooked;

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
		string_format(dst, "i2c sensor read: select bus #%d error", bus);
		i2c_error_format_string(dst, error);
		i2c_select_bus(0);
		return(false);
	}

	error = i2c_error_ok;

	if(html)
		string_format(dst, "%d</td><td align=\"right\">%u</td><td align=\"right\">0x%02x</td><td>%s</td><td>%s</td>", bus, sensor, entry->address, entry->name, entry->type);
	else
		string_format(dst, "%s sensor %u/%02u@%02x: %s, %s: ", device_data[sensor].registered ? "+" : " ", (unsigned int)bus, sensor, entry->address, entry->name, entry->type);

	if((error = entry->read_fn(bus, entry, &value, &device_data[current])) == i2c_error_ok)
	{
		if(!config_get_int("i2s.%u.%u.factor", &int_factor, bus, sensor))
			int_factor = 1000;

		if(!config_get_int("i2s.%u.%u.offset", &int_offset, bus, sensor))
			int_offset = 0;

		extracooked = (value.cooked * int_factor / 1000.0) + (int_offset / 1000.0);

		if(html)
			string_format(dst, "<td align=\"right\">%.*f %s", (int)entry->precision, extracooked, entry->unity);
		else
			string_format(dst, "[%.*f] %s", (int)entry->precision, extracooked, entry->unity);

		if(verbose)
			string_format(dst, " (uncalibrated: %.*f, raw: %.*f)", (int)entry->precision, value.cooked, (int)entry->precision, value.raw);
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
		if(!config_get_int("i2s.%u.%u.factor", &int_factor, bus, sensor))
			int_factor = 1000;

		if(!config_get_int("i2s.%u.%u.offset", &int_offset, bus, sensor))
			int_offset = 0;

		string_format(dst, ", calibration: factor = %4f, offset = %4f", int_factor / 1000.0, int_offset / 1000.0);
	}

	i2c_select_bus(0);
	return(true);
}
