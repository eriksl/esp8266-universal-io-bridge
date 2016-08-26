#ifndef io_gpio_h
#define io_gpio_h

#include "util.h"
#include "io_shared.h"

#include <stdint.h>
#include <eagle_soc.h>

void		io_gpio_periodic(int io, const struct io_info_entry_T *, io_data_entry_t *, io_flags_t *);
io_error_t	io_gpio_init(const struct io_info_entry_T *);
io_error_t	io_gpio_init_pin_mode(string_t *, const struct io_info_entry_T *, io_data_pin_entry_t *, const io_config_pin_entry_t *, int);
io_error_t	io_gpio_get_pin_info(string_t *, const struct io_info_entry_T *, io_data_pin_entry_t *, const io_config_pin_entry_t *, int);
io_error_t	io_gpio_read_pin(string_t *, const struct io_info_entry_T *, io_data_pin_entry_t *, const io_config_pin_entry_t *, int, int *);
io_error_t	io_gpio_write_pin(string_t *, const struct io_info_entry_T *, io_data_pin_entry_t *, const io_config_pin_entry_t *, int, int);

#include "util.h"

// generic

iram static inline uint32_t gpio_pin_addr(int pin)
{
	return(GPIO_PIN0_ADDRESS + (pin * 4));
}

// read / write registers

iram static inline uint32_t read_peri_reg(uint32_t addr)
{
	volatile uint32_t *ptr = (volatile uint32_t *)addr;

	return(*ptr);
}

iram static inline void write_peri_reg(volatile uint32_t addr, uint32_t value)
{
	volatile uint32_t *ptr = (volatile uint32_t *)addr;

	*ptr = value;
}

iram static inline void clear_peri_reg_mask(volatile uint32_t addr, uint32_t mask)
{
	write_peri_reg(addr, read_peri_reg(addr) & ~mask);
}

iram static inline void set_peri_reg_mask(volatile uint32_t addr, uint32_t mask)
{
	write_peri_reg(addr, read_peri_reg(addr) | mask);
}

// read write GPIO registers

iram static inline uint32_t gpio_reg_read(int reg)
{
	return(read_peri_reg(PERIPHS_GPIO_BASEADDR + reg));
}

iram static inline void gpio_reg_write(int reg, uint32_t value)
{
	write_peri_reg(PERIPHS_GPIO_BASEADDR + reg, value);
}

// read input

iram static inline uint32_t gpio_get_mask(void)
{
	return(gpio_reg_read(GPIO_IN_ADDRESS));
}

iram static inline int gpio_get(int io)
{
	return((gpio_get_mask() & (1 << io)) ? 1 : 0);
}

// set output low / high

iram static inline void gpio_clear_mask(uint32_t mask)
{
	gpio_reg_write(GPIO_OUT_W1TC_ADDRESS, mask);
}

iram static inline void gpio_set_mask(uint32_t mask)
{
	gpio_reg_write(GPIO_OUT_W1TS_ADDRESS, mask);
}

iram static inline void gpio_set(int io, int onoff)
{
	if(onoff)
		gpio_set_mask(1 << io);
	else
		gpio_clear_mask(1 << io);
}

#endif
