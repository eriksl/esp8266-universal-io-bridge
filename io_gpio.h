#ifndef io_gpio_h
#define io_gpio_h

#include "io.h"
#include "util.h"

#include <stdint.h>
#include <eagle_soc.h>

void		io_gpio_periodic(int io, const struct io_info_entry_T *, io_data_entry_t *, io_flags_t *);
io_error_t	io_gpio_init(const struct io_info_entry_T *);
io_error_t	io_gpio_init_pin_mode(string_t *, const struct io_info_entry_T *, io_data_pin_entry_t *, const io_config_pin_entry_t *, int);
io_error_t	io_gpio_get_pin_info(string_t *, const struct io_info_entry_T *, io_data_pin_entry_t *, const io_config_pin_entry_t *, int);
io_error_t	io_gpio_read_pin(string_t *, const struct io_info_entry_T *, io_data_pin_entry_t *, const io_config_pin_entry_t *, int, int *);
io_error_t	io_gpio_write_pin(string_t *, const struct io_info_entry_T *, io_data_pin_entry_t *, const io_config_pin_entry_t *, int, int);

app_action_t application_function_pwm_period(const string_t *src, string_t *dst);

#include "util.h"

// generic

always_inline attr_speed static uint32_t gpio_pin_addr(int pin)
{
	return(GPIO_PIN0_ADDRESS + (pin << 2));
}

// read / write registers

always_inline attr_speed static uint32_t read_peri_reg(uint32_t addr)
{
	volatile uint32_t *ptr = (volatile uint32_t *)addr;

	return(*ptr);
}

always_inline attr_speed static void write_peri_reg(volatile uint32_t addr, uint32_t value)
{
	volatile uint32_t *ptr = (volatile uint32_t *)addr;

	*ptr = value;
}

always_inline attr_speed static void clear_peri_reg_mask(volatile uint32_t addr, uint32_t mask)
{
	write_peri_reg(addr, read_peri_reg(addr) & ~mask);
}

always_inline attr_speed static void set_peri_reg_mask(volatile uint32_t addr, uint32_t mask)
{
	write_peri_reg(addr, read_peri_reg(addr) | mask);
}

always_inline attr_speed static void clear_set_peri_reg_mask(uint32_t addr, uint32_t clearmask, uint32_t setmask)
{
	clear_peri_reg_mask(addr, clearmask);
	set_peri_reg_mask(addr, setmask);
}

// read write GPIO registers

always_inline attr_speed static uint32_t gpio_reg_read(int reg)
{
	return(read_peri_reg(PERIPHS_GPIO_BASEADDR + reg));
}

always_inline attr_speed static void gpio_reg_write(int reg, uint32_t value)
{
	write_peri_reg(PERIPHS_GPIO_BASEADDR + reg, value);
}

// read input

always_inline attr_speed static uint32_t gpio_get_all(void)
{
	return(gpio_reg_read(GPIO_IN_ADDRESS));
}

always_inline attr_speed static int gpio_get(int io)
{
	return((gpio_get_all() & (1 << io)) ? 1 : 0);
}

// set output low / high

always_inline attr_speed static void gpio_clear_mask(uint32_t mask)
{
	gpio_reg_write(GPIO_OUT_W1TC_ADDRESS, mask);
}

always_inline attr_speed static void gpio_set_mask(uint32_t mask)
{
	gpio_reg_write(GPIO_OUT_W1TS_ADDRESS, mask);
}

always_inline attr_speed static void gpio_set(int io, int onoff)
{
	if(onoff)
		gpio_set_mask(1 << io);
	else
		gpio_clear_mask(1 << io);
}

#endif
