#ifndef _eagle_h_
#define _eagle_h_

#include "attribute.h"

#include <stdint.h>

enum
{
	PERIPHS_GPIO_BASEADDR =		0x60000300,
	GPIO_OUT_ADDRESS =			0x00,
	GPIO_OUT_W1TS_ADDRESS =		0x04,
	GPIO_OUT_W1TC_ADDRESS =		0x08,
	GPIO_ENABLE_ADDRESS =		0x0c,
	GPIO_ENABLE_W1TS_ADDRESS =	0x10,
	GPIO_ENABLE_W1TC_ADDRESS =	0x14,
	GPIO_IN_ADDRESS =			0x18,
	GPIO_STATUS_ADDRESS =		0x1c,
	GPIO_STATUS_W1TS_ADDRESS =	0x20,
	GPIO_STATUS_W1TC_ADDRESS =	0x24,
	GPIO_PIN0_ADDRESS =			0x28,
};

enum
{
	PERIPHS_IO_MUX_FUNC =			0x13,
	PERIPHS_IO_MUX_FUNC_BIT_2 =		1 << 8,
	PERIPHS_IO_MUX_PULLUP =			1 << 7,
	PERIPHS_IO_MUX_PULLUP2 =		1 << 6,
	PERIPHS_IO_MUX_FUNC_BIT_1 =		1 << 5,
	PERIPHS_IO_MUX_FUNC_BIT_0 =		1 << 4,
	PERIPHS_IO_MUX_SLEEP_PULLUP =	1 << 3,
	PERIPHS_IO_MUX_SLEEP_PULLUP2 =	1 << 2,
	PERIPHS_IO_MUX_SLEEP_OE =		1 << 1,
	PERIPHS_IO_MUX_OE =				1 << 0,
};

enum
{
	PERIPHS_IO_MUX =			0x60000800,
	PERIPHS_IO_MUX_MTDI_U =		PERIPHS_IO_MUX + 0x04,
	PERIPHS_IO_MUX_MTCK_U =		PERIPHS_IO_MUX + 0x08,
	PERIPHS_IO_MUX_MTMS_U =		PERIPHS_IO_MUX + 0x0c,
	PERIPHS_IO_MUX_MTDO_U =		PERIPHS_IO_MUX + 0x10,
	PERIPHS_IO_MUX_U0RXD_U =	PERIPHS_IO_MUX + 0x14,
	PERIPHS_IO_MUX_U0TXD_U =	PERIPHS_IO_MUX + 0x18,
	PERIPHS_IO_MUX_SD_CLK_U =	PERIPHS_IO_MUX + 0x1c,
	PERIPHS_IO_MUX_SD_DATA0_U =	PERIPHS_IO_MUX + 0x20,
	PERIPHS_IO_MUX_SD_DATA1_U =	PERIPHS_IO_MUX + 0x24,
	PERIPHS_IO_MUX_SD_DATA2_U =	PERIPHS_IO_MUX + 0x28,
	PERIPHS_IO_MUX_SD_DATA3_U =	PERIPHS_IO_MUX + 0x2c,
	PERIPHS_IO_MUX_SD_CMD_U =	PERIPHS_IO_MUX + 0x30,
	PERIPHS_IO_MUX_GPIO0_U =	PERIPHS_IO_MUX + 0x34,
	PERIPHS_IO_MUX_GPIO2_U =	PERIPHS_IO_MUX + 0x38,
	PERIPHS_IO_MUX_GPIO4_U =	PERIPHS_IO_MUX + 0x3c,
	PERIPHS_IO_MUX_GPIO5_U =	PERIPHS_IO_MUX + 0x40,
};

enum PERIPHS_IO_MUX_bits
{
	PERIPHS_IO_MUX_SPI_ENABLE =		0x005,
	PERIPHS_IO_MUX_SPI_SYSCLK =		1UL << 8,
	PERIPHS_IO_MUX_HSPI_ENABLE =	0x105,
	PERIPHS_IO_MUX_HSPI_SYSCLK =	1UL << 9,
};

enum
{
	ETS_SLC_INUM =			1,
	ETS_SPI_INUM =			2,
	ETS_RTC_INUM =			3,
	ETS_GPIO_INUM =			4,
	ETS_UART_INUM =			5,
	ETS_CCOMPARE0_INUM =	6,
	ETS_SOFT_INUM =			7,
	ETS_WDT_INUM =			8,
	ETS_TIMER0_INUM =		9,
	ETS_NMI_INUM =			14,
};

enum FUNC
{
	FUNC_GPIO0 =		0,
	FUNC_GPIO1 =		3,
	FUNC_GPIO2 =		0,
	FUNC_I2SO_WS =		1,
	FUNC_GPIO3 =		3,
	FUNC_GPIO4 =		0,
	FUNC_GPIO5 =		0,
	FUNC_GPIO6 =		3,
	FUNC_GPIO7 =		3,
	FUNC_GPIO8 =		3,
	FUNC_GPIO9 =		3,
	FUNC_GPIO10 =		3,
	FUNC_GPIO11 =		3,
	FUNC_HSPIQ =		2,
	FUNC_GPIO12 =		3,
	FUNC_I2SI_DATA =	1,
	FUNC_HSPID =		2,
	FUNC_GPIO13 =		3,
	FUNC_I2SI_BCK =		1,
	FUNC_HSPICLK =		2,
	FUNC_GPIO14 =		3,
	FUNC_I2SI_WS =		1,
	FUNC_HSPICS	=		2,
	FUNC_GPIO15 =		3,
	FUNC_I2SO_BCK =		1,
	FUNC_U0RXD =		0,
	FUNC_I2SO_DATA =	1,
	FUNC_U0TXD =		0,
	FUNC_U1TXD_BK =		2,
};

