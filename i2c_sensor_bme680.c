/*********************************************
 * MOST OF THIS CODE IS COPYRIGHTED BY BOSCH *
 * SEE COPYRIGHT NOTICE AT END OF BOSCH CODE *
 *********************************************/

#include "attribute.h"

//#include "BME680_driver/bme680.h"
//#include "BME680_driver/bme680_defs.h"

#include <stdint.h>
#include <stddef.h>

#if !defined(UINT8_C) && !defined(INT8_C)
#define INT8_C(x)       S8_C(x)
#define UINT8_C(x)      U8_C(x)
#endif

#if !defined(UINT16_C) && !defined(INT16_C)
#define INT16_C(x)      S16_C(x)
#define UINT16_C(x)     U16_C(x)
#endif

#if !defined(INT32_C) && !defined(UINT32_C)
#define INT32_C(x)      S32_C(x)
#define UINT32_C(x)     U32_C(x)
#endif

#if !defined(INT64_C) && !defined(UINT64_C)
#define INT64_C(x)      S64_C(x)
#define UINT64_C(x)     U64_C(x)
#endif

#ifndef NULL
#ifdef __cplusplus
#define NULL   0
#else
#define NULL   ((void *) 0)
#endif
#endif

/** BME680 General config */
#define BME680_POLL_PERIOD_MS		UINT8_C(10)

/** BME680 I2C addresses */
#define BME680_I2C_ADDR_PRIMARY		UINT8_C(0x76)
#define BME680_I2C_ADDR_SECONDARY	UINT8_C(0x77)

/** BME680 unique chip identifier */
#define BME680_CHIP_ID  UINT8_C(0x61)

/** BME680 coefficients related defines */
#define BME680_COEFF_SIZE		UINT8_C(41)
#define BME680_COEFF_ADDR1_LEN		UINT8_C(25)
#define BME680_COEFF_ADDR2_LEN		UINT8_C(16)

/** BME680 field_x related defines */
#define BME680_FIELD_LENGTH		UINT8_C(15)
#define BME680_FIELD_ADDR_OFFSET	UINT8_C(17)

/** Soft reset command */
#define BME680_SOFT_RESET_CMD   UINT8_C(0xb6)

/** Error code definitions */
#define BME680_OK		INT8_C(0)
/* Errors */
#define BME680_E_NULL_PTR		    INT8_C(-1)
#define BME680_E_COM_FAIL		    INT8_C(-2)
#define BME680_E_DEV_NOT_FOUND		INT8_C(-3)
#define BME680_E_INVALID_LENGTH		INT8_C(-4)

/* Warnings */
#define BME680_W_DEFINE_PWR_MODE	INT8_C(1)
#define BME680_W_NO_NEW_DATA        INT8_C(2)

/* Info's */
#define BME680_I_MIN_CORRECTION		UINT8_C(1)
#define BME680_I_MAX_CORRECTION		UINT8_C(2)

/** Register map */
/** Other coefficient's address */
#define BME680_ADDR_RES_HEAT_VAL_ADDR	UINT8_C(0x00)
#define BME680_ADDR_RES_HEAT_RANGE_ADDR	UINT8_C(0x02)
#define BME680_ADDR_RANGE_SW_ERR_ADDR	UINT8_C(0x04)
#define BME680_ADDR_SENS_CONF_START	UINT8_C(0x5A)
#define BME680_ADDR_GAS_CONF_START	UINT8_C(0x64)

/** Field settings */
#define BME680_FIELD0_ADDR		UINT8_C(0x1d)

/** Heater settings */
#define BME680_RES_HEAT0_ADDR		UINT8_C(0x5a)
#define BME680_GAS_WAIT0_ADDR		UINT8_C(0x64)

/** Sensor configuration registers */
#define BME680_CONF_HEAT_CTRL_ADDR		UINT8_C(0x70)
#define BME680_CONF_ODR_RUN_GAS_NBC_ADDR	UINT8_C(0x71)
#define BME680_CONF_OS_H_ADDR			UINT8_C(0x72)
#define BME680_MEM_PAGE_ADDR			UINT8_C(0xf3)
#define BME680_CONF_T_P_MODE_ADDR		UINT8_C(0x74)
#define BME680_CONF_ODR_FILT_ADDR		UINT8_C(0x75)

/** Coefficient's address */
#define BME680_COEFF_ADDR1	UINT8_C(0x89)
#define BME680_COEFF_ADDR2	UINT8_C(0xe1)

/** Chip identifier */
#define BME680_CHIP_ID_ADDR	UINT8_C(0xd0)

/** Soft reset register */
#define BME680_SOFT_RESET_ADDR		UINT8_C(0xe0)

/** Heater control settings */
#define BME680_ENABLE_HEATER		UINT8_C(0x00)
#define BME680_DISABLE_HEATER		UINT8_C(0x08)

/** Gas measurement settings */
#define BME680_DISABLE_GAS_MEAS		UINT8_C(0x00)
#define BME680_ENABLE_GAS_MEAS		UINT8_C(0x01)

/** Over-sampling settings */
#define BME680_OS_NONE		UINT8_C(0)
#define BME680_OS_1X		UINT8_C(1)
#define BME680_OS_2X		UINT8_C(2)
#define BME680_OS_4X		UINT8_C(3)
#define BME680_OS_8X		UINT8_C(4)
#define BME680_OS_16X		UINT8_C(5)

/** IIR filter settings */
#define BME680_FILTER_SIZE_0	UINT8_C(0)
#define BME680_FILTER_SIZE_1	UINT8_C(1)
#define BME680_FILTER_SIZE_3	UINT8_C(2)
#define BME680_FILTER_SIZE_7	UINT8_C(3)
#define BME680_FILTER_SIZE_15	UINT8_C(4)
#define BME680_FILTER_SIZE_31	UINT8_C(5)
#define BME680_FILTER_SIZE_63	UINT8_C(6)
#define BME680_FILTER_SIZE_127	UINT8_C(7)

/** Power mode settings */
#define BME680_SLEEP_MODE	UINT8_C(0)
#define BME680_FORCED_MODE	UINT8_C(1)

/** Delay related macro declaration */
#define BME680_RESET_PERIOD	UINT32_C(10)

/** SPI memory page settings */
#define BME680_MEM_PAGE0	UINT8_C(0x10)
#define BME680_MEM_PAGE1	UINT8_C(0x00)

/** Ambient humidity shift value for compensation */
#define BME680_HUM_REG_SHIFT_VAL	UINT8_C(4)

/** Run gas enable and disable settings */
#define BME680_RUN_GAS_DISABLE	UINT8_C(0)
#define BME680_RUN_GAS_ENABLE	UINT8_C(1)

/** Buffer length macro declaration */
#define BME680_TMP_BUFFER_LENGTH	UINT8_C(40)
#define BME680_REG_BUFFER_LENGTH	UINT8_C(6)
#define BME680_FIELD_DATA_LENGTH	UINT8_C(3)
#define BME680_GAS_REG_BUF_LENGTH	UINT8_C(20)

/** Settings selector */
#define BME680_OST_SEL			UINT16_C(1)
#define BME680_OSP_SEL			UINT16_C(2)
#define BME680_OSH_SEL			UINT16_C(4)
#define BME680_GAS_MEAS_SEL		UINT16_C(8)
#define BME680_FILTER_SEL		UINT16_C(16)
#define BME680_HCNTRL_SEL		UINT16_C(32)
#define BME680_RUN_GAS_SEL		UINT16_C(64)
#define BME680_NBCONV_SEL		UINT16_C(128)
#define BME680_GAS_SENSOR_SEL		(BME680_GAS_MEAS_SEL | BME680_RUN_GAS_SEL | BME680_NBCONV_SEL)

/** Number of conversion settings*/
#define BME680_NBCONV_MIN		UINT8_C(0)
#define BME680_NBCONV_MAX		UINT8_C(10)

/** Mask definitions */
#define BME680_GAS_MEAS_MSK	UINT8_C(0x30)
#define BME680_NBCONV_MSK	UINT8_C(0X0F)
#define BME680_FILTER_MSK	UINT8_C(0X1C)
#define BME680_OST_MSK		UINT8_C(0XE0)
#define BME680_OSP_MSK		UINT8_C(0X1C)
#define BME680_OSH_MSK		UINT8_C(0X07)
#define BME680_HCTRL_MSK	UINT8_C(0x08)
#define BME680_RUN_GAS_MSK	UINT8_C(0x10)
#define BME680_MODE_MSK		UINT8_C(0x03)
#define BME680_RHRANGE_MSK	UINT8_C(0x30)
#define BME680_RSERROR_MSK	UINT8_C(0xf0)
#define BME680_NEW_DATA_MSK	UINT8_C(0x80)
#define BME680_GAS_INDEX_MSK	UINT8_C(0x0f)
#define BME680_GAS_RANGE_MSK	UINT8_C(0x0f)
#define BME680_GASM_VALID_MSK	UINT8_C(0x20)
#define BME680_HEAT_STAB_MSK	UINT8_C(0x10)
#define BME680_MEM_PAGE_MSK	UINT8_C(0x10)
#define BME680_SPI_RD_MSK	UINT8_C(0x80)
#define BME680_SPI_WR_MSK	UINT8_C(0x7f)
#define	BME680_BIT_H1_DATA_MSK	UINT8_C(0x0F)

