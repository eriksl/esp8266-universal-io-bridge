#include "i2s.h"
#include "util.h"
#include "io.h"
#include "eagle.h"
#include "io_gpio.h"
#include "stats.h"

#include <stdint.h>

static bool inited = false;

enum
{
	bits_per_sample = 4,
	values_per_led = 4, /* some LEDs take four values */
};

attr_inline void i2s_interrupt_arm(bool enable) // inline required for ISR in IRAM
{
	static const unsigned int all_i2s_ints_clear =
			I2S_I2S_TX_REMPTY_INT_CLR |
			I2S_I2S_TX_WFULL_INT_CLR |
			I2S_I2S_RX_REMPTY_INT_CLR |
			I2S_I2S_RX_WFULL_INT_CLR |
			I2S_I2S_PUT_DATA_INT_CLR |
			I2S_I2S_TAKE_DATA_INT_CLR;

	clear_peri_reg_mask(I2SINT_ENA, all_i2s_ints_clear);
	set_peri_reg_mask(I2SINT_CLR, all_i2s_ints_clear);
	clear_peri_reg_mask(I2SINT_CLR, all_i2s_ints_clear);

	if(enable)
		set_peri_reg_mask(I2SINT_ENA, I2S_I2S_TX_REMPTY_INT_ENA);
}

iram static void i2s_callback(void *arg)
{
	ets_isr_mask(1 << ETS_SPI_INUM);

	stat_spi_slave_interrupts++;

	if(!(read_peri_reg(ISR_FLAGS_REG) & ISR_FLAGS_I2S))
		goto done;

	stat_spi_slave_i2s_interrupts++;
	i2s_interrupt_arm(false);
	clear_peri_reg_mask(I2SCONF, I2S_I2S_TX_START);

done:
	ets_isr_unmask(1 << ETS_SPI_INUM);
}

/*
 * Espressif black magic. No clue how it works, so please don't ask.
 */

static unsigned int get_peri_reg_bits(unsigned int reg, unsigned int hipos, unsigned int lowpos)
{
	return((read_peri_reg(reg) >> lowpos) & ((1 << (hipos - lowpos + 1)) - 1));
}

static unsigned int espressif_opaque_i2c_readReg(unsigned int block, unsigned int host_id, unsigned int reg_add)
{
    uint32_t mst_ctrl_addr = 0x60000d00 + (host_id * 4);

    write_peri_reg(mst_ctrl_addr, (0 << 24) | (0 << 16) | (reg_add << 8) | block);
    while(get_peri_reg_bits(mst_ctrl_addr, 25, 25) != 0);
    return((read_peri_reg(mst_ctrl_addr) >> 16) & 0xff);
}

static void espressif_opaque_i2c_writeReg(unsigned int block, unsigned int host_id, unsigned int reg_add, unsigned int pData)
{
    uint32_t mst_ctrl_addr = 0x60000d00 + (host_id * 4);

    write_peri_reg(mst_ctrl_addr, (1 << 24) | (pData << 16) | (reg_add << 8) | block);
    while(get_peri_reg_bits(mst_ctrl_addr, 25, 25) != 0);
}

static void espressif_opaque_i2c_writeReg_Mask(unsigned int block, unsigned int host_id, unsigned int reg_add, unsigned int msb, unsigned int lsb, unsigned int indata)
{
	unsigned int current = espressif_opaque_i2c_readReg(block, host_id, reg_add);

	espressif_opaque_i2c_writeReg(block, host_id, reg_add, (current & (~(((1 << (msb - lsb + 1)) - 1) << lsb))) | (indata << lsb));
}

static void espressif_opaque_start_i2s_clock(void)
{
	espressif_opaque_i2c_writeReg_Mask(0x67, 0x04, 0x04, 0x07, 0x07, 0x01);
}

/* https://github.com/espressif/ESP8266_MP3_DECODER/issues/3
 *
 *	CLK_I2S = 160MHz / I2S_CLKM_DIV_NUM
 *	BCLK = CLK_I2S / I2S_BCK_DIV_NUM
 *	WS = BCLK / 2 / (16 + I2S_BITS_MOD)
 *	Note that I2S_CLKM_DIV_NUM must be >5 for I2S data */