enum
{
	PERIPHS_RTC_BASEADDR =	0x60000700,
	RTC_GPIO_OUT =			PERIPHS_RTC_BASEADDR + 0x68,
	RTC_GPIO_ENABLE =		PERIPHS_RTC_BASEADDR + 0x74,
	RTC_GPIO_IN_DATA =		PERIPHS_RTC_BASEADDR + 0x8c,
	RTC_GPIO_CONF =			PERIPHS_RTC_BASEADDR + 0x90,
	PAD_XPD_DCDC_CONF =		PERIPHS_RTC_BASEADDR + 0xa0,
};

enum
{
	gpio_pdm_source =	1 << 0,
	gpio_open_drain =	1 << 2,
};

enum
{
	gpio_pdm_reg =				0x60000368,
	gpio_pdm_enable =			1 << 16,
	gpio_pdm_prescale_shift =	8,
	gpio_pdm_prescale_mask =	0xff,
	gpio_pdm_target_shift =		0,
	gpio_pdm_target_mask =		0xff,
};

enum INT_ENABLE
{
	INT_NMI_INT_ENT =	0x3ff00000,
	INT_ENABLE_REG =	0x3ff00004,
	INT_ENABLE_WDOG =	1 << 0,
	INT_ENABLE_TIMER0 =	1 << 1,
	INT_ENABLE_TIMER1 =	1 << 2,
};

enum CLK_PRE_PORT
{
	CLK_PRE_PORT_REG =	0x3ff00014,
	CLK_160 =			1 << 0,
};

enum ISR_FLAGS
{
	ISR_FLAGS_REG =		0x3ff00020,
	ISR_FLAGS_UART0 =	1 << 0,
	ISR_FLAGS_UART1 =	1 << 2,
	ISR_FLAGS_SPI =		1 << 4,
	ISR_FLAGS_HSPI =	1 << 7,
	ISR_FLAGS_I2S =		1 << 9,
};

enum IRAM_BASE
{
	IRAM_BASE_REG =			0x3ff00024,
	IRAM_MAP_0x40108000 =	1 << 7,
	IRAM_MAP_0x4010c000 =	1 << 8,
};

enum TIMER0_LOAD
{
	TIMER0_LOAD_REG = 0x60000600,
};

enum TIMER0_COUNT
{
	TIMER0_COUNT_REG = 0x60000604,
};

enum TIMER0_CTRL
{
	TIMER0_CTRL_REG =			0x60000608,
	TIMER0_CTRL_INT_EDGE =		0 << 0,
	TIMER0_CTRL_INT_LEVEL =		1 << 0,
	TIMER0_CTRL_DIVIDE_BY_16 =	1 << 2,
	TIMER0_CTRL_DIVIDE_BY_256 =	1 << 3,
	TIMER0_CTRL_AUTO_RELOAD =	1 << 6,
	TIMER0_CTRL_ENABLE_TIMER =	1 << 7,
	TIMER0_CTRL_INT_STATUS =	1 << 8,
};

enum TIMER0_INT
{
	TIMER0_INT_REG =	0x6000060c,
	TIMER0_INT_CLEAR =	1 << 0
};

enum TIMER1_LOAD
{
	TIMER1_LOAD_REG = 0x60000620,
};

enum TIMER1_COUNT
{
	TIMER1_COUNT_REG = 0x60000624,
};

enum TIMER1_CTRL
{
	TIMER1_CTRL_REG = 0x60000628,
	TIMER1_CTRL_INT_EDGE =		0 << 0,
	TIMER1_CTRL_INT_LEVEL =		1 << 0,
	TIMER1_CTRL_DIVIDE_BY_16 =	1 << 2,
	TIMER1_CTRL_DIVIDE_BY_256 =	1 << 3,
	TIMER1_CTRL_AUTO_RELOAD =	1 << 6,
	TIMER1_CTRL_ENABLE_TIMER =	1 << 7,
	TIMER1_CTRL_INT_STATUS =	1 << 8,
};

enum
{
	WDT_CNTL =			0x60000900,
	WDT_STAGE0_RELOAD =	0x60000904,
	WDT_STAGE1_RELOAD =	0x60000908,
	WDT_COUNTER =		0x6000090c,
	WDT_STAGE =			0x60000910,
	WDT_RESET =			0x60000914,
	WDT_RESET_STAGE =	0x60000918,
	WDT_SDK_TIMER_REG = 0x3ff20c00,
};

enum
{
	WDT_CNTL_ENABLE =	1 << 0,
};

enum
{
	UART_CLK_FREQ = 80000000,
};