/** Bit position definitions for sensor settings */
#define BME680_GAS_MEAS_POS	UINT8_C(4)
#define BME680_FILTER_POS	UINT8_C(2)
#define BME680_OST_POS		UINT8_C(5)
#define BME680_OSP_POS		UINT8_C(2)
#define BME680_RUN_GAS_POS	UINT8_C(4)

/** Array Index to Field data mapping for Calibration Data*/
#define BME680_T2_LSB_REG	(1)
#define BME680_T2_MSB_REG	(2)
#define BME680_T3_REG		(3)
#define BME680_P1_LSB_REG	(5)
#define BME680_P1_MSB_REG	(6)
#define BME680_P2_LSB_REG	(7)
#define BME680_P2_MSB_REG	(8)
#define BME680_P3_REG		(9)
#define BME680_P4_LSB_REG	(11)
#define BME680_P4_MSB_REG	(12)
#define BME680_P5_LSB_REG	(13)
#define BME680_P5_MSB_REG	(14)
#define BME680_P7_REG		(15)
#define BME680_P6_REG		(16)
#define BME680_P8_LSB_REG	(19)
#define BME680_P8_MSB_REG	(20)
#define BME680_P9_LSB_REG	(21)
#define BME680_P9_MSB_REG	(22)
#define BME680_P10_REG		(23)
#define BME680_H2_MSB_REG	(25)
#define BME680_H2_LSB_REG	(26)
#define BME680_H1_LSB_REG	(26)
#define BME680_H1_MSB_REG	(27)
#define BME680_H3_REG		(28)
#define BME680_H4_REG		(29)
#define BME680_H5_REG		(30)
#define BME680_H6_REG		(31)
#define BME680_H7_REG		(32)
#define BME680_T1_LSB_REG	(33)
#define BME680_T1_MSB_REG	(34)
#define BME680_GH2_LSB_REG	(35)
#define BME680_GH2_MSB_REG	(36)
#define BME680_GH1_REG		(37)
#define BME680_GH3_REG		(38)

/** BME680 register buffer index settings*/
#define BME680_REG_FILTER_INDEX		UINT8_C(5)
#define BME680_REG_TEMP_INDEX		UINT8_C(4)
#define BME680_REG_PRES_INDEX		UINT8_C(4)
#define BME680_REG_HUM_INDEX		UINT8_C(2)
#define BME680_REG_NBCONV_INDEX		UINT8_C(1)
#define BME680_REG_RUN_GAS_INDEX	UINT8_C(1)
#define BME680_REG_HCTRL_INDEX		UINT8_C(0)

/** BME680 pressure calculation macros */
/*! This max value is used to provide precedence to multiplication or division
 * in pressure compensation equation to achieve least loss of precision and
 * avoiding overflows.
 * i.e Comparing value, BME680_MAX_OVERFLOW_VAL = INT32_C(1 << 30)
 */
#define BME680_MAX_OVERFLOW_VAL      INT32_C(0x40000000)

/** Macro to combine two 8 bit data's to form a 16 bit data */
#define BME680_CONCAT_BYTES(msb, lsb)	(((uint8_t)msb << 8) | (uint8_t)lsb)

