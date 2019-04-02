#include "stats.h"

#include "util.h"
#include "config.h"
#include "time.h"
#include "i2c.h"
#include "i2c_sensor.h"
#include "rboot-interface.h"
#include "sdk.h"

stat_flags_t stat_flags;

int stat_uart0_rx_interrupts;
int stat_uart0_tx_interrupts;
int stat_uart1_tx_interrupts;
int stat_fast_timer;
int stat_slow_timer;
int stat_pwm_cycles;
int stat_timer_interrupts;
int stat_pwm_timer_interrupts;
int stat_pwm_timer_interrupts_while_nmi_masked;
int stat_pc_counts;
int stat_display_init_time_us;
int stat_cmd_receive_buffer_overflow;
int stat_cmd_send_buffer_overflow;
int stat_uart_receive_buffer_overflow;
int stat_uart_send_buffer_overflow;

int stat_update_uart;
int stat_update_command_udp;
int stat_update_command_tcp;
int stat_update_display;
int stat_update_ntp;

unsigned int stat_task_uart_posted;
unsigned int stat_task_uart_failed;
unsigned int stat_task_command_posted;
unsigned int stat_task_command_failed;
unsigned int stat_task_timer_posted;
unsigned int stat_task_timer_failed;

unsigned int stat_i2c_sda_stucks;
unsigned int stat_i2c_sda_stuck_max_period;
unsigned int stat_i2c_bus_locks;
unsigned int stat_i2c_bus_lock_max_period;
unsigned int stat_i2c_soft_resets;
unsigned int stat_i2c_hard_resets;

int stat_debug_1;
int stat_debug_2;
int stat_debug_3;

volatile uint32_t	*stat_stack_sp_initial;
int					stat_stack_painted;
unsigned int		stat_heap_min, stat_heap_max;

roflash static const char *const flash_map[] =
{
	"4 Mb map 256/256",
	"2 Mb no map",
	"8 Mb map 512/512",
	"16 Mb map 512/512",
	"32 Mb map 512/512",
	"16 Mb map 1024/1024",
	"32 Mb map 1024/1024",
	"unknown map",
	"unknown",
};

roflash static const char *const reset_map[] =
{
	"power on",
	"hardware watchdog",
	"exception",
	"software watchdog",
	"user reset",
	"deep sleep awake",
	"external system reset",
	"unknown1"
	"unknown2"
};

roflash static const char *const phy[] = {
	"unknown",
	"802.11b",
	"802.11g",
	"802.11n",
	"unknown"
};

roflash static const char *const slp[] =
{
	"none",
	"light",
	"modem",
	"unknown"
};

typedef struct
{
	attr_flash_align	uint32_t	id;
	attr_flash_align	const char *name;
} manufacturer_t;

assert_size(manufacturer_t, 8);

roflash static const manufacturer_t manufacturers[] =
{
	{	0xc8,	"GigaDevice"	},
	{	0xe0,	"Berg Micro"	},
	{	0xef,	"Winbond"		},
	{	0,		(const char *)0	}
};

attr_pure static const char *manufacturer_id_to_string(unsigned int id)
{
	const manufacturer_t *manufacturer;

	for(manufacturer = manufacturers; manufacturer->id && manufacturer->name; manufacturer++)
		if(manufacturer->id == id)
			return(manufacturer->name);

	return("unknown");
}