attr_inline uint32_t REG_UART_BASE(uint32_t i)	{ return(0x60000000 + (i * 0xf00)); }
attr_inline uint32_t UART_FIFO(uint32_t i)		{ return(REG_UART_BASE(i) + 0x0); }

enum
{
	UART_RXFIFO_RD_BYTE =	0xff,
	UART_RXFIFO_RD_BYTE_S =	0,
};

attr_inline uint32_t UART_INT_RAW(uint32_t i)	{ return(REG_UART_BASE(i) + 0x4); }

enum
{
	UART_RXFIFO_TOUT_INT_RAW =	1U << 8,
	UART_BRK_DET_INT_RAW =		1U << 7,
	UART_CTS_CHG_INT_RAW =		1U << 6,
	UART_DSR_CHG_INT_RAW =		1U << 5,
	UART_RXFIFO_OVF_INT_RAW =	1U << 4,
	UART_FRM_ERR_INT_RAW =		1U << 3,
	UART_PARITY_ERR_INT_RAW =	1U << 2,
	UART_TXFIFO_EMPTY_INT_RAW =	1U << 1,
	UART_RXFIFO_FULL_INT_RAW =	1U << 0,
};

attr_inline uint32_t UART_INT_ST(uint32_t i )	{ return(REG_UART_BASE(i) + 0x8); }

enum
{
	UART_RXFIFO_TOUT_INT_ST =	1U << 8,
	UART_BRK_DET_INT_ST =		1U << 7,
	UART_CTS_CHG_INT_ST =		1U << 6,
	UART_DSR_CHG_INT_ST =		1U << 5,
	UART_RXFIFO_OVF_INT_ST =	1U << 4,
	UART_FRM_ERR_INT_ST =		1U << 3,
	UART_PARITY_ERR_INT_ST =	1U << 2,
	UART_TXFIFO_EMPTY_INT_ST =	1U << 1,
	UART_RXFIFO_FULL_INT_ST =	1U << 0,
};

attr_inline uint32_t UART_INT_ENA(uint32_t i)	{ return(REG_UART_BASE(i) + 0xc); }

enum
{
	UART_RXFIFO_TOUT_INT_ENA =	1U << 8,
	UART_BRK_DET_INT_ENA =		1U << 7,
	UART_CTS_CHG_INT_ENA =		1U << 6,
	UART_DSR_CHG_INT_ENA =		1U << 5,
	UART_RXFIFO_OVF_INT_ENA =	1U << 4,
	UART_FRM_ERR_INT_ENA =		1U << 3,
	UART_PARITY_ERR_INT_ENA =	1U << 2,
	UART_TXFIFO_EMPTY_INT_ENA =	1U << 1,
	UART_RXFIFO_FULL_INT_ENA =	1U << 0,
};

attr_inline uint32_t UART_INT_CLR(uint32_t i)	{ return(REG_UART_BASE(i) + 0x10); }

enum
{
	UART_RXFIFO_TOUT_INT_CLR =	1U << 8,
	UART_BRK_DET_INT_CLR =		1U << 7,
	UART_CTS_CHG_INT_CLR =		1U << 6,
	UART_DSR_CHG_INT_CLR =		1U << 5,
	UART_RXFIFO_OVF_INT_CLR =	1U << 4,
	UART_FRM_ERR_INT_CLR =		1U << 3,
	UART_PARITY_ERR_INT_CLR =	1U << 2,
	UART_TXFIFO_EMPTY_INT_CLR =	1U << 1,
	UART_RXFIFO_FULL_INT_CLR =	1U << 0,
};

attr_inline uint32_t UART_CLKDIV(uint32_t i)	{ return(REG_UART_BASE(i) + 0x14); }

enum
{
	UART_CLKDIV_CNT =			0x000fffff,
	UART_CLKDIV_S =				0,
};

attr_inline uint32_t UART_AUTOBAUD(uint32_t i)	{ return(REG_UART_BASE(i) + 0x18); }

enum
{
	UART_GLITCH_FILT =			0x000000ff,
	UART_GLITCH_FILT_S =		8,
	UART_AUTOBAUD_EN =			1U << 0,
};

attr_inline uint32_t UART_STATUS(uint32_t i)	{ return(REG_UART_BASE(i) + 0x1c); }

enum
{
	UART_TXD =					1U << 31,
	UART_RTSN =					1U << 30,
	UART_DTRN =					1U << 29,
	UART_TXFIFO_CNT =			0x000000ff,
	UART_TXFIFO_CNT_S =			16,
	UART_RXD =					1U << 15,
	UART_CTSN =					1U << 14,
	UART_DSRN =					1U << 13,
	UART_RXFIFO_CNT =			0x000000ff,
	UART_RXFIFO_CNT_S =			0,
};

attr_inline uint32_t UART_CONF0(uint32_t i)		{ return(REG_UART_BASE(i) + 0x20); }