bool i2s_init(void)
{
	static const unsigned int all_spi_slave_ints =
			SPI_TRANS_DONE_EN |
			SPI_SLV_WR_STA_DONE_EN |
			SPI_SLV_RD_STA_DONE_EN |
			SPI_SLV_WR_BUF_DONE_EN |
			SPI_SLV_RD_BUF_DONE_EN |
			SPI_TRANS_DONE |
			SPI_SLV_WR_STA_DONE |
			SPI_SLV_RD_STA_DONE |
			SPI_SLV_WR_BUF_DONE |
			SPI_SLV_RD_BUF_DONE;

	static const unsigned int conf_reset_mask = I2S_I2S_TX_FIFO_RESET | I2S_I2S_RX_FIFO_RESET | I2S_I2S_TX_RESET | I2S_I2S_RX_RESET;

	static const unsigned int conf_setup =
				(( 4 & I2S_BCK_DIV_NUM)		<< I2S_BCK_DIV_NUM_S) |		// bitclock = 4.70 MHz / 3 = 1.57 MHz
				((17 & I2S_CLKM_DIV_NUM)	<< I2S_CLKM_DIV_NUM_S) |	// prescaler = 17, 80 MHz / 17 = 4.71 MHz
				(( 0 & I2S_BITS_MOD)		<< I2S_BITS_MOD_S) |		// see note above, has to do with the word clock, not used here
			//	I2S_RECE_MSB_SHIFT |									// unknown
			//	I2S_TRANS_MSB_SHIFT |									// unknown
			//	I2S_I2S_RX_START |										// start the receiver
			//	I2S_I2S_TX_START |										// start the sender
			//	I2S_MSB_RIGHT |											// unknown
			//	I2S_RIGHT_FIRST |										// unknown
			//	I2S_RECE_SLAVE_MOD |									// slave (clock) receiver (?)
			//	I2S_TRANS_SLAVE_MOD |									// slave (clock) sender (?)
			//	I2S_I2S_RX_FIFO_RESET |									// reset receiver FIFO
			//	I2S_I2S_TX_FIFO_RESET |									// reset sender FIFO
			//	I2S_I2S_RX_RESET |										// reset receiver
			//	I2S_I2S_TX_RESET |										// reset sender
			0;

	static const unsigned int fifoconf_setup =
			((0 & I2S_I2S_RX_FIFO_MOD) << I2S_I2S_RX_FIFO_MOD_S) |	// not used
			((0 & I2S_I2S_TX_FIFO_MOD) << I2S_I2S_TX_FIFO_MOD_S) |	// 0 = 16/16 bits layout
			//	I2S_I2S_DSCR_EN |									// DMA disabled FIFO mode enabled
			//	I2S_I2S_TX_DATA_NUM I2S_I2S_TX_DATA_NUM_S | 		// FIXME what's this
			//	I2S_I2S_RX_DATA_NUM I2S_I2S_RX_DATA_NUM_S | 		// FIXME what's this
			0;

	static const unsigned int confchan_setup =
			((0 & I2S_RX_CHAN_MOD) << I2S_RX_CHAN_MOD_S) |	// dual channel (not used)
			((0 & I2S_TX_CHAN_MOD) << I2S_TX_CHAN_MOD_S);	// dual channel

	clear_peri_reg_mask(I2SCONF, conf_reset_mask);
	set_peri_reg_mask(I2SCONF, conf_reset_mask);
	clear_peri_reg_mask(I2SCONF, conf_reset_mask);

	espressif_opaque_start_i2s_clock();

	write_peri_reg(I2SCONF, conf_setup);
	write_peri_reg(I2S_FIFO_CONF, fifoconf_setup);
	write_peri_reg(I2SCONF_CHAN, confchan_setup);

	clear_peri_reg_mask(SPI_SLAVE(0), all_spi_slave_ints);	// I2S interrupt is shared with SPI (flash) SLAVE mode interrupts, make sure we don't get them
	i2s_interrupt_arm(false);
	ets_isr_attach(ETS_SPI_INUM, i2s_callback, (void *)0);
	ets_isr_unmask(1 << ETS_SPI_INUM);

	inited = true;

	return(true);
}

static unsigned int fifo_count = 0;

bool i2s_send(unsigned int length, const uint8_t *data)
{
	uint32_t current = 0;
	unsigned int idx;

	if(!inited)
	{
		log("! i2s: not inited\n");
		return(false);
	}

	for(idx = 0; idx < length; idx++)
	{
		current <<= 8;
		current |= data[idx];

		if((idx & 0x03) == 0x03)
		{
			write_peri_reg(I2STXFIFO, current);
			fifo_count++;
			current = 0;
		}
	}

	if((idx & 0x03) != 0x00)
	{
		write_peri_reg(I2STXFIFO, current);
		fifo_count++;
	}

	return(true);
}

void i2s_flush(void)
{
	i2s_interrupt_arm(true);
	set_peri_reg_mask(I2SCONF, I2S_I2S_TX_START);
	fifo_count = 0;
}
