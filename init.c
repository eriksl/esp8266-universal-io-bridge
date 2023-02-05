#include "init.h"

#include "util.h"
#include "io.h"
#include "stats.h"
#include "sys_time.h"
#include "dispatch.h"
#include "sequencer.h"
#include "wlan.h"

#include <stdint.h>

static void user_init2(void);
void user_init(void);

static const partition_item_t partition_items[] =
{
	{	SYSTEM_PARTITION_RF_CAL, 				RFCAL_OFFSET,				RFCAL_SIZE,				},
	{	SYSTEM_PARTITION_PHY_DATA,				PHYDATA_OFFSET,				PHYDATA_SIZE,			},
	{	SYSTEM_PARTITION_SYSTEM_PARAMETER,		SYSTEM_CONFIG_OFFSET,		SYSTEM_CONFIG_SIZE,		},
	{	SYSTEM_PARTITION_CUSTOMER_BEGIN + 0,	USER_CONFIG_OFFSET,			USER_CONFIG_SIZE,		},
	{	SYSTEM_PARTITION_CUSTOMER_BEGIN + 1,	OFFSET_BOOT,				SIZE_BOOT,				},
	{	SYSTEM_PARTITION_CUSTOMER_BEGIN + 2,	OFFSET_RBOOT_CFG,			SIZE_RBOOT_CFG,			},
	{	SYSTEM_PARTITION_CUSTOMER_BEGIN + 3,	OFFSET_IMG_0,				SIZE_IMG,				},
	{	SYSTEM_PARTITION_CUSTOMER_BEGIN + 4,	OFFSET_IMG_1,				SIZE_IMG,				},
	{	SYSTEM_PARTITION_CUSTOMER_BEGIN + 5,	SEQUENCER_FLASH_OFFSET_0,	SEQUENCER_FLASH_SIZE,	},
	{	SYSTEM_PARTITION_CUSTOMER_BEGIN + 6,	SEQUENCER_FLASH_OFFSET_1,	SEQUENCER_FLASH_SIZE,	},
	{	SYSTEM_PARTITION_CUSTOMER_BEGIN + 7,	PICTURE_FLASH_OFFSET_0,		PICTURE_FLASH_SIZE,		},
	{	SYSTEM_PARTITION_CUSTOMER_BEGIN + 8,	PICTURE_FLASH_OFFSET_1,		PICTURE_FLASH_SIZE,		},
	{	SYSTEM_PARTITION_CUSTOMER_BEGIN + 9,	FONT_FLASH_OFFSET_0,		FONT_FLASH_SIZE,		},
	{	SYSTEM_PARTITION_CUSTOMER_BEGIN + 10,	FONT_FLASH_OFFSET_1,		FONT_FLASH_SIZE,		},
	{	SYSTEM_PARTITION_CUSTOMER_BEGIN + 11,	MISC_FLASH_OFFSET,			MISC_FLASH_SIZE,		},
};

void user_spi_flash_dio_to_qio_pre_init(void);
iram void user_spi_flash_dio_to_qio_pre_init(void)
{
}

		volatile uint32_t	*stack_stack_sp_initial;
		int					stack_stack_painted;
static	volatile uint32_t	*stack_stack_paint_ptr; // this cannot be on the stack

iram void stack_paint_stack(void)
{
	// don't declare stack variables here, they will get overwritten

	volatile uint32_t sp;
	stack_stack_sp_initial = &sp;

	for(stack_stack_paint_ptr = (uint32_t *)stack_top; (stack_stack_paint_ptr < (uint32_t *)stack_bottom) && (stack_stack_paint_ptr < (volatile uint32_t *)stack_stack_sp_initial); stack_stack_paint_ptr++)
	{
		*stack_stack_paint_ptr = stack_paint_magic;
		stack_stack_painted += 4;
	}
}

void user_pre_init(void);
iram void user_pre_init(void)
{
	stat_flags.user_pre_init_called = 1;
	stat_flags.user_pre_init_success = system_partition_table_regist(partition_items, sizeof(partition_items) / sizeof(*partition_items), FLASH_SIZE_SDK);
	system_phy_set_powerup_option(3); /* request full calibration */
}

uint32_t user_iram_memory_is_enabled(void);
iram attr_const uint32_t user_iram_memory_is_enabled(void)
{
	return(0);
}

void user_init(void)
{
	stack_paint_stack();

	system_set_os_print(0);
	dispatch_init1();
	config_init();
	uart_init();
	uart_set_initial(0);
	uart_set_initial(1);
	os_install_putc1(&logchar);
	system_set_os_print(1);
	power_save_enable(config_flags_match(flag_wlan_power_save));
	wifi_station_ap_number_set(0);
	wifi_station_set_reconnect_policy(true);
	wifi_set_phy_mode(PHY_MODE_11G);
	system_init_done_cb(user_init2);
}

static void user_init2(void)
{
	stat_heap_min = stat_heap_max = xPortGetFreeHeapSize();

	dispatch_init2();

	if(config_flags_match(flag_cpu_high_speed))
		system_update_cpu_freq(160);
	else
		system_update_cpu_freq(80);

	if(!wlan_start())
		dispatch_post_task(1, task_wlan_recovery, 0);

	application_init();
	time_init();
	io_init();

	log("[system] boot done\n");

	if(config_flags_match(flag_auto_sequencer))
		sequencer_start(0, 1);
}