enum
{
	UART_DTR_INV =						1U << 24,
	UART_RTS_INV =						1U << 23,
	UART_TXD_INV =						1U << 22,
	UART_DSR_INV =						1U << 21,
	UART_CTS_INV =						1U << 20,
	UART_RXD_INV =						1U << 19,
	UART_TXFIFO_RST =					1U << 18,
	UART_RXFIFO_RST =					1U << 17,
	UART_IRDA_EN =						1U << 16,
	UART_TX_FLOW_EN =					1U << 15,
	UART_LOOPBACK =						1U << 14,
	UART_IRDA_RX_INV =					1U << 13,
	UART_IRDA_TX_INV =					1U << 12,
	UART_IRDA_WCTL =					1U << 11,
	UART_IRDA_TX_EN =					1U << 10,
	UART_IRDA_DPLX =					1U << 9,
	UART_TXD_BRK =						1U << 8,
	UART_SW_DTR =						1U << 7,
	UART_SW_RTS =						1U << 6,
	UART_STOP_BIT_NUM =					0x00000003,
	UART_STOP_BIT_NUM_S =				4,
	UART_BIT_NUM =						0x00000003,
	UART_BIT_NUM_S =					2,
	UART_PARITY_EN =					1U << 1,
	UART_PARITY =						1U << 0,
};

attr_inline uint32_t UART_CONF1(uint32_t i)		{ return(REG_UART_BASE(i) + 0x24); }

enum
{
	UART_RX_TOUT_EN =						1U << 31,
	UART_RX_TOUT_THRHD =					0x7f,
	UART_RX_TOUT_THRHD_S =					24,
	UART_RX_FLOW_EN =						1 << 23,
	UART_RX_FLOW_THRHD =					0x7f,
	UART_RX_FLOW_THRHD_S =					16,
	UART_TXFIFO_EMPTY_THRHD =				0x7f,
	UART_TXFIFO_EMPTY_THRHD_S =				8,
	UART_RXFIFO_FULL_THRHD =				0x7f,
	UART_RXFIFO_FULL_THRHD_S =				0,
};

attr_inline uint32_t UART_LOWPULSE(uint32_t i)	{ return(REG_UART_BASE(i) + 0x28); }

enum
{
	UART_LOWPULSE_MIN_CNT =					0x000fffff,
	UART_LOWPULSE_MIN_CNT_S =				0,
};

attr_inline uint32_t UART_HIGHPULSE(uint32_t i)	{ return(REG_UART_BASE(i) + 0x2c); }

enum
{
	UART_HIGHPULSE_MIN_CNT =				0x000fffff,
	UART_HIGHPULSE_MIN_CNT_S =				0,
};

attr_inline uint32_t UART_PULSE_NUM(uint32_t i)	{ return(REG_UART_BASE(i) + 0x30); }

enum
{
	UART_PULSE_NUM_CNT =					0x0003ff,
	UART_PULSE_NUM_CNT_S =					0,
};

attr_inline unsigned int REG_SPI_BASE(unsigned int i)	{ return(0x60000200 - (i * 0x100)); }

attr_inline unsigned int SPI_CMD(unsigned int i)		{ return(REG_SPI_BASE(i) + 0x00); }
attr_inline unsigned int SPI_ADDR(unsigned int i)		{ return(REG_SPI_BASE(i) + 0x04); }
attr_inline unsigned int SPI_CTRL(unsigned int i)		{ return(REG_SPI_BASE(i) + 0x08); }
attr_inline unsigned int SPI_CTRL1(unsigned int i)		{ return(REG_SPI_BASE(i) + 0x0c); }
attr_inline unsigned int SPI_RD_STATUS(unsigned int i)	{ return(REG_SPI_BASE(i) + 0x10); }
attr_inline unsigned int SPI_CTRL2(unsigned int i)		{ return(REG_SPI_BASE(i) + 0x14); }
attr_inline unsigned int SPI_CLOCK(unsigned int i)		{ return(REG_SPI_BASE(i) + 0x18); }
attr_inline unsigned int SPI_USER(unsigned int i)		{ return(REG_SPI_BASE(i) + 0x1c); }
attr_inline unsigned int SPI_USER1(unsigned int i)		{ return(REG_SPI_BASE(i) + 0x20); }
attr_inline unsigned int SPI_USER2(unsigned int i)		{ return(REG_SPI_BASE(i) + 0x24); }
attr_inline unsigned int SPI_WR_STATUS(unsigned int i)	{ return(REG_SPI_BASE(i) + 0x28); }
attr_inline unsigned int SPI_PIN(unsigned int i)		{ return(REG_SPI_BASE(i) + 0x2c); }
attr_inline unsigned int SPI_SLAVE(unsigned int i)		{ return(REG_SPI_BASE(i) + 0x30); }
attr_inline unsigned int SPI_SLAVE1(unsigned int i)		{ return(REG_SPI_BASE(i) + 0x34); }
attr_inline unsigned int SPI_SLAVE2(unsigned int i)		{ return(REG_SPI_BASE(i) + 0x38); }
attr_inline unsigned int SPI_SLAVE3(unsigned int i)		{ return(REG_SPI_BASE(i) + 0x3c); }
attr_inline unsigned int SPI_W0(unsigned int i)			{ return(REG_SPI_BASE(i) + 0x40); }
attr_inline unsigned int SPI_EXT0(unsigned int i)		{ return(REG_SPI_BASE(i) + 0xf0); }
attr_inline unsigned int SPI_EXT1(unsigned int i)		{ return(REG_SPI_BASE(i) + 0xf4); }
attr_inline unsigned int SPI_EXT2(unsigned int i)		{ return(REG_SPI_BASE(i) + 0xf8); }
attr_inline unsigned int SPI_EXT3(unsigned int i)		{ return(REG_SPI_BASE(i) + 0xfc); }

