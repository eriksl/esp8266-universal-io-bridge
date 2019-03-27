#ifndef io_gpio_h
#define io_gpio_h

#include "io.h"
#include "util.h"

#include <stdint.h>
#include <eagle_soc.h>

void			io_gpio_reset_all_pins(void);
void			io_gpio_periodic_fast(int io, const struct io_info_entry_T *, io_data_entry_t *, io_flags_t *);
io_error_t		io_gpio_init(const struct io_info_entry_T *);
unsigned int	io_gpio_pin_max_value(const struct io_info_entry_T *, io_data_pin_entry_t *, const io_config_pin_entry_t *, unsigned int pin);
io_error_t		io_gpio_init_pin_mode(string_t *, const struct io_info_entry_T *, io_data_pin_entry_t *, const io_config_pin_entry_t *, int);
io_error_t		io_gpio_get_pin_info(string_t *, const struct io_info_entry_T *, io_data_pin_entry_t *, const io_config_pin_entry_t *, int);
io_error_t		io_gpio_read_pin(string_t *, const struct io_info_entry_T *, io_data_pin_entry_t *, const io_config_pin_entry_t *, int, uint32_t *);
io_error_t		io_gpio_write_pin(string_t *, const struct io_info_entry_T *, io_data_pin_entry_t *, const io_config_pin_entry_t *, int, uint32_t);
int				io_gpio_get_uart_from_pin(unsigned int pin);
_Bool			io_gpio_pwm1_width_set(unsigned int period, _Bool load, _Bool save);
unsigned int	io_gpio_pwm1_width_get(void);

// generic

attr_inline uint32_t gpio_pin_addr(int pin)
{
	return(GPIO_PIN0_ADDRESS + (pin << 2));
}

// read / write registers

attr_inline uint32_t read_peri_reg(uint32_t addr)
{
	volatile uint32_t *ptr = (volatile uint32_t *)addr;

	return(*ptr);
}

attr_inline void write_peri_reg(volatile uint32_t addr, uint32_t value)
{
	volatile uint32_t *ptr = (volatile uint32_t *)addr;

	*ptr = value;
}

attr_inline void clear_peri_reg_mask(volatile uint32_t addr, uint32_t mask)
{
	write_peri_reg(addr, read_peri_reg(addr) & ~mask);
}

attr_inline void set_peri_reg_mask(volatile uint32_t addr, uint32_t mask)
{
	write_peri_reg(addr, read_peri_reg(addr) | mask);
}

attr_inline void clear_set_peri_reg_mask(uint32_t addr, uint32_t clearmask, uint32_t setmask)
{
	uint32_t regval;

	regval = read_peri_reg(addr);
	regval &= ~clearmask;
	regval |= setmask;
	write_peri_reg(addr, regval);
}

// read write GPIO registers

attr_inline uint32_t gpio_reg_read(int reg)
{
	return(read_peri_reg(PERIPHS_GPIO_BASEADDR + reg));
}

attr_inline void gpio_reg_write(int reg, uint32_t value)
{
	write_peri_reg(PERIPHS_GPIO_BASEADDR + reg, value);
}

// read input

attr_inline uint32_t gpio_get_all(void)
{
	return(gpio_reg_read(GPIO_IN_ADDRESS));
}

attr_inline int gpio_get(int io)
{
	return((gpio_get_all() & (1 << io)) ? 1 : 0);
}

// set output low / high

attr_inline void gpio_clear_mask(uint32_t mask)
{
	gpio_reg_write(GPIO_OUT_W1TC_ADDRESS, mask);
}

attr_inline void gpio_set_mask(uint32_t mask)
{
	gpio_reg_write(GPIO_OUT_W1TS_ADDRESS, mask);
}

attr_inline void gpio_set(int io, int onoff)
{
	if(onoff)
		gpio_set_mask(1 << io);
	else
		gpio_clear_mask(1 << io);
}

#endif