void stats_firmware(string_t *dst)
{
	const struct rst_info *rst_info;
	uint32_t flash_id = spi_flash_get_id();
	unsigned int flash_manufacturer_id	= (flash_id & 0x000000ff) >> 0;
	unsigned int flash_speed			= (flash_id & 0x0000ff00) >> 8;
	unsigned int flash_size				= (flash_id & 0x00ff0000) >> 16;
	unsigned int stack_size = stack_bottom - stack_top;
	int stack_used = -1; // no painted words found, overflow
	int stack_free = -1;
	unsigned int current_partition;
	unsigned int heap;
	partition_item_t partition_item;
	uint32_t *sp;

	for(sp = (typeof(sp))stack_top; sp < (typeof(sp))stack_bottom; sp++)
		if(*sp != stack_paint_magic)
			break;

	if(sp != (typeof(sp))stack_top)
	{
		stack_free = (unsigned int)sp - stack_top;
		stack_used = stack_bottom - (unsigned int)sp;
	}

	heap = system_get_free_heap_size();

	if(heap < stat_heap_min)
		stat_heap_min = heap;

	if(heap > stat_heap_max)
		stat_heap_max = heap;

	rst_info = system_get_rst_info();

	string_format(dst,
			"> firmware version date: %s\n"
			"> SDK version: %s\n"
			"> system id: %lu\n"
			"> spi flash id: %08x, manufacturer: %s, speed: %02x MHz, size: %d kib / %d MiB\n"
			"> cpu frequency: %u MHz\n"
			"> reset cause: %s, exception: %lu, epc1: %lx, epc2: %lx, epc3: %lx, excvaddr: %lx, depc: %lx\n"
			"> heap free current: %u, min: %u, max: %u bytes\n"
			">\n"
			"> stack:\n"
			">   bottom: %p\n"
			">   top: %p\n"
			">   initial stack pointer: %p (%d bytes)\n"
			">   current stack pointer: %p (%d bytes)\n"
			">   painted: %d bytes\n"
			">   not painted: %u bytes\n"
			">   size: %u bytes\n"
			">   used: %d bytes\n"
			">   free: %d bytes\n",
				__DATE__ " " __TIME__,
				system_get_sdk_version(),
				system_get_chip_id(),
				(unsigned int)flash_id, manufacturer_id_to_string(flash_manufacturer_id), flash_speed, 1 << (flash_size - 10), 1 << (flash_size - 17),
				system_get_cpu_freq(),
				reset_map[rst_info->reason], rst_info->exccause, rst_info->epc1, rst_info->epc2, rst_info->epc3, rst_info->excvaddr, rst_info->depc,
				heap, stat_heap_min, stat_heap_max,
				(void *)stack_bottom,
				(void *)stack_top,
				stat_stack_sp_initial, (typeof(stat_stack_sp_initial))stack_bottom - stat_stack_sp_initial,
				&sp, (typeof(&sp))stack_bottom - &sp,
				stat_stack_painted,
				stack_size - stat_stack_painted,
				stack_size,
				stack_used,
				stack_free);

	system_print_meminfo();

#if IMAGE_OTA == 1
	rboot_if_info(dst);
#else
	string_append(dst, ">\n> No OTA image\n");
#endif
	string_append(dst, ">\n> flash partition table:\n");
	string_format(dst, ">   map: %s\n", flash_map[system_get_flash_size_map()]);

	for(current_partition = 0; current_partition < SYSTEM_PARTITION_CUSTOMER_BEGIN + 16; current_partition++)
	{
		if(system_partition_get_item((partition_type_t)current_partition, &partition_item))
		{
			string_append(dst, ">     ");

			switch(current_partition)
			{
				case(SYSTEM_PARTITION_INVALID):							string_append(dst, "invalid partition"); break;
				case(SYSTEM_PARTITION_BOOTLOADER):						string_append(dst, "bootloader       "); break;
				case(SYSTEM_PARTITION_OTA_1):							string_append(dst, "SDK OTA 1        "); break;
				case(SYSTEM_PARTITION_OTA_2):							string_append(dst, "SDK OTA 2        "); break;
				case(SYSTEM_PARTITION_RF_CAL):							string_append(dst, "RF calibration   "); break;
				case(SYSTEM_PARTITION_PHY_DATA):						string_append(dst, "PHY config       "); break;
				case(SYSTEM_PARTITION_SYSTEM_PARAMETER):				string_append(dst, "SYSTEM config    "); break;
				case(SYSTEM_PARTITION_AT_PARAMETER):					string_append(dst, "AT parameters    "); break;
				case(SYSTEM_PARTITION_SSL_CLIENT_CERT_PRIVKEY):			string_append(dst, "SSL client key   "); break;
				case(SYSTEM_PARTITION_SSL_CLIENT_CA):					string_append(dst, "SSL client ca    "); break;
				case(SYSTEM_PARTITION_SSL_SERVER_CERT_PRIVKEY):			string_append(dst, "SSL server key   "); break;
				case(SYSTEM_PARTITION_SSL_SERVER_CA):					string_append(dst, "SSL server ca    "); break;
				case(SYSTEM_PARTITION_WPA2_ENTERPRISE_CERT_PRIVKEY):	string_append(dst, "EAP privkey      "); break;
				case(SYSTEM_PARTITION_WPA2_ENTERPRISE_CA):				string_append(dst, "EAP ca           "); break;
				case(SYSTEM_PARTITION_CUSTOMER_BEGIN+0):				string_append(dst, "USER config      "); break;
#if IMAGE_OTA == 0
				case(SYSTEM_PARTITION_CUSTOMER_BEGIN+1):				string_append(dst, "PLAIN image IRAM "); break;
				case(SYSTEM_PARTITION_CUSTOMER_BEGIN+2):				string_append(dst, "PLAIN image IROM "); break;
				case(SYSTEM_PARTITION_CUSTOMER_BEGIN+3):				string_append(dst, "sequencer        "); break;
#else
				case(SYSTEM_PARTITION_CUSTOMER_BEGIN+1):				string_append(dst, "RBOOT OTA boot   "); break;
				case(SYSTEM_PARTITION_CUSTOMER_BEGIN+2):				string_append(dst, "RBOOT OTA config "); break;
				case(SYSTEM_PARTITION_CUSTOMER_BEGIN+3):				string_append(dst, "OTA image slot 0 "); break;
				case(SYSTEM_PARTITION_CUSTOMER_BEGIN+4):				string_append(dst, "OTA image slot 1 "); break;
				case(SYSTEM_PARTITION_CUSTOMER_BEGIN+5):				string_append(dst, "sequencer slot 0 "); break;
				case(SYSTEM_PARTITION_CUSTOMER_BEGIN+6):				string_append(dst, "sequencer slot 1 "); break;
#endif
				default:												string_append(dst, "unknown partition"); break;
			}

			string_format(dst, " start: 0x%06lx size: %3lu kB\n", (unsigned long int)partition_item.addr, partition_item.size / 1024);
		}
	}
}