enum SPI_CMD_bits
{
	SPI_USR =			1UL << 18,
};

enum SPI_CTRL_bits
{
	SPI_WR_BIT_ORDER =	1UL << 26,		//	write: 1: LSB first, 0: MSB first
	SPI_RD_BIT_ORDER =	1UL << 25,		//	read:  1: LSB first, 0: MSB first
	SPI_QIO_MODE =		1UL << 24,
	SPI_DIO_MODE =		1UL << 23,
	SPI_QOUT_MODE =		1UL << 20,
	SPI_DOUT_MODE =		1UL << 14,
	SPI_FASTRD_MODE =	1UL << 13,
};

enum SPI_RD_STATUS_bits
{
	SPI_STATUS_EXT =				0xff,
	SPI_STATUS_EXT_S =				24,
	SPI_WB_MODE =					0xff,
	SPI_WB_MODE_S =					16,
	SPI_FLASH_STATUS_PRO_FLAG =		1UL << 7,
	SPI_FLASH_TOP_BOT_PRO_FLAG =	1UL << 5,
	SPI_FLASH_BP2 =					1UL << 4,
	SPI_FLASH_BP1 =					1UL << 3,
	SPI_FLASH_BP0 =					1UL << 2,
	SPI_FLASH_WRENABLE_FLAG =		1UL << 1,
	SPI_FLASH_BUSY_FLAG =			1UL << 0,
};

enum SPI_CTRL2_bits
{
	SPI_CS_DELAY_NUM =			0xf,
	SPI_CS_DELAY_NUM_S =		28,
	SPI_CS_DELAY_MODE_0_0 =		(0b00 << 26),
	SPI_CS_DELAY_MODE_0_5 =		(0b01 << 26),
	SPI_CS_DELAY_MODE_1_0 =		(0b10 << 26),
	SPI_MOSI_DELAY_NUM =		0x7,
	SPI_MOSI_DELAY_NUM_S =		23,
	SPI_MOSI_DELAY_MODE_0_0 =	(0b00 << 21),
	SPI_MOSI_DELAY_MODE_0_5 =	(0b01 << 21),
	SPI_MOSI_DELAY_MODE_1_0 =	(0b10 << 21),
	SPI_MISO_DELAY_NUM =		0x7,
	SPI_MISO_DELAY_NUM_S =		0x7,
	SPI_MISO_DELAY_MODE_0_0 =	(0b00 << 16),
	SPI_MISO_DELAY_MODE_0_5 =	(0b01 << 16),
	SPI_MISO_DELAY_MODE_1_0 =	(0b10 << 16),
	SPI_CK_OUT_HIGH_MODE =		0xf,
	SPI_CK_OUT_HIGH_MODE_S =	12,
	SPI_CK_OUT_LOW_MODE =		0xf,
	SPI_CK_OUT_LOW_MODE_S =		8,
	SPI_HOLD_TIME =				0xf,
	SPI_HOLD_TIME_S =			4,
	SPI_SETUP_TIME =			0xf,
	SPI_SETUP_TIME_S =			0,
};

enum SPI_CLOCK_bits
{
	SPI_CLK_EQU_SYSCLK =		1UL << 31,
	SPI_CLKDIV_PRE =			0x1fff,
	SPI_CLKDIV_PRE_S =			18,
	SPI_CLKCNT_N =				0x3f,
	SPI_CLKCNT_N_S =			12,
	SPI_CLKCNT_H =				0x3f,			// "in the master mode, it must be floor((SPI_CLKCNT_N + 1) / 2 - 1). In the slave mode, it must be 0."
	SPI_CLKCNT_H_S =			6,
	SPI_CLKCNT_L =				0x3f,			// "in the master mode, it must be equal to SPI_CLKCNT_N. In the slave mode, it must be 0."
	SPI_CLKCNT_L_S =			0,
};

enum SPI_USER_bits
{
	SPI_USR_COMMAND =			1UL << 31,
	SPI_USR_ADDR =				1UL << 30,
	SPI_USR_DUMMY =				1UL << 29,
	SPI_USR_MISO =				1UL << 28,
	SPI_USR_MOSI =				1UL << 27,
	SPI_USR_DUMMY_IDLE =		1UL << 26,
	SPI_USR_MOSI_HIGHPART =		1UL << 25,
	SPI_USR_MISO_HIGHPART =		1UL << 24,
	SPI_USR_PREP_HOLD =			1UL << 23,
	SPI_USR_CMD_HOLD =			1UL << 22,
	SPI_USR_ADDR_HOLD =			1UL << 21,
	SPI_USR_DUMMY_HOLD =		1UL << 20,
	SPI_USR_DIN_HOLD =			1UL << 19,
	SPI_USR_DOUT_HOLD =			1UL << 18,
	SPI_USR_HOLD_POL =			1UL << 17,
	SPI_SIO =					1UL << 16,
	SPI_FWRITE_QIO =			1UL << 15,
	SPI_FWRITE_DIO =			1UL << 14,
	SPI_FWRITE_QUAD =			1UL << 13,
	SPI_FWRITE_DUAL =			1UL << 12,
	SPI_WR_BYTE_ORDER =			1UL << 11,		//	write: 	1: little endian, 0: big endian
	SPI_RD_BYTE_ORDER =			1UL << 10,		//	read:	1: little endian, 0: big endian
	SPI_AHB_ENDIAN_MODE =		0x3,
	SPI_AHB_ENDIAN_MODE_S =		8,
	SPI_CK_OUT_EDGE =			1UL << 7,		//	master:	clock phase: 0: data valid on active phase of clock, 1: data valid on inactive phase of clock
	SPI_CK_I_EDGE =				1UL << 6,		//	slave:	clock phase: 0: data valid on active phase of clock, 1: data valid on inactive phase of clock
	SPI_CS_SETUP =				1UL << 5,
	SPI_CS_HOLD =				1UL << 4,
	SPI_AHB_USR_COMMAND =		1UL << 3,
	SPI_FLASH_MODE =			1UL << 2,
	SPI_AHB_USR_COMMAND_4BYTE =	1UL << 1,
	SPI_DOUTDIN =				1UL << 0,
};