/** Macro to SET and GET BITS of a register */
#define BME680_SET_BITS(reg_data, bitname, data) \
		((reg_data & ~(bitname##_MSK)) | \
		((data << bitname##_POS) & bitname##_MSK))
#define BME680_GET_BITS(reg_data, bitname)	((reg_data & (bitname##_MSK)) >> \
	(bitname##_POS))

/** Macro variant to handle the bitname position if it is zero */
#define BME680_SET_BITS_POS_0(reg_data, bitname, data) \
				((reg_data & ~(bitname##_MSK)) | \
				(data & bitname##_MSK))
#define BME680_GET_BITS_POS_0(reg_data, bitname)  (reg_data & (bitname##_MSK))

/** Type definitions */
/*!
 * Generic communication function pointer
 * @param[in] dev_id: Place holder to store the id of the device structure
 *                    Can be used to store the index of the Chip select or
 *                    I2C address of the device.
 * @param[in] reg_addr:	Used to select the register the where data needs to
 *                      be read from or written to.
 * @param[in/out] reg_data: Data array to read/write
 * @param[in] len: Length of the data array
 */
typedef int (*bme680_com_fptr_t)(unsigned int dev_id, unsigned int reg_addr, uint8_t *data, unsigned int len);

/*!
 * Delay function pointer
 * @param[in] period: Time period in milliseconds
 */
typedef void (*bme680_delay_fptr_t)(unsigned int period);

/*!
 * @brief Interface selection Enumerations
 */
enum bme680_intf {
	/*! SPI interface */
	BME680_SPI_INTF,
	/*! I2C interface */
	BME680_I2C_INTF
};

/* structure definitions */
/*!
 * @brief Sensor field data structure
 */
struct	bme680_field_data {
	/*! Contains new_data, gasm_valid & heat_stab */
	unsigned int status;
	/*! The index of the heater profile used */
	unsigned int gas_index;
	/*! Measurement index to track order */
	unsigned int meas_index;

	/*! Temperature in degree celsius */
	double temperature;
	/*! Pressure in Pascal */
	double pressure;
	/*! Humidity in % relative humidity x1000 */
	double humidity;
	/*! Gas resistance in Ohms */
	double gas_resistance;
};

/*!
 * @brief Structure to hold the Calibration data
 */
struct	bme680_calib_data {
	/*! Variable to store calibrated humidity data */
	unsigned int par_h1;
	/*! Variable to store calibrated humidity data */
	unsigned int par_h2;
	/*! Variable to store calibrated humidity data */
	int par_h3;
	/*! Variable to store calibrated humidity data */
	int par_h4;
	/*! Variable to store calibrated humidity data */
	int par_h5;
	/*! Variable to store calibrated humidity data */
	unsigned int par_h6;
	/*! Variable to store calibrated humidity data */
	int par_h7;
	/*! Variable to store calibrated gas data */
	int par_gh1;
	/*! Variable to store calibrated gas data */
	int par_gh2;
	/*! Variable to store calibrated gas data */
	int par_gh3;
	/*! Variable to store calibrated temperature data */
	unsigned int par_t1;
	/*! Variable to store calibrated temperature data */
	int par_t2;
	/*! Variable to store calibrated temperature data */
	int par_t3;
	/*! Variable to store calibrated pressure data */
	unsigned int par_p1;
	/*! Variable to store calibrated pressure data */
	int par_p2;
	/*! Variable to store calibrated pressure data */
	int par_p3;
	/*! Variable to store calibrated pressure data */
	int par_p4;
	/*! Variable to store calibrated pressure data */
	int par_p5;
	/*! Variable to store calibrated pressure data */
	int par_p6;
	/*! Variable to store calibrated pressure data */
	int par_p7;
	/*! Variable to store calibrated pressure data */
	int par_p8;
	/*! Variable to store calibrated pressure data */
	int par_p9;
	/*! Variable to store calibrated pressure data */
	unsigned int par_p10;
	/*! Variable to store t_fine size */
	double t_fine;
	/*! Variable to store heater resistance range */
	unsigned int res_heat_range;
	/*! Variable to store heater resistance value */
	int res_heat_val;
	/*! Variable to store error range */
	int range_sw_err;
};

/*!
 * @brief BME680 sensor settings structure which comprises of ODR,
 * over-sampling and filter settings.
 */
struct	bme680_tph_sett {
	/*! Humidity oversampling */
	unsigned int os_hum;
	/*! Temperature oversampling */
	unsigned int os_temp;
	/*! Pressure oversampling */
	unsigned int os_pres;
	/*! Filter coefficient */
	unsigned int filter;
};

/*!
 * @brief BME680 gas sensor which comprises of gas settings
 *  and status parameters
 */
struct	bme680_gas_sett {
	/*! Variable to store nb conversion */
	unsigned int nb_conv;
	/*! Variable to store heater control */
	unsigned int heatr_ctrl;
	/*! Run gas enable value */
	unsigned int run_gas;
	/*! Heater temperature value */
	unsigned int heatr_temp;
	/*! Duration profile value */
	unsigned int heatr_dur;
};

/*!
 * @brief BME680 device structure
 */
struct	bme680_dev {
	/*! Chip Id */
	uint8_t chip_id;
	/*! Device Id */
	uint8_t dev_id;
	/*! SPI/I2C interface */
	enum bme680_intf intf;
	/*! Memory page used */
	unsigned int mem_page;
	/*! Ambient temperature in Degree C */
	double amb_temp;
	/*! Sensor calibration data */
	struct bme680_calib_data calib;
	/*! Sensor settings */
	struct bme680_tph_sett tph_sett;
	/*! Gas Sensor settings */
	struct bme680_gas_sett gas_sett;
	/*! Sensor power modes */
	unsigned int power_mode;
	/*! New sensor fields */
	unsigned int new_fields;
	/*! Store the info messages */
	unsigned int info_msg;
	/*! Bus read function pointer */
	bme680_com_fptr_t read;
	/*! Bus write function pointer */
	bme680_com_fptr_t write;
	/*! delay function pointer */
	bme680_delay_fptr_t delay_ms;
	/*! Communication function result */
	int com_rslt;
};

/* function prototype declarations */
/*!
 *  @brief This API is the entry point.
 *  It reads the chip-id and calibration data from the sensor.
 *
 *  @param[in,out] dev : Structure instance of bme680_dev
 *
 *  @return Result of API execution status
 *  @retval zero -> Success / +ve value -> Warning / -ve value -> Error
 */
static int bme680_init(struct bme680_dev *dev);

/*!
 * @brief This API writes the given data to the register address
 * of the sensor.
 *
 * @param[in] reg_addr : Register address from where the data to be written.
 * @param[in] reg_data : Pointer to data buffer which is to be written
 * in the sensor.
 * @param[in] len : No of bytes of data to write..
 * @param[in] dev : Structure instance of bme680_dev.
 *
 * @return Result of API execution status
 * @retval zero -> Success / +ve value -> Warning / -ve value -> Error
 */
static int bme680_set_regs(const unsigned int *reg_addr, const uint8_t *reg_data, unsigned int len, struct bme680_dev *dev);

/*!
 * @brief This API reads the data from the given register address of the sensor.
 *
 * @param[in] reg_addr : Register address from where the data to be read
 * @param[out] reg_data : Pointer to data buffer to store the read data.
 * @param[in] len : No of bytes of data to be read.
 * @param[in] dev : Structure instance of bme680_dev.
 *
 * @return Result of API execution status
 * @retval zero -> Success / +ve value -> Warning / -ve value -> Error
 */
static int bme680_get_regs(unsigned int reg_addr, uint8_t *reg_data, unsigned int len, struct bme680_dev *dev);

/*!
 * @brief This API performs the soft reset of the sensor.
 *
 * @param[in] dev : Structure instance of bme680_dev.
 *
 * @return Result of API execution status
 * @retval zero -> Success / +ve value -> Warning / -ve value -> Error.
 */
static int bme680_soft_reset(struct bme680_dev *dev);

/*!
 * @brief This API is used to set the power mode of the sensor.
 *
 * @param[in] dev : Structure instance of bme680_dev
 * @note : Pass the value to bme680_dev.power_mode structure variable.
 *
 *  value	|	mode
 * -------------|------------------
 *	0x00	|	BME680_SLEEP_MODE
 *	0x01	|	BME680_FORCED_MODE
 *
 * * @return Result of API execution status
 * @retval zero -> Success / +ve value -> Warning / -ve value -> Error
 */
static int bme680_set_sensor_mode(struct bme680_dev *dev);

/*!
 * @brief This API is used to get the power mode of the sensor.
 *
 * @param[in] dev : Structure instance of bme680_dev
 * @note : bme680_dev.power_mode structure variable hold the power mode.
 *
 *  value	|	mode
 * ---------|------------------
 *	0x00	|	BME680_SLEEP_MODE
 *	0x01	|	BME680_FORCED_MODE
 *
 * @return Result of API execution status
 * @retval zero -> Success / +ve value -> Warning / -ve value -> Error
 */
#if 0 // never used
static int bme680_get_sensor_mode(struct bme680_dev *dev);
#endif

/*!
 * @brief This API is used to set the profile duration of the sensor.
 *
 * @param[in] dev	   : Structure instance of bme680_dev.
 * @param[in] duration : Duration of the measurement in ms.
 *
 * @return Nothing
 */
#if 0 // never used
static void bme680_set_profile_dur(unsigned int duration, struct bme680_dev *dev);
#endif

/*!
 * @brief This API is used to get the profile duration of the sensor.
 *
 * @param[in] dev	   : Structure instance of bme680_dev.
 * @param[in] duration : Duration of the measurement in ms.
 *
 * @return Nothing
 */
static void bme680_get_profile_dur(unsigned int *duration, const struct bme680_dev *dev);

/*!
 * @brief This API reads the pressure, temperature and humidity and gas data
 * from the sensor, compensates the data and store it in the bme680_data
 * structure instance passed by the user.
 *
 * @param[out] data: Structure instance to hold the data.
 * @param[in] dev : Structure instance of bme680_dev.
 *
 * @return Result of API execution status
 * @retval zero -> Success / +ve value -> Warning / -ve value -> Error
 */
static int bme680_get_sensor_data(struct bme680_field_data *data, struct bme680_dev *dev);

/*!
 * @brief This API is used to set the oversampling, filter and T,P,H, gas selection
 * settings in the sensor.
 *
 * @param[in] dev : Structure instance of bme680_dev.
 * @param[in] desired_settings : Variable used to select the settings which
 * are to be set in the sensor.
 *
 *	 Macros	                   |  Functionality
 *---------------------------------|----------------------------------------------
 *	BME680_OST_SEL             |    To set temperature oversampling.
 *	BME680_OSP_SEL             |    To set pressure oversampling.
 *	BME680_OSH_SEL             |    To set humidity oversampling.
 *	BME680_GAS_MEAS_SEL        |    To set gas measurement setting.
 *	BME680_FILTER_SEL          |    To set filter setting.
 *	BME680_HCNTRL_SEL          |    To set humidity control setting.
 *	BME680_RUN_GAS_SEL         |    To set run gas setting.
 *	BME680_NBCONV_SEL          |    To set NB conversion setting.
 *	BME680_GAS_SENSOR_SEL      |    To set all gas sensor related settings
 *
 * @note : Below are the macros to be used by the user for selecting the
 * desired settings. User can do OR operation of these macros for configuring
 * multiple settings.
 *
 * @return Result of API execution status
 * @retval zero -> Success / +ve value -> Warning / -ve value -> Error.
 */
static int bme680_set_sensor_settings(unsigned int desired_settings, struct bme680_dev *dev);

/*!
 * @brief This API is used to get the oversampling, filter and T,P,H, gas selection
 * settings in the sensor.
 *
 * @param[in] dev : Structure instance of bme680_dev.
 * @param[in] desired_settings : Variable used to select the settings which
 * are to be get from the sensor.
 *
 * @return Result of API execution status
 * @retval zero -> Success / +ve value -> Warning / -ve value -> Error.
 */
#if 0 // never used
static int bme680_get_sensor_settings(unsigned int desired_settings, struct bme680_dev *dev);
#endif

//#include "BME680_driver/bme680.c"

/*!
 * @brief This internal API is used to read the calibrated data from the sensor.
 *
 * This function is used to retrieve the calibration
 * data from the image registers of the sensor.
 *
 * @note Registers 89h  to A1h for calibration data 1 to 24
 *        from bit 0 to 7
 * @note Registers E1h to F0h for calibration data 25 to 40
 *        from bit 0 to 7
 * @param[in] dev	:Structure instance of bme680_dev.
 *
 * @return Result of API execution status.
 * @retval zero -> Success / +ve value -> Warning / -ve value -> Error
 */
static int get_calib_data(struct bme680_dev *dev);

/*!
 * @brief This internal API is used to set the gas configuration of the sensor.
 *
 * @param[in] dev	:Structure instance of bme680_dev.
 *
 * @return Result of API execution status.
 * @retval zero -> Success / +ve value -> Warning / -ve value -> Error
 */
static int set_gas_config(struct bme680_dev *dev);

/*!
 * @brief This internal API is used to get the gas configuration of the sensor.
 * @note heatr_temp and heatr_dur values are currently register data
 * and not the actual values set
 *
 * @param[in] dev	:Structure instance of bme680_dev.
 *
 * @return Result of API execution status.
 * @retval zero -> Success / +ve value -> Warning / -ve value -> Error
 */
#if 0 // never used
static int get_gas_config(struct bme680_dev *dev);
#endif

/*!
 * @brief This internal API is used to calculate the Heat duration value.
 *
 * @param[in] dur	:Value of the duration to be shared.
 *
 * @return uint8_t threshold duration after calculation.
 */
static unsigned int calc_heater_dur(unsigned int dur);

/*!
 * @brief This internal API is used to calculate the
 * temperature value value in float format
 *
 * @param[in] dev	:Structure instance of bme680_dev.
 * @param[in] temp_adc	:Contains the temperature ADC value .
 *
 * @return Calculated temperature in float
 */
static double calc_temperature(unsigned int temp_adc, struct bme680_dev *dev);

/*!
 * @brief This internal API is used to calculate the
 * pressure value value in float format
 *
 * @param[in] dev	:Structure instance of bme680_dev.
 * @param[in] pres_adc	:Contains the pressure ADC value .
 *
 * @return Calculated pressure in float.
 */
static double calc_pressure(unsigned int pres_adc, const struct bme680_dev *dev);

/*!
 * @brief This internal API is used to calculate the
 * humidity value value in float format
 *
 * @param[in] dev	:Structure instance of bme680_dev.
 * @param[in] hum_adc	:Contains the humidity ADC value.
 *
 * @return Calculated humidity in float.
 */
static double calc_humidity(unsigned int hum_adc, const struct bme680_dev *dev);

/*!
 * @brief This internal API is used to calculate the
 * gas resistance value value in float format
 *
 * @param[in] dev		:Structure instance of bme680_dev.
 * @param[in] gas_res_adc	:Contains the Gas Resistance ADC value.
 * @param[in] gas_range		:Contains the range of gas values.
 *
 * @return Calculated gas resistance in float.
 */
static double calc_gas_resistance(unsigned int gas_res_adc, unsigned int gas_range, const struct bme680_dev *dev);

/*!
 * @brief This internal API is used to calculate the
 * heater resistance value in float format
 *
 * @param[in] temp	: Contains the target temperature value.
 * @param[in] dev	: Structure instance of bme680_dev.
 *
 * @return Calculated heater resistance in float.
 */
static double calc_heater_res(unsigned int temp, const struct bme680_dev *dev);

/*!
 * @brief This internal API is used to calculate the field data of sensor.
 *
 * @param[out] data :Structure instance to hold the data
 * @param[in] dev	:Structure instance of bme680_dev.
 *
 *  @return int8_t result of the field data from sensor.
 */
static int read_field_data(struct bme680_field_data *data, struct bme680_dev *dev);

/*!
 * @brief This internal API is used to set the memory page
 * based on register address.
 *
 * The value of memory page
 *  value  | Description
 * --------|--------------
 *   0     | BME680_PAGE0_SPI
 *   1     | BME680_PAGE1_SPI
 *
 * @param[in] dev	:Structure instance of bme680_dev.
 * @param[in] reg_addr	:Contains the register address array.
 *
 * @return Result of API execution status
 * @retval zero -> Success / +ve value -> Warning / -ve value -> Error
 */
static int set_mem_page(unsigned int reg_addr, struct bme680_dev *dev);

/*!
 * @brief This internal API is used to get the memory page based
 * on register address.
 *
 * The value of memory page
 *  value  | Description
 * --------|--------------
 *   0     | BME680_PAGE0_SPI
 *   1     | BME680_PAGE1_SPI
 *
 * @param[in] dev	:Structure instance of bme680_dev.
 *
 * @return Result of API execution status
 * @retval zero -> Success / +ve value -> Warning / -ve value -> Error
 */
static int get_mem_page(struct bme680_dev *dev);

/*!
 * @brief This internal API is used to validate the device pointer for
 * null conditions.
 *
 * @param[in] dev	:Structure instance of bme680_dev.
 *
 * @return Result of API execution status
 * @retval zero -> Success / +ve value -> Warning / -ve value -> Error
 */
static int null_ptr_check(const struct bme680_dev *dev);

/*!
 * @brief This internal API is used to check the boundary
 * conditions.
 *
 * @param[in] value	:pointer to the value.
 * @param[in] min	:minimum value.
 * @param[in] max	:maximum value.
 * @param[in] dev	:Structure instance of bme680_dev.
 *
 * @return Result of API execution status
 * @retval zero -> Success / +ve value -> Warning / -ve value -> Error
 */
static int boundary_check(unsigned int *value, unsigned int min, unsigned int max, struct bme680_dev *dev);

/****************** Global Function Definitions *******************************/
/*!
 *@brief This API is the entry point.
 *It reads the chip-id and calibration data from the sensor.
 */
irom static int bme680_init(struct bme680_dev *dev)
{
	int rslt;

	/* Check for null pointer in the device structure*/
	rslt = null_ptr_check(dev);
	if (rslt == BME680_OK) {
		/* Soft reset to restore it to default values*/
		rslt = bme680_soft_reset(dev);
		if (rslt == BME680_OK) {
			rslt = bme680_get_regs(BME680_CHIP_ID_ADDR, &dev->chip_id, 1, dev);
			if (rslt == BME680_OK) {
				if (dev->chip_id == BME680_CHIP_ID) {
					/* Get the Calibration data */
					rslt = get_calib_data(dev);
				} else {
					rslt = BME680_E_DEV_NOT_FOUND;
				}
			}
		}
	}

	return rslt;
}

/*!
 * @brief This API reads the data from the given register address of the sensor.
 */
irom static int bme680_get_regs(unsigned int reg_addr, uint8_t *reg_data, unsigned int len, struct bme680_dev *dev)
{
	int rslt;

	/* Check for null pointer in the device structure*/
	rslt = null_ptr_check(dev);
	if (rslt == BME680_OK) {
		if (dev->intf == BME680_SPI_INTF) {
			/* Set the memory page */
			rslt = set_mem_page(reg_addr, dev);
			if (rslt == BME680_OK)
				reg_addr = reg_addr | BME680_SPI_RD_MSK;
		}
		dev->com_rslt = dev->read(dev->dev_id, reg_addr, reg_data, len);
		if (dev->com_rslt != 0)
			rslt = BME680_E_COM_FAIL;
	}

	return rslt;
}

/*!
 * @brief This API writes the given data to the register address
 * of the sensor.
 */
irom static int bme680_set_regs(const unsigned int *reg_addr, const uint8_t *reg_data, unsigned int len, struct bme680_dev *dev)
{
	int rslt;
	/* Length of the temporary buffer is 2*(length of register)*/
	uint8_t tmp_buff[BME680_TMP_BUFFER_LENGTH] = { 0 };
	unsigned int index;

	/* Check for null pointer in the device structure*/
	rslt = null_ptr_check(dev);
	if (rslt == BME680_OK) {
		if ((len > 0) && (len < BME680_TMP_BUFFER_LENGTH / 2)) {
			/* Interleave the 2 arrays */
			for (index = 0; index < len; index++) {
				if (dev->intf == BME680_SPI_INTF) {
					/* Set the memory page */
					rslt = set_mem_page(reg_addr[index], dev);
					tmp_buff[(2 * index)] = reg_addr[index] & BME680_SPI_WR_MSK;
				} else {
					tmp_buff[(2 * index)] = reg_addr[index];
				}
				tmp_buff[(2 * index) + 1] = reg_data[index];
			}
			/* Write the interleaved array */
			if (rslt == BME680_OK) {
				dev->com_rslt = dev->write(dev->dev_id, tmp_buff[0], &tmp_buff[1], (2 * len) - 1);
				if (dev->com_rslt != 0)
					rslt = BME680_E_COM_FAIL;
			}
		} else {
			rslt = BME680_E_INVALID_LENGTH;
		}
	}

	return rslt;
}

/*!
 * @brief This API performs the soft reset of the sensor.
 */
irom static int bme680_soft_reset(struct bme680_dev *dev)
{
	int rslt;
	unsigned int reg_addr = BME680_SOFT_RESET_ADDR;
	/* 0xb6 is the soft reset command */
	uint8_t soft_rst_cmd = BME680_SOFT_RESET_CMD;

	/* Check for null pointer in the device structure*/
	rslt = null_ptr_check(dev);
	if (rslt == BME680_OK) {
		if (dev->intf == BME680_SPI_INTF)
			rslt = get_mem_page(dev);

		/* Reset the device */
		if (rslt == BME680_OK) {
			rslt = bme680_set_regs(&reg_addr, &soft_rst_cmd, 1, dev);
			/* Wait for 5ms */
			dev->delay_ms(BME680_RESET_PERIOD);

			if (rslt == BME680_OK) {
				/* After reset get the memory page */
				if (dev->intf == BME680_SPI_INTF)
					rslt = get_mem_page(dev);
			}
		}
	}

	return rslt;
}

/*!
 * @brief This API is used to set the oversampling, filter and T,P,H, gas selection
 * settings in the sensor.
 */
irom static int bme680_set_sensor_settings(unsigned int desired_settings, struct bme680_dev *dev)
{
	int rslt;
	unsigned int reg_addr;
	uint8_t data = 0;
	unsigned int count = 0;
	unsigned int reg_array[BME680_REG_BUFFER_LENGTH] = { 0 };
	uint8_t data_array[BME680_REG_BUFFER_LENGTH] = { 0 };
	unsigned int intended_power_mode = dev->power_mode; /* Save intended power mode */

	/* Check for null pointer in the device structure*/
	rslt = null_ptr_check(dev);
	if (rslt == BME680_OK) {
		if (desired_settings & BME680_GAS_MEAS_SEL)
			rslt = set_gas_config(dev);

		dev->power_mode = BME680_SLEEP_MODE;
		if (rslt == BME680_OK)
			rslt = bme680_set_sensor_mode(dev);

		/* Selecting the filter */
		if (desired_settings & BME680_FILTER_SEL) {
			rslt = boundary_check(&dev->tph_sett.filter, BME680_FILTER_SIZE_0, BME680_FILTER_SIZE_127, dev);
			reg_addr = BME680_CONF_ODR_FILT_ADDR;

			if (rslt == BME680_OK)
				rslt = bme680_get_regs(reg_addr, &data, 1, dev);

			if (desired_settings & BME680_FILTER_SEL)
				data = BME680_SET_BITS(data, BME680_FILTER, dev->tph_sett.filter);

			reg_array[count] = reg_addr; /* Append configuration */
			data_array[count] = data;
			count++;
		}

		/* Selecting heater control for the sensor */
		if (desired_settings & BME680_HCNTRL_SEL) {
			rslt = boundary_check(&dev->gas_sett.heatr_ctrl, BME680_ENABLE_HEATER,
				BME680_DISABLE_HEATER, dev);
			reg_addr = BME680_CONF_HEAT_CTRL_ADDR;

			if (rslt == BME680_OK)
				rslt = bme680_get_regs(reg_addr, &data, 1, dev);
			data = BME680_SET_BITS_POS_0(data, BME680_HCTRL, dev->gas_sett.heatr_ctrl);

			reg_array[count] = reg_addr; /* Append configuration */
			data_array[count] = data;
			count++;
		}

		/* Selecting heater T,P oversampling for the sensor */
		if (desired_settings & (BME680_OST_SEL | BME680_OSP_SEL)) {
			rslt = boundary_check(&dev->tph_sett.os_temp, BME680_OS_NONE, BME680_OS_16X, dev);
			reg_addr = BME680_CONF_T_P_MODE_ADDR;

			if (rslt == BME680_OK)
				rslt = bme680_get_regs(reg_addr, &data, 1, dev);

			if (desired_settings & BME680_OST_SEL)
				data = BME680_SET_BITS(data, BME680_OST, dev->tph_sett.os_temp);

			if (desired_settings & BME680_OSP_SEL)
				data = BME680_SET_BITS(data, BME680_OSP, dev->tph_sett.os_pres);

			reg_array[count] = reg_addr;
			data_array[count] = data;
			count++;
		}

		/* Selecting humidity oversampling for the sensor */
		if (desired_settings & BME680_OSH_SEL) {
			rslt = boundary_check(&dev->tph_sett.os_hum, BME680_OS_NONE, BME680_OS_16X, dev);
			reg_addr = BME680_CONF_OS_H_ADDR;

			if (rslt == BME680_OK)
				rslt = bme680_get_regs(reg_addr, &data, 1, dev);
			data = BME680_SET_BITS_POS_0(data, BME680_OSH, dev->tph_sett.os_hum);

			reg_array[count] = reg_addr; /* Append configuration */
			data_array[count] = data;
			count++;
		}

		/* Selecting the runGas and NB conversion settings for the sensor */
		if (desired_settings & (BME680_RUN_GAS_SEL | BME680_NBCONV_SEL)) {
			rslt = boundary_check(&dev->gas_sett.run_gas, BME680_RUN_GAS_DISABLE,
				BME680_RUN_GAS_ENABLE, dev);
			if (rslt == BME680_OK) {
				/* Validate boundary conditions */
				rslt = boundary_check(&dev->gas_sett.nb_conv, BME680_NBCONV_MIN,
					BME680_NBCONV_MAX, dev);
			}

			reg_addr = BME680_CONF_ODR_RUN_GAS_NBC_ADDR;

			if (rslt == BME680_OK)
				rslt = bme680_get_regs(reg_addr, &data, 1, dev);

			if (desired_settings & BME680_RUN_GAS_SEL)
				data = BME680_SET_BITS(data, BME680_RUN_GAS, dev->gas_sett.run_gas);

			if (desired_settings & BME680_NBCONV_SEL)
				data = BME680_SET_BITS_POS_0(data, BME680_NBCONV, dev->gas_sett.nb_conv);

			reg_array[count] = reg_addr; /* Append configuration */
			data_array[count] = data;
			count++;
		}

		if (rslt == BME680_OK)
			rslt = bme680_set_regs(reg_array, data_array, count, dev);

		/* Restore previous intended power mode */
		dev->power_mode = intended_power_mode;
	}

	return rslt;
}

/*!
 * @brief This API is used to get the oversampling, filter and T,P,H, gas selection
 * settings in the sensor.
 */
#if 0 // never used
irom static int bme680_get_sensor_settings(unsigned int desired_settings, struct bme680_dev *dev)
{
	int rslt;
	/* starting address of the register array for burst read*/
	unsigned int reg_addr = BME680_CONF_HEAT_CTRL_ADDR;
	unsigned int data_array[BME680_REG_BUFFER_LENGTH] = { 0 };

	/* Check for null pointer in the device structure*/
	rslt = null_ptr_check(dev);
	if (rslt == BME680_OK) {
		rslt = bme680_get_regs(reg_addr, data_array, BME680_REG_BUFFER_LENGTH, dev);

		if (rslt == BME680_OK) {
			if (desired_settings & BME680_GAS_MEAS_SEL)
				rslt = get_gas_config(dev);

			/* get the T,P,H ,Filter,ODR settings here */
			if (desired_settings & BME680_FILTER_SEL)
				dev->tph_sett.filter = BME680_GET_BITS(data_array[BME680_REG_FILTER_INDEX],
					BME680_FILTER);

			if (desired_settings & (BME680_OST_SEL | BME680_OSP_SEL)) {
				dev->tph_sett.os_temp = BME680_GET_BITS(data_array[BME680_REG_TEMP_INDEX], BME680_OST);
				dev->tph_sett.os_pres = BME680_GET_BITS(data_array[BME680_REG_PRES_INDEX], BME680_OSP);
			}

			if (desired_settings & BME680_OSH_SEL)
				dev->tph_sett.os_hum = BME680_GET_BITS_POS_0(data_array[BME680_REG_HUM_INDEX],
					BME680_OSH);

			/* get the gas related settings */
			if (desired_settings & BME680_HCNTRL_SEL)
				dev->gas_sett.heatr_ctrl = BME680_GET_BITS_POS_0(data_array[BME680_REG_HCTRL_INDEX],
					BME680_HCTRL);

			if (desired_settings & (BME680_RUN_GAS_SEL | BME680_NBCONV_SEL)) {
				dev->gas_sett.nb_conv = BME680_GET_BITS_POS_0(data_array[BME680_REG_NBCONV_INDEX],
					BME680_NBCONV);
				dev->gas_sett.run_gas = BME680_GET_BITS(data_array[BME680_REG_RUN_GAS_INDEX],
					BME680_RUN_GAS);
			}
		}
	} else {
		rslt = BME680_E_NULL_PTR;
	}

	return rslt;
}
#endif

/*!
 * @brief This API is used to set the power mode of the sensor.
 */
irom static int bme680_set_sensor_mode(struct bme680_dev *dev)
{
	int rslt;
	uint8_t tmp_pow_mode;
	unsigned int pow_mode = 0;
	unsigned int reg_addr = BME680_CONF_T_P_MODE_ADDR;

	/* Check for null pointer in the device structure*/
	rslt = null_ptr_check(dev);
	if (rslt == BME680_OK) {
		/* Call repeatedly until in sleep */
		do {
			rslt = bme680_get_regs(BME680_CONF_T_P_MODE_ADDR, &tmp_pow_mode, 1, dev);
			if (rslt == BME680_OK) {
				/* Put to sleep before changing mode */
				pow_mode = (tmp_pow_mode & BME680_MODE_MSK);

				if (pow_mode != BME680_SLEEP_MODE) {
					tmp_pow_mode = tmp_pow_mode & (~BME680_MODE_MSK); /* Set to sleep */
					rslt = bme680_set_regs(&reg_addr, &tmp_pow_mode, 1, dev);
					dev->delay_ms(BME680_POLL_PERIOD_MS);
				}
			}
		} while (pow_mode != BME680_SLEEP_MODE);

		/* Already in sleep */
		if (dev->power_mode != BME680_SLEEP_MODE) {
			tmp_pow_mode = (tmp_pow_mode & ~BME680_MODE_MSK) | (dev->power_mode & BME680_MODE_MSK);
			if (rslt == BME680_OK)
				rslt = bme680_set_regs(&reg_addr, &tmp_pow_mode, 1, dev);
		}
	}

	return rslt;
}

/*!
 * @brief This API is used to get the power mode of the sensor.
 */
#if 0 // unused
irom static int bme680_get_sensor_mode(struct bme680_dev *dev)
{
	int rslt;
	unsigned int mode;

	/* Check for null pointer in the device structure*/
	rslt = null_ptr_check(dev);
	if (rslt == BME680_OK) {
		rslt = bme680_get_regs(BME680_CONF_T_P_MODE_ADDR, &mode, 1, dev);
		/* Masking the other register bit info*/
		dev->power_mode = mode & BME680_MODE_MSK;
	}

	return rslt;
}
#endif

/*!
 * @brief This API is used to set the profile duration of the sensor.
 */
#if 0 // never used
irom static void bme680_set_profile_dur(unsigned int duration, struct bme680_dev *dev)
{
	unsigned int tph_dur; /* Calculate in us */
	unsigned int meas_cycles;
	unsigned int os_to_meas_cycles[6] = {0, 1, 2, 4, 8, 16};

	meas_cycles = os_to_meas_cycles[dev->tph_sett.os_temp];
	meas_cycles += os_to_meas_cycles[dev->tph_sett.os_pres];
	meas_cycles += os_to_meas_cycles[dev->tph_sett.os_hum];

	/* TPH measurement duration */
	tph_dur = meas_cycles * UINT32_C(1963);
	tph_dur += UINT32_C(477 * 4); /* TPH switching duration */
	tph_dur += UINT32_C(477 * 5); /* Gas measurement duration */
	tph_dur += UINT32_C(500); /* Get it to the closest whole number.*/
	tph_dur /= UINT32_C(1000); /* Convert to ms */

	tph_dur += UINT32_C(1); /* Wake up duration of 1ms */
	/* The remaining time should be used for heating */
	dev->gas_sett.heatr_dur = duration - (uint16_t) tph_dur;
}
#endif

/*!
 * @brief This API is used to get the profile duration of the sensor.
 */
irom static void bme680_get_profile_dur(unsigned int *duration, const struct bme680_dev *dev)
{
	unsigned int tph_dur; /* Calculate in us */
	unsigned int meas_cycles;
	unsigned int os_to_meas_cycles[6] = {0, 1, 2, 4, 8, 16};

	meas_cycles = os_to_meas_cycles[dev->tph_sett.os_temp];
	meas_cycles += os_to_meas_cycles[dev->tph_sett.os_pres];
	meas_cycles += os_to_meas_cycles[dev->tph_sett.os_hum];

	/* TPH measurement duration */
	tph_dur = meas_cycles * UINT32_C(1963);
	tph_dur += UINT32_C(477 * 4); /* TPH switching duration */
	tph_dur += UINT32_C(477 * 5); /* Gas measurement duration */
	tph_dur += UINT32_C(500); /* Get it to the closest whole number.*/
	tph_dur /= UINT32_C(1000); /* Convert to ms */

	tph_dur += UINT32_C(1); /* Wake up duration of 1ms */

	*duration = (uint16_t) tph_dur;

	/* Get the gas duration only when the run gas is enabled */
	if (dev->gas_sett.run_gas) {
		/* The remaining time should be used for heating */
		*duration += dev->gas_sett.heatr_dur;
	}
}

/*!
 * @brief This API reads the pressure, temperature and humidity and gas data
 * from the sensor, compensates the data and store it in the bme680_data
 * structure instance passed by the user.
 */
irom static int bme680_get_sensor_data(struct bme680_field_data *data, struct bme680_dev *dev)
{
	int rslt;

	/* Check for null pointer in the device structure*/
	rslt = null_ptr_check(dev);
	if (rslt == BME680_OK) {
		/* Reading the sensor data in forced mode only */
		rslt = read_field_data(data, dev);
		if (rslt == BME680_OK) {
			if (data->status & BME680_NEW_DATA_MSK)
				dev->new_fields = 1;
			else
				dev->new_fields = 0;
		}
	}

	return rslt;
}

/*!
 * @brief This internal API is used to read the calibrated data from the sensor.
 */
irom static int get_calib_data(struct bme680_dev *dev)
{
	int rslt;
	uint8_t coeff_array[BME680_COEFF_SIZE] = { 0 };
	uint8_t temp_var = 0; /* Temporary variable */

	/* Check for null pointer in the device structure*/
	rslt = null_ptr_check(dev);
	if (rslt == BME680_OK) {
		rslt = bme680_get_regs(BME680_COEFF_ADDR1, coeff_array, BME680_COEFF_ADDR1_LEN, dev);
		/* Append the second half in the same array */
		if (rslt == BME680_OK)
			rslt = bme680_get_regs(BME680_COEFF_ADDR2, &coeff_array[BME680_COEFF_ADDR1_LEN]
			, BME680_COEFF_ADDR2_LEN, dev);

		/* Temperature related coefficients */
		dev->calib.par_t1 = (uint16_t) (BME680_CONCAT_BYTES(coeff_array[BME680_T1_MSB_REG],
			coeff_array[BME680_T1_LSB_REG]));
		dev->calib.par_t2 = (int16_t) (BME680_CONCAT_BYTES(coeff_array[BME680_T2_MSB_REG],
			coeff_array[BME680_T2_LSB_REG]));
		dev->calib.par_t3 = (int8_t) (coeff_array[BME680_T3_REG]);

		/* Pressure related coefficients */
		dev->calib.par_p1 = (uint16_t) (BME680_CONCAT_BYTES(coeff_array[BME680_P1_MSB_REG],
			coeff_array[BME680_P1_LSB_REG]));
		dev->calib.par_p2 = (int16_t) (BME680_CONCAT_BYTES(coeff_array[BME680_P2_MSB_REG],
			coeff_array[BME680_P2_LSB_REG]));
		dev->calib.par_p3 = (int8_t) coeff_array[BME680_P3_REG];
		dev->calib.par_p4 = (int16_t) (BME680_CONCAT_BYTES(coeff_array[BME680_P4_MSB_REG],
			coeff_array[BME680_P4_LSB_REG]));
		dev->calib.par_p5 = (int16_t) (BME680_CONCAT_BYTES(coeff_array[BME680_P5_MSB_REG],
			coeff_array[BME680_P5_LSB_REG]));
		dev->calib.par_p6 = (int8_t) (coeff_array[BME680_P6_REG]);
		dev->calib.par_p7 = (int8_t) (coeff_array[BME680_P7_REG]);
		dev->calib.par_p8 = (int16_t) (BME680_CONCAT_BYTES(coeff_array[BME680_P8_MSB_REG],
			coeff_array[BME680_P8_LSB_REG]));
		dev->calib.par_p9 = (int16_t) (BME680_CONCAT_BYTES(coeff_array[BME680_P9_MSB_REG],
			coeff_array[BME680_P9_LSB_REG]));
		dev->calib.par_p10 = (uint8_t) (coeff_array[BME680_P10_REG]);

		/* Humidity related coefficients */
		dev->calib.par_h1 = (uint16_t) (((uint16_t) coeff_array[BME680_H1_MSB_REG] << BME680_HUM_REG_SHIFT_VAL)
			| (coeff_array[BME680_H1_LSB_REG] & BME680_BIT_H1_DATA_MSK));
		dev->calib.par_h2 = (uint16_t) (((uint16_t) coeff_array[BME680_H2_MSB_REG] << BME680_HUM_REG_SHIFT_VAL)
			| ((coeff_array[BME680_H2_LSB_REG]) >> BME680_HUM_REG_SHIFT_VAL));
		dev->calib.par_h3 = (int8_t) coeff_array[BME680_H3_REG];
		dev->calib.par_h4 = (int8_t) coeff_array[BME680_H4_REG];
		dev->calib.par_h5 = (int8_t) coeff_array[BME680_H5_REG];
		dev->calib.par_h6 = (uint8_t) coeff_array[BME680_H6_REG];
		dev->calib.par_h7 = (int8_t) coeff_array[BME680_H7_REG];

		/* Gas heater related coefficients */
		dev->calib.par_gh1 = (int8_t) coeff_array[BME680_GH1_REG];
		dev->calib.par_gh2 = (int16_t) (BME680_CONCAT_BYTES(coeff_array[BME680_GH2_MSB_REG],
			coeff_array[BME680_GH2_LSB_REG]));
		dev->calib.par_gh3 = (int8_t) coeff_array[BME680_GH3_REG];

		/* Other coefficients */
		if (rslt == BME680_OK) {
			rslt = bme680_get_regs(BME680_ADDR_RES_HEAT_RANGE_ADDR, &temp_var, 1, dev);

			dev->calib.res_heat_range = ((temp_var & BME680_RHRANGE_MSK) / 16);
			if (rslt == BME680_OK) {
				rslt = bme680_get_regs(BME680_ADDR_RES_HEAT_VAL_ADDR, &temp_var, 1, dev);

				dev->calib.res_heat_val = (int8_t) temp_var;
				if (rslt == BME680_OK)
					rslt = bme680_get_regs(BME680_ADDR_RANGE_SW_ERR_ADDR, &temp_var, 1, dev);
			}
		}
		dev->calib.range_sw_err = ((int8_t) temp_var & (int8_t) BME680_RSERROR_MSK) / 16;
	}

	return rslt;
}

/*!
 * @brief This internal API is used to set the gas configuration of the sensor.
 */
irom static int set_gas_config(struct bme680_dev *dev)
{
	int rslt;

	/* Check for null pointer in the device structure*/
	rslt = null_ptr_check(dev);
	if (rslt == BME680_OK) {

		unsigned int reg_addr[2] = {0};
		uint8_t reg_data[2] = {0};

		if (dev->power_mode == BME680_FORCED_MODE) {
			reg_addr[0] = BME680_RES_HEAT0_ADDR;
			reg_data[0] = calc_heater_res(dev->gas_sett.heatr_temp, dev);
			reg_addr[1] = BME680_GAS_WAIT0_ADDR;
			reg_data[1] = calc_heater_dur(dev->gas_sett.heatr_dur);
			dev->gas_sett.nb_conv = 0;
		} else {
			rslt = BME680_W_DEFINE_PWR_MODE;
		}
		if (rslt == BME680_OK)
			rslt = bme680_set_regs(reg_addr, reg_data, 2, dev);
	}

	return rslt;
}

/*!
 * @brief This internal API is used to get the gas configuration of the sensor.
 * @note heatr_temp and heatr_dur values are currently register data
 * and not the actual values set
 */
#if 0 // never used
irom static int get_gas_config(struct bme680_dev *dev)
{
	int rslt;
	/* starting address of the register array for burst read*/
	unsigned int reg_addr1 = BME680_ADDR_SENS_CONF_START;
	unsigned int reg_addr2 = BME680_ADDR_GAS_CONF_START;
	unsigned int reg_data = 0;

	/* Check for null pointer in the device structure*/
	rslt = null_ptr_check(dev);
	if (rslt == BME680_OK) {
		if (BME680_SPI_INTF == dev->intf) {
			/* Memory page switch the SPI address*/
			rslt = set_mem_page(reg_addr1, dev);
		}

		if (rslt == BME680_OK) {
			rslt = bme680_get_regs(reg_addr1, &reg_data, 1, dev);
			if (rslt == BME680_OK) {
				dev->gas_sett.heatr_temp = reg_data;
				rslt = bme680_get_regs(reg_addr2, &reg_data, 1, dev);
				if (rslt == BME680_OK) {
					/* Heating duration register value */
					dev->gas_sett.heatr_dur = reg_data;
				}
			}
		}
	}

	return rslt;
}
#endif

/*!
 * @brief This internal API is used to calculate the
 * temperature value in float format
 */
irom static double calc_temperature(unsigned int temp_adc, struct bme680_dev *dev)
{
	double var1 = 0;
	double var2 = 0;
	double calc_temp = 0;

	/* calculate var1 data */
	var1  = ((((double)temp_adc / 16384.0f) - ((double)dev->calib.par_t1 / 1024.0f))
			* ((double)dev->calib.par_t2));

	/* calculate var2 data */
	var2  = (((((double)temp_adc / 131072.0f) - ((double)dev->calib.par_t1 / 8192.0f)) *
		(((double)temp_adc / 131072.0f) - ((double)dev->calib.par_t1 / 8192.0f))) *
		((double)dev->calib.par_t3 * 16.0f));

	/* t_fine value*/
	dev->calib.t_fine = (var1 + var2);

	/* compensated temperature data*/
	calc_temp  = ((dev->calib.t_fine) / 5120.0f);

	return calc_temp;
}

/*!
 * @brief This internal API is used to calculate the
 * pressure value in float format
 */
irom static double calc_pressure(unsigned int pres_adc, const struct bme680_dev *dev)
{
	double var1 = 0;
	double var2 = 0;
	double var3 = 0;
	double calc_pres = 0;

	var1 = (((double)dev->calib.t_fine / 2.0f) - 64000.0f);
	var2 = var1 * var1 * (((double)dev->calib.par_p6) / (131072.0f));
	var2 = var2 + (var1 * ((double)dev->calib.par_p5) * 2.0f);
	var2 = (var2 / 4.0f) + (((double)dev->calib.par_p4) * 65536.0f);
	var1 = (((((double)dev->calib.par_p3 * var1 * var1) / 16384.0f)
		+ ((double)dev->calib.par_p2 * var1)) / 524288.0f);
	var1 = ((1.0f + (var1 / 32768.0f)) * ((double)dev->calib.par_p1));
	calc_pres = (1048576.0f - ((double)pres_adc));

	/* Avoid exception caused by division by zero */
	if ((int)var1 != 0) {
		calc_pres = (((calc_pres - (var2 / 4096.0f)) * 6250.0f) / var1);
		var1 = (((double)dev->calib.par_p9) * calc_pres * calc_pres) / 2147483648.0f;
		var2 = calc_pres * (((double)dev->calib.par_p8) / 32768.0f);
		var3 = ((calc_pres / 256.0f) * (calc_pres / 256.0f) * (calc_pres / 256.0f)
			* (dev->calib.par_p10 / 131072.0f));
		calc_pres = (calc_pres + (var1 + var2 + var3 + ((double)dev->calib.par_p7 * 128.0f)) / 16.0f);
	} else {
		calc_pres = 0;
	}

	return calc_pres;
}

/*!
 * @brief This internal API is used to calculate the
 * humidity value in float format
 */
irom static double calc_humidity(unsigned int hum_adc, const struct bme680_dev *dev)
{
	double calc_hum = 0;
	double var1 = 0;
	double var2 = 0;
	double var3 = 0;
	double var4 = 0;
	double temp_comp;

	/* compensated temperature data*/
	temp_comp  = ((dev->calib.t_fine) / 5120.0f);

	var1 = (double)((double)hum_adc) - (((double)dev->calib.par_h1 * 16.0f) + (((double)dev->calib.par_h3 / 2.0f)
		* temp_comp));

	var2 = var1 * ((double)(((double) dev->calib.par_h2 / 262144.0f) * (1.0f + (((double)dev->calib.par_h4 / 16384.0f)
		* temp_comp) + (((double)dev->calib.par_h5 / 1048576.0f) * temp_comp * temp_comp))));

	var3 = (double) dev->calib.par_h6 / 16384.0f;

	var4 = (double) dev->calib.par_h7 / 2097152.0f;

	calc_hum = var2 + ((var3 + (var4 * temp_comp)) * var2 * var2);

	if (calc_hum > 100.0f)
		calc_hum = 100.0f;
	else if (calc_hum < 0.0f)
		calc_hum = 0.0f;

	return calc_hum;
}

/*!
 * @brief This internal API is used to calculate the
 * gas resistance value in float format
 */
irom static double calc_gas_resistance(unsigned int gas_res_adc, unsigned int gas_range, const struct bme680_dev *dev)
{
	double calc_gas_res;
	double var1 = 0;
	double var2 = 0;
	double var3 = 0;

	const double lookup_k1_range[16] = {
	0.0, 0.0, 0.0, 0.0, 0.0, -1.0, 0.0, -0.8,
	0.0, 0.0, -0.2, -0.5, 0.0, -1.0, 0.0, 0.0};
	const double lookup_k2_range[16] = {
	0.0, 0.0, 0.0, 0.0, 0.1, 0.7, 0.0, -0.8,
	-0.1, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};

	var1 = (1340.0f + (5.0f * dev->calib.range_sw_err));
	var2 = (var1) * (1.0f + lookup_k1_range[gas_range]/100.0f);
	var3 = 1.0f + (lookup_k2_range[gas_range]/100.0f);

	calc_gas_res = 1.0f / (double)(var3 * (0.000000125f) * (double)(1 << gas_range) * (((((double)gas_res_adc)
		- 512.0f)/var2) + 1.0f));

	return calc_gas_res;
}

/*!
 * @brief This internal API is used to calculate the
 * heater resistance value in double format
 */
irom static double calc_heater_res(unsigned int temp, const struct bme680_dev *dev)
{
	double var1 = 0;
	double var2 = 0;
	double var3 = 0;
	double var4 = 0;
	double var5 = 0;
	double res_heat = 0;

	if (temp > 400) /* Cap temperature */
		temp = 400;

	var1 = (((double)dev->calib.par_gh1 / (16.0f)) + 49.0f);
	var2 = ((((double)dev->calib.par_gh2 / (32768.0f)) * (0.0005f)) + 0.00235f);
	var3 = ((double)dev->calib.par_gh3 / (1024.0f));
	var4 = (var1 * (1.0f + (var2 * (double)temp)));
	var5 = (var4 + (var3 * (double)dev->amb_temp));
	res_heat = (uint8_t)(3.4f * ((var5 * (4 / (4 + (double)dev->calib.res_heat_range)) *
		(1/(1 + ((double) dev->calib.res_heat_val * 0.002f)))) - 25));

	return res_heat;
}

/*!
 * @brief This internal API is used to calculate the Heat duration value.
 */
irom attr_const static unsigned int calc_heater_dur(unsigned int dur)
{
	unsigned int factor = 0;
	unsigned int durval;

	if (dur >= 0xfc0) {
		durval = 0xff; /* Max duration*/
	} else {
		while (dur > 0x3F) {
			dur = dur / 4;
			factor += 1;
		}
		durval = (unsigned int) (dur + (factor * 64));
	}

	return durval;
}

/*!
 * @brief This internal API is used to calculate the field data of sensor.
 */
irom static int read_field_data(struct bme680_field_data *data, struct bme680_dev *dev)
{
	int rslt;
	uint8_t buff[BME680_FIELD_LENGTH] = { 0 };
	unsigned int gas_range;
	unsigned int adc_temp;
	unsigned int adc_pres;
	unsigned int adc_hum;
	unsigned int adc_gas_res;
	unsigned int tries = 10;

	/* Check for null pointer in the device structure*/
	rslt = null_ptr_check(dev);
	do {
		if (rslt == BME680_OK) {
			rslt = bme680_get_regs(((uint8_t) (BME680_FIELD0_ADDR)), buff, (uint16_t) BME680_FIELD_LENGTH,
				dev);

			data->status = buff[0] & BME680_NEW_DATA_MSK;
			data->gas_index = buff[0] & BME680_GAS_INDEX_MSK;
			data->meas_index = buff[1];

			/* read the raw data from the sensor */
			adc_pres = (uint32_t) (((uint32_t) buff[2] * 4096) | ((uint32_t) buff[3] * 16)
				| ((uint32_t) buff[4] / 16));
			adc_temp = (uint32_t) (((uint32_t) buff[5] * 4096) | ((uint32_t) buff[6] * 16)
				| ((uint32_t) buff[7] / 16));
			adc_hum = (uint16_t) (((uint32_t) buff[8] * 256) | (uint32_t) buff[9]);
			adc_gas_res = (uint16_t) ((uint32_t) buff[13] * 4 | (((uint32_t) buff[14]) / 64));
			gas_range = buff[14] & BME680_GAS_RANGE_MSK;

			data->status |= buff[14] & BME680_GASM_VALID_MSK;
			data->status |= buff[14] & BME680_HEAT_STAB_MSK;

			if (data->status & BME680_NEW_DATA_MSK) {
				data->temperature = calc_temperature(adc_temp, dev);
				data->pressure = calc_pressure(adc_pres, dev);
				data->humidity = calc_humidity(adc_hum, dev);
				data->gas_resistance = calc_gas_resistance(adc_gas_res, gas_range, dev);
				break;
			}
			/* Delay to poll the data */
			dev->delay_ms(BME680_POLL_PERIOD_MS);
		}
		tries--;
	} while (tries);

	if (!tries)
		rslt = BME680_W_NO_NEW_DATA;

	return rslt;
}

/*!
 * @brief This internal API is used to set the memory page based on register address.
 */
irom static int set_mem_page(unsigned int reg_addr, struct bme680_dev *dev)
{
	int rslt;
	uint8_t reg;
	unsigned int mem_page;

	/* Check for null pointers in the device structure*/
	rslt = null_ptr_check(dev);
	if (rslt == BME680_OK) {
		if (reg_addr > 0x7f)
			mem_page = BME680_MEM_PAGE1;
		else
			mem_page = BME680_MEM_PAGE0;

		if (mem_page != dev->mem_page) {
			dev->mem_page = mem_page;

			dev->com_rslt = dev->read(dev->dev_id, BME680_MEM_PAGE_ADDR | BME680_SPI_RD_MSK, &reg, 1);
			if (dev->com_rslt != 0)
				rslt = BME680_E_COM_FAIL;

			if (rslt == BME680_OK) {
				reg = reg & (~BME680_MEM_PAGE_MSK);
				reg = reg | (dev->mem_page & BME680_MEM_PAGE_MSK);

				dev->com_rslt = dev->write(dev->dev_id, BME680_MEM_PAGE_ADDR & BME680_SPI_WR_MSK,
					&reg, 1);
				if (dev->com_rslt != 0)
					rslt = BME680_E_COM_FAIL;
			}
		}
	}

	return rslt;
}

/*!
 * @brief This internal API is used to get the memory page based on register address.
 */
irom static int get_mem_page(struct bme680_dev *dev)
{
	int rslt;
	uint8_t reg;

	/* Check for null pointer in the device structure*/
	rslt = null_ptr_check(dev);
	if (rslt == BME680_OK) {
		dev->com_rslt = dev->read(dev->dev_id, BME680_MEM_PAGE_ADDR | BME680_SPI_RD_MSK, &reg, 1);
		if (dev->com_rslt != 0)
			rslt = BME680_E_COM_FAIL;
		else
			dev->mem_page = reg & BME680_MEM_PAGE_MSK;
	}

	return rslt;
}

/*!
 * @brief This internal API is used to validate the boundary
 * conditions.
 */
irom static int boundary_check(unsigned int *value, unsigned int min, unsigned int max, struct bme680_dev *dev)
{
	int rslt = BME680_OK;

	if (value != NULL) {
		/* Check if value is below minimum value */
		if (*value < min) {
			/* Auto correct the invalid value to minimum value */
			*value = min;
			dev->info_msg |= BME680_I_MIN_CORRECTION;
		}
		/* Check if value is above maximum value */
		if (*value > max) {
			/* Auto correct the invalid value to maximum value */
			*value = max;
			dev->info_msg |= BME680_I_MAX_CORRECTION;
		}
	} else {
		rslt = BME680_E_NULL_PTR;
	}

	return rslt;
}

/*!
 * @brief This internal API is used to validate the device structure pointer for
 * null conditions.
 */
irom static int null_ptr_check(const struct bme680_dev *dev)
{
	int rslt;

	if ((dev == NULL) || (dev->read == NULL) || (dev->write == NULL) || (dev->delay_ms == NULL)) {
		/* Device structure pointer is not valid */
		rslt = BME680_E_NULL_PTR;
	} else {
		/* Device structure is fine */
		rslt = BME680_OK;
	}

	return rslt;
}

/*
 * Copyright (C) 2017 - 2018 Bosch Sensortec GmbH
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * Neither the name of the copyright holder nor the names of the
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 * CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL COPYRIGHT HOLDER
 * OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES(INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE
 *
 * The information provided is believed to be accurate and reliable.
 * The copyright holder assumes no responsibility
 * for the consequences of use
 * of such information nor for any infringement of patents or
 * other rights of third parties which may result from its use.
 * No license is granted by implication or otherwise under any patent or
 * patent rights of the copyright holder.
 */

/************************************
 * BOSCH COPYRIGHTED CODE ENDS HERE *
 ************************************/

#include "util.h"
#include "i2c.h"
#include "i2c_sensor.h"
#include "i2c_sensor_bme680.h"

static struct bme680_dev bme680;

irom static void user_delay_ms(unsigned int period)
{
	//log("bme680: sleep %u ms\n", period);

	msleep(period);
}

irom static int user_i2c_read(unsigned int dev_id, unsigned int reg_addr, uint8_t *reg_data, unsigned int len)
{
	if(i2c_send1(dev_id, reg_addr) != i2c_error_ok)
		return(-1);

	if(i2c_receive(dev_id, len, reg_data) != i2c_error_ok)
		return(-1);

	return(0);
}

irom static int user_i2c_write(unsigned int dev_id, unsigned int reg_addr, uint8_t *reg_data, unsigned int len)
{
	uint8_t i2c_buffer[32];

	if(len > sizeof(i2c_buffer))
		return(-1);

	memcpy(&i2c_buffer[1], reg_data, len);
	i2c_buffer[0] = reg_addr;

	if(i2c_send(dev_id, len + 1, i2c_buffer) != i2c_error_ok)
		return(-1);

	return(0);
}

static _Bool values_valid = false;
static i2c_sensor_value_t temperature;
static i2c_sensor_value_t humidity;
static i2c_sensor_value_t airpressure;
static i2c_sensor_value_t airquality;

irom i2c_error_t sensor_bme680_all_init(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_device_data_t *data)
{
	unsigned int set_required_settings;

	bme680.dev_id = entry->address;
	bme680.intf = BME680_I2C_INTF;
	bme680.read = user_i2c_read;
	bme680.write = user_i2c_write;
	bme680.delay_ms = user_delay_ms;
	/* amb_temp can be set to 25 prior to configuring the gas sensor 
	* or by performing a few temperature readings without operating the gas sensor.
	*/
	bme680.amb_temp = 22;

	if(bme680_init(&bme680))
		return(i2c_error_device_error_1);

	/* Set the temperature, pressure and humidity settings */
	bme680.tph_sett.os_hum = BME680_OS_2X;
	bme680.tph_sett.os_pres = BME680_OS_4X;
	bme680.tph_sett.os_temp = BME680_OS_8X;
	bme680.tph_sett.filter = BME680_FILTER_SIZE_3;

	/* Set the remaining gas sensor settings and link the heating profile */
	bme680.gas_sett.run_gas = BME680_ENABLE_GAS_MEAS;
	/* Create a ramp heat waveform in 3 steps */
	bme680.gas_sett.heatr_temp = 320; /* degree Celsius */
	bme680.gas_sett.heatr_dur = 150; /* milliseconds */

	/* Select the power mode */
	/* Must be set before writing the sensor configuration */
	bme680.power_mode = BME680_FORCED_MODE; 

	/* Set the required sensor settings needed */
	set_required_settings = BME680_OST_SEL | BME680_OSP_SEL | BME680_OSH_SEL | BME680_FILTER_SEL | BME680_GAS_SENSOR_SEL;

	/* Set the desired sensor configuration */
	if(bme680_set_sensor_settings(set_required_settings, &bme680))
		return(i2c_error_device_error_2);

	return(i2c_error_ok);
}

irom static i2c_error_t sensor_bme680_read(void)
{
	/* Get the total measurement duration so as to sleep or wait till the
	* measurement is complete */

	unsigned int meas_period;
	bme680_get_profile_dur(&meas_period, &bme680);
	struct bme680_field_data data;

	/* Set the power mode */
	if(bme680_set_sensor_mode(&bme680))
		return(i2c_error_device_error_3);

	user_delay_ms(meas_period); /* Delay till the measurement is ready */

	if(bme680_get_sensor_data(&data, &bme680))
		log("bme680: error\n");
	else
	{
		temperature.raw = data.temperature;
		temperature.cooked = temperature.raw;
		humidity.raw = data.humidity;
		humidity.cooked = humidity.raw;
		airpressure.raw = data.pressure;
		airpressure.cooked = airpressure.raw / 100.0;
		airquality.raw = data.gas_resistance;

		if((data.status & BME680_GASM_VALID_MSK) && (data.status & BME680_HEAT_STAB_MSK))
		{
			airquality.cooked = airquality.raw * 100 / 200000;

			if(airquality.cooked < 0)
				airquality.cooked = 0;

			if(airquality.cooked > 100)
				airquality.cooked = 100;
		}
		else
			airquality.cooked = 0;
	}

	values_valid = true;
	return(i2c_error_ok);
}

irom i2c_error_t sensor_bme680_temperature_read(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_value_t *value, i2c_sensor_device_data_t *data)
{
	i2c_error_t error;

	if((error = sensor_bme680_read()) != i2c_error_ok)
		return(error);

	if(!values_valid)
		return(i2c_error_device_error_4);

	*value = temperature;

	return(i2c_error_ok);
}

irom attr_const i2c_error_t sensor_bme680_humidity_read(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_value_t *value, i2c_sensor_device_data_t *data)
{
	if(!values_valid)
		return(i2c_error_device_error_4);

	*value = humidity;

	return(i2c_error_ok);
}

irom attr_const i2c_error_t sensor_bme680_airpressure_read(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_value_t *value, i2c_sensor_device_data_t *data)
{
	if(!values_valid)
		return(i2c_error_device_error_4);

	*value = airpressure;

	return(i2c_error_ok);
}

irom attr_const i2c_error_t sensor_bme680_airquality_read(int bus, const i2c_sensor_device_table_entry_t *entry, i2c_sensor_value_t *value, i2c_sensor_device_data_t *data)
{
	if(!values_valid)
		return(i2c_error_device_error_4);

	*value = airquality;

	return(i2c_error_ok);
}