void stats_time(string_t *dst)
{
	unsigned int Y, M, D, h, m, s;
	const char *time_source;

	time_source = time_get(&h, &m, &s, &Y, &M, &D);

	string_format(dst, "> uptime: %s\n", string_to_cstr(time_uptime_stats()));
	string_format(dst, "> system: %s\n", string_to_cstr(time_system_stats()));
	string_format(dst, "> rtc:    %s\n", string_to_cstr(time_rtc_stats()));
	string_format(dst, "> timer:  %s\n", string_to_cstr(time_timer_stats()));
	string_format(dst, "> ntp:    %s\n", string_to_cstr(time_ntp_stats()));
	string_format(dst, "> time:   %04u/%02u/%02u %02u:%02u:%02u, source: %s\n", Y, M, D, h, m, s, time_source);
}

void stats_counters(string_t *dst)
{
	string_format(dst,
			"> user_pre_init called: %s\n"
			"> user_pre_init success: %s\n"
			"> int uart0 rx: %d\n"
			"> int uart0 tx: %d\n"
			"> int uart1 tx: %d\n"
			"> fast timer fired: %d\n"
			"> slow timer fired: %d\n"
			"> primary pwm cycles: %d\n"
			"> ... int fired: %d\n"
			"> ... while masked: %d\n"
			"> pc counts: %d\n"
			"> uart updated: %d\n"
			"> commands/udp processed: %d\n"
			"> commands/tcp processed: %d\n"
			"> display updated: %d\n"
			"> ntp updated: %d\n"
			"> cmd receive buffer overflow events: %d\n"
			"> cmd send buffer overflow events: %d\n"
			"> uart receive buffer overflow events: %d\n"
			"> uart send buffer overflow events: %d\n"
			"> task uart posted: %u\n"
			"> task uart failed: %u\n"
			"> task command posted: %u\n"
			"> task command failed: %u\n"
			"> task timer posted: %u\n"
			"> task timer failed: %u\n"
			"> debug counter 1: 0x%08x %d\n"
			"> debug counter 2: 0x%08x %d\n"
			"> debug counter 3: 0x%08x %d\n",
				yesno(stat_flags.user_pre_init_called),
				yesno(stat_flags.user_pre_init_success),
				stat_uart0_rx_interrupts,
				stat_uart0_tx_interrupts,
				stat_uart1_tx_interrupts,
				stat_fast_timer,
				stat_slow_timer,
				stat_pwm_cycles,
				stat_pwm_timer_interrupts,
				stat_pwm_timer_interrupts_while_nmi_masked,
				stat_pc_counts,
				stat_update_uart,
				stat_update_command_udp,
				stat_update_command_tcp,
				stat_update_display,
				stat_update_ntp,
				stat_cmd_receive_buffer_overflow,
				stat_cmd_send_buffer_overflow,
				stat_uart_receive_buffer_overflow,
				stat_uart_send_buffer_overflow,
				stat_task_uart_posted,
				stat_task_uart_failed,
				stat_task_command_posted,
				stat_task_command_failed,
				stat_task_timer_posted,
				stat_task_timer_failed,
				(unsigned int)stat_debug_1,
				stat_debug_1,
				(unsigned int)stat_debug_2,
				stat_debug_2,
				(unsigned int)stat_debug_3,
				stat_debug_3);
}