enum SPI_USER1_bits
{
	SPI_USR_ADDR_BITLEN =		0x3f,
	SPI_USR_ADDR_BITLEN_S =		26,
	SPI_USR_MOSI_BITLEN =		0x1ff,
	SPI_USR_MOSI_BITLEN_S =		17,
	SPI_USR_MISO_BITLEN =		0x1ff,
	SPI_USR_MISO_BITLEN_S =		8,
	SPI_USR_DUMMY_CYCLELEN =	0xff,
	SPI_USR_DUMMY_CYCLELEN_S =	0,
};

enum SPI_USER2_bits
{
	SPI_USR_COMMAND_BITLEN =	0xf,
	SPI_USR_COMMAND_BITLEN_S =	28,
	SPI_USR_COMMAND_VALUE =		0xffff,
	SPI_USR_COMMAND_VALUE_S =	0,
};

enum SPI_PIN_bits
{
	SPI_IDLE_EDGE =				1UL << 29,		//	0: clock polarity normal (CPOL = 0, active high), 1: clock polarity inverted (CPOL = 1, active low)
	SPI_CS2_DIS =				1UL << 2,
	SPI_CS1_DIS =				1UL << 1,
	SPI_CS0_DIS =				1UL << 0,
};

enum SPI_SLAVE_bits
{
	SPI_SYNC_RESET =			1UL << 31,
	SPI_SLAVE_MODE =			1UL << 30,
	SPI_SLV_WR_RD_BUF_EN =		1UL << 29,
	SPI_SLV_WR_RD_STA_EN =		1UL << 28,
	SPI_SLV_CMD_DEFINE =		1UL << 27,
	SPI_TRANS_CNT =				0xf,
	SPI_TRANS_CNT_S =			23,
	SPI_SLV_LAST_STATE =		0x7,
	SPI_SLV_LAST_STATE_S =		20,
	SPI_SLV_LAST_COMMAND =		0x7,
	SPI_SLV_LAST_COMMAND_S =	17,
	SPI_CS_I_MODE =				0x3,
	SPI_CS_I_MODE_S =			10,
	SPI_TRANS_DONE_EN =			1UL << 9,
	SPI_SLV_WR_STA_DONE_EN =	1UL << 8,
	SPI_SLV_RD_STA_DONE_EN =	1UL << 7,
	SPI_SLV_WR_BUF_DONE_EN =	1UL << 6,
	SPI_SLV_RD_BUF_DONE_EN =	1UL << 5,
	SLV_SPI_INT_EN =			0x1f,
	SLV_SPI_INT_EN_S =			5,
	SPI_TRANS_DONE =			1UL << 4,
	SPI_SLV_WR_STA_DONE =		1UL << 3,
	SPI_SLV_RD_STA_DONE =		1UL << 2,
	SPI_SLV_WR_BUF_DONE =		1UL << 1,
	SPI_SLV_RD_BUF_DONE =		1UL << 0,
};

enum SPI_SLAVE1_bits
{
	SPI_SLV_STATUS_BITLEN =		0x1f,
	SPI_SLV_STATUS_BITLEN_S =	27,
	SPI_SLV_STATUS_FAST_EN =	1UL << 26,
	SPI_SLV_STATUS_READBACK =	1UL << 25,
	SPI_SLV_BUF_BITLEN =		0x1ff,
	SPI_SLV_BUF_BITLEN_S =		16,
	SPI_SLV_RD_ADDR_BITLEN =	0x3f,
	SPI_SLV_RD_ADDR_BITLEN_S =	10,
	SPI_SLV_WR_ADDR_BITLEN =	0x3f,
	SPI_SLV_WR_ADDR_BITLEN_S =	4,
	SPI_SLV_WRSTA_DUMMY_EN =	1UL << 3,
	SPI_SLV_RDSTA_DUMMY_EN =	1UL << 2,
	SPI_SLV_WRBUF_DUMMY_EN =	1UL << 1,
	SPI_SLV_RDBUF_DUMMY_EN =	1UL << 0,
};

enum SPI_SLAVE2_bits
{
	SPI_SLV_WRBUF_DUMMY_CYCLELEN =		0xff,
	SPI_SLV_WRBUF_DUMMY_CYCLELEN_S =	24,
	SPI_SLV_RDBUF_DUMMY_CYCLELEN =		0xff,
	SPI_SLV_RDBUF_DUMMY_CYCLELEN_S =	16,
	SPI_SLV_WRSTR_DUMMY_CYCLELEN =		0xff,
	SPI_SLV_WRSTR_DUMMY_CYCLELEN_S =	8,
	SPI_SLV_RDSTR_DUMMY_CYCLELEN =		0xff,
	SPI_SLV_RDSTR_DUMMY_CYCLELEN_S =	0,
};

enum SPI_SLAVE3_bits
{
	SPI_SLV_WRSTA_CMD_VALUE =	0xff,
	SPI_SLV_WRSTA_CMD_VALUE_S =	24,
	SPI_SLV_RDSTA_CMD_VALUE =	0xff,
	SPI_SLV_RDSTA_CMD_VALUE_S =	16,
	SPI_SLV_WRBUF_CMD_VALUE =	0xff,
	SPI_SLV_WRBUF_CMD_VALUE_S =	8,
	SPI_SLV_RDBUF_CMD_VALUE =	0xff,
	SPI_SLV_RDBUF_CMD_VALUE_S =	0,
};

enum SPI_EXT0_bits
{
	SPI_T_PP_ENA =		1UL << 31,
	SPI_T_PP_SHIFT =	0xf,
	SPI_T_PP_SHIFT_S =	16,
	SPI_T_PP_TIME =		0xfff,
	SPI_T_PP_TIME_S =	0,
};

enum SPI_EXT1_bits
{
	SPI_T_ERASE_ENA =		1UL << 31,
	SPI_T_ERASE_SHIFT =		0xf,
	SPI_T_ERASE_SHIFT_S =	16,
	SPI_T_ERASE_TIME =		0xfff,
	SPI_T_ERASE_TIME_S =	0,
};

enum SPI_EXT2_bits
{
	SPI_ST =	0x7,
	SPI_ST_S =	0,
};

enum SPI_EXT3_bits
{
	SPI_INT_HOLD_ENA =		0x3,
	SPI_INT_HOLD_ENA_S =	0,
};

enum
{
	SPI_BUFFER_SIZE = 64,
};

attr_inline uint32_t UART_DATE(uint32_t i)		{ return(REG_UART_BASE(i) + 0x78); }
attr_inline uint32_t UART_ID(uint32_t i)		{ return(REG_UART_BASE(i) + 0x7c); }

typedef void (*ets_isr_t)(void *);

void NmiTimSetFunc(void(*func)(void));
void ets_isr_mask(uint32_t);
void ets_isr_unmask(uint32_t);
void ets_isr_attach(int, ets_isr_t func, void *arg);

typedef struct attr_packed
{
	uint32_t	blocksize:12;
	uint32_t	datalen:12;
	uint32_t	unused:6;
	uint32_t 	eof:1;
	uint32_t	owner:1;

	void*	buffer;
	void*	next_link;
} slc_pointer_t;

assert_size(slc_pointer_t, 12);

enum
{
	DR_REG_I2S_BASE =		0x60000e00,
};

enum
{
	I2STXFIFO =		DR_REG_I2S_BASE + 0x0000,
	I2SRXFIFO  =	DR_REG_I2S_BASE + 0x0004,
};

enum
{
	I2SCONF =				DR_REG_I2S_BASE + 0x0008,

	I2S_BCK_DIV_NUM =		0x0000003f,
	I2S_BCK_DIV_NUM_S =		22,

	I2S_CLKM_DIV_NUM =		0x0000003f,
	I2S_CLKM_DIV_NUM_S =	16,

	I2S_BITS_MOD =			0x0000000f,
	I2S_BITS_MOD_S =		12,

	I2S_RECE_MSB_SHIFT =	1 << 11,
	I2S_TRANS_MSB_SHIFT =	1 << 10,
	I2S_I2S_RX_START =		1 << 9,
	I2S_I2S_TX_START =		1 << 8,
	I2S_MSB_RIGHT =			1 << 7,
	I2S_RIGHT_FIRST =		1 << 6,
	I2S_RECE_SLAVE_MOD =	1 << 5,
	I2S_TRANS_SLAVE_MOD	=	1 << 4,
	I2S_I2S_RX_FIFO_RESET =	1 << 3,
	I2S_I2S_TX_FIFO_RESET =	1 << 2,
	I2S_I2S_RX_RESET =		1 << 1,
	I2S_I2S_TX_RESET =		1 << 0,
};

enum
{
	I2SINT_RAW =					DR_REG_I2S_BASE + 0x000c,
	I2S_I2S_TX_REMPTY_INT_RAW =		1 << 5,
	I2S_I2S_TX_WFULL_INT_RAW =		1 << 4,
	I2S_I2S_RX_REMPTY_INT_RAW =		1 << 3,
	I2S_I2S_RX_WFULL_INT_RAW =		1 << 2,
	I2S_I2S_TX_PUT_DATA_INT_RAW	=	1 << 1,
	I2S_I2S_RX_TAKE_DATA_INT_RAW =	1 << 0
};