void stats_i2c(string_t *dst)
{
	i2c_info_t			i2c_info;
	i2c_sensor_info_t	i2c_sensor_info;

	i2c_get_info(&i2c_info);
	i2c_sensor_get_info(&i2c_sensor_info);

	string_format(dst,
			"> display initialisation time: %d ms\n"
			"> i2c sda stucks: %u\n"
			"> i2c sda max stuck periods: %u\n"
			"> i2c bus locks: %u\n"
			"> i2c bus max locked periods: %u\n"
			"> i2c soft resets: %u\n"
			"> i2c hard resets: %u\n"
			"> i2c multiplexer found: %s\n"
			"> i2c buses: %d\n"
			"> i2c sensors init called: %u\n"
			"> i2c sensors init succeeded: %u\n"
			"> i2c sensors init skip disabled: %u (%u)\n"
			"> i2c sensors init skip secondary: %u (%u)\n"
			"> i2c sensors init skip found on bus 0: %u\n"
			"> i2c sensors init skip dup address: %u\n"
			"> i2c sensors init failed: %u\n"
			"> i2c sensors init current bus: %u\n"
			"> i2c sensors init current sensor id: %u\n"
			"> i2c sensors init finished: %s\n"
			"> i2c sensors init duration: %lu ms\n",
				stat_display_init_time_us / 1000,
				stat_i2c_sda_stucks,
				stat_i2c_sda_stuck_max_period,
				stat_i2c_bus_locks,
				stat_i2c_bus_lock_max_period,
				stat_i2c_soft_resets,
				stat_i2c_hard_resets,
				yesno(i2c_info.multiplexer),
				i2c_info.buses,
				i2c_sensor_info.init_called,
				i2c_sensor_info.init_succeeded,
				i2c_sensor_info.init_skip_disabled,
				i2c_sensor_info.init_skip_disabled / i2c_info.buses,
				i2c_sensor_info.init_skip_secondary,
				i2c_sensor_info.init_skip_secondary / i2c_info.buses,
				i2c_sensor_info.init_skip_found_on_bus_0,
				i2c_sensor_info.init_skip_duplicate_address,
				i2c_sensor_info.init_failed,
				i2c_sensor_info.init_current_bus,
				i2c_sensor_info.init_current_sensor,
				yesno(i2c_sensor_info.init_finished),
				(uint32_t)((i2c_sensor_info.init_finished_us - i2c_sensor_info.init_started_us) / 1000));
}

void stats_wlan(string_t *dst)
{
	mac_addr_t mac_addr;
	struct ip_info ip_addr_info;
	struct station_config config;

	wifi_station_get_config_default(&config);

	string_format(dst, "> default ssid: \"%s\", passwd: \"%s\"\n",
			config.ssid, config.password);

	wifi_station_get_config(&config);

	string_format(dst, "> current ssid: \"%s\", passwd: \"%s\"\n",
			config.ssid, config.password);

	string_format(dst,
			"> autoconnect: %s\n"
			"> phy mode: %s\n"
			"> sleep mode: %s\n"
			"> channel: %u\n"
			"> signal strength: %d dB\n"
			">\n",
				onoff(wifi_station_get_auto_connect()),
				phy[wifi_get_phy_mode()],
				slp[wifi_get_sleep_type()],
				wifi_get_channel(),
				wifi_station_get_rssi());

	wifi_get_ip_info(SOFTAP_IF, &ip_addr_info);

	string_append(dst, "> ap mac address: ");
	wifi_get_macaddr(SOFTAP_IF, mac_addr);
	string_mac(dst, mac_addr);
	string_append(dst, "\n");

	string_append(dst, "> ap ip address: ");
	string_ip(dst, ip_addr_info.ip);
	string_append(dst, "\n");

	string_append(dst, "> ap gateway: ");
	string_ip(dst, ip_addr_info.gw);
	string_append(dst, "\n");

	string_append(dst, "> ap ip netmask: ");
	string_ip(dst, ip_addr_info.netmask);
	string_append(dst, "\n");

	string_append(dst, ">\n");

	wifi_get_ip_info(STATION_IF, &ip_addr_info);

	string_append(dst, "> station mac address: ");
	wifi_get_macaddr(STATION_IF, mac_addr);
	string_mac(dst, mac_addr);
	string_append(dst, "\n");

	string_append(dst, "> station ip address: ");
	string_ip(dst, ip_addr_info.ip);
	string_append(dst, "\n");

	string_append(dst, "> station gateway: ");
	string_ip(dst, ip_addr_info.gw);
	string_append(dst, "\n");

	string_append(dst, "> station ip netmask: ");
	string_ip(dst, ip_addr_info.netmask);
	string_append(dst, "\n");
}