enum
{
	I2SINT_ST =						DR_REG_I2S_BASE + 0x0010,
	I2S_I2S_TX_REMPTY_INT_ST =		1 << 5,
	I2S_I2S_TX_WFULL_INT_ST =		1 << 4,
	I2S_I2S_RX_REMPTY_INT_ST = 		1 << 3,
	I2S_I2S_RX_WFULL_INT_ST =		1 << 2,
	I2S_I2S_TX_PUT_DATA_INT_ST =	1 << 1,
	I2S_I2S_RX_TAKE_DATA_INT_ST =	1 << 0
};

enum
{
	I2SINT_ENA =					DR_REG_I2S_BASE + 0x0014,
	I2S_I2S_TX_REMPTY_INT_ENA =		1 << 5,
	I2S_I2S_TX_WFULL_INT_ENA =		1 << 4,
	I2S_I2S_RX_REMPTY_INT_ENA =		1 << 3,
	I2S_I2S_RX_WFULL_INT_ENA =		1 << 2,
	I2S_I2S_TX_PUT_DATA_INT_ENA =	1 << 1,
	I2S_I2S_RX_TAKE_DATA_INT_ENA =	1 << 0
};

enum
{
	I2SINT_CLR =					DR_REG_I2S_BASE + 0x0018,
	I2S_I2S_TX_REMPTY_INT_CLR =		1 << 5,
	I2S_I2S_TX_WFULL_INT_CLR =		1 << 4,
	I2S_I2S_RX_REMPTY_INT_CLR =		1 << 3,
	I2S_I2S_RX_WFULL_INT_CLR =		1 << 2,
	I2S_I2S_PUT_DATA_INT_CLR =		1 << 1,
	I2S_I2S_TAKE_DATA_INT_CLR =		1 << 0
};

enum
{
	I2STIMING =						DR_REG_I2S_BASE + 0x001c,
	I2S_TRANS_BCK_IN_INV =			1 << 22,
	I2S_RECE_DSYNC_SW =				1 << 21,
	I2S_TRANS_DSYNC_SW =			1 << 20,
	I2S_RECE_BCK_OUT_DELAY =		0x00000003,
	I2S_RECE_BCK_OUT_DELAY_S =		18,
	I2S_RECE_WS_OUT_DELAY =			0x00000003,
	I2S_RECE_WS_OUT_DELAY_S =		16,
	I2S_TRANS_SD_OUT_DELAY =		0x00000003,
	I2S_TRANS_SD_OUT_DELAY_S =		14,
	I2S_TRANS_WS_OUT_DELAY =		0x00000003,
	I2S_TRANS_WS_OUT_DELAY_S =		12,
	I2S_TRANS_BCK_OUT_DELAY =		0x00000003,
	I2S_TRANS_BCK_OUT_DELAY_S =		10,
	I2S_RECE_SD_IN_DELAY =			0x00000003,
	I2S_RECE_SD_IN_DELAY_S =		8,
	I2S_RECE_WS_IN_DELAY =			0x00000003,
	I2S_RECE_WS_IN_DELAY_S =		6,
	I2S_RECE_BCK_IN_DELAY =			0x00000003,
	I2S_RECE_BCK_IN_DELAY_S =		4,
	I2S_TRANS_WS_IN_DELAY =			0x00000003,
	I2S_TRANS_WS_IN_DELAY_S =		2,
	I2S_TRANS_BCK_IN_DELAY =		0x00000003,
	I2S_TRANS_BCK_IN_DELAY_S =		0
};

enum
{
	I2S_FIFO_CONF =			DR_REG_I2S_BASE + 0x0020,
	I2S_I2S_RX_FIFO_MOD =	0x00000007,
	I2S_I2S_RX_FIFO_MOD_S =	16,
	I2S_I2S_TX_FIFO_MOD =	0x00000007,
	I2S_I2S_TX_FIFO_MOD_S =	13,
	I2S_I2S_DSCR_EN =		1 << 12,
	I2S_I2S_TX_DATA_NUM	=	0x0000003f,
	I2S_I2S_TX_DATA_NUM_S =	6,
	I2S_I2S_RX_DATA_NUM = 	0x0000003f,
	I2S_I2S_RX_DATA_NUM_S =	0
};

enum
{
	I2SRXEOF_NUM =			DR_REG_I2S_BASE + 0x0024,
	I2S_I2S_RX_EOF_NUM =	0xffffffff,
	I2S_I2S_RX_EOF_NUM_S =	0
};

enum
{
	I2SCONF_SIGLE_DATA =	DR_REG_I2S_BASE + 0x0028,
	I2S_I2S_SIGLE_DATA =	0xffffffff,
	I2S_I2S_SIGLE_DATA_S =	0
};

enum
{
	I2SCONF_CHAN =			DR_REG_I2S_BASE + 0x002c,
	I2S_RX_CHAN_MOD =		0x00000003,
	I2S_RX_CHAN_MOD_S =		3,
	I2S_TX_CHAN_MOD =		0x00000007,
	I2S_TX_CHAN_MOD_S =		0
};

void rom_i2c_writeReg(uint8_t block, uint8_t host_id, uint8_t reg_add, uint8_t data);

#endif
