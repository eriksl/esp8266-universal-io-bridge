#include "stats.h"

#include "util.h"
#include "sys_string.h"
#include "config.h"
#include "sys_time.h"
#include "i2c.h"
#include "i2c_sensor.h"
#include "rboot-interface.h"
#include "sdk.h"

stat_flags_t stat_flags;

unsigned int stat_uart_spurious;
unsigned int stat_uart0_rx_interrupts;
unsigned int stat_uart0_tx_interrupts;
unsigned int stat_uart1_tx_interrupts;
unsigned int stat_fast_timer;
unsigned int stat_slow_timer;
unsigned int stat_pwm_cycles;
unsigned int stat_timer_interrupts;
unsigned int stat_pwm_timer_interrupts;
unsigned int stat_pwm_timer_interrupts_while_nmi_masked;
unsigned int stat_pc_counts;
unsigned int stat_cmd_receive_buffer_overflow;
unsigned int stat_cmd_send_buffer_overflow;
unsigned int stat_uart_receive_buffer_overflow;
unsigned int stat_uart_send_buffer_overflow;
unsigned int stat_update_uart;
unsigned int stat_update_command_udp;
unsigned int stat_update_command_tcp;
unsigned int stat_update_command_uart;
unsigned int stat_update_display;
unsigned int stat_task_posted[3];
unsigned int stat_task_executed[3];
unsigned int stat_task_post_failed[3];
unsigned int stat_task_current_queue[3];
unsigned int stat_task_max_queue[3];
unsigned int stat_config_read_requests;
unsigned int stat_config_read_loads;
unsigned int stat_config_write_requests;
unsigned int stat_config_write_saved;
unsigned int stat_config_write_aborted;
unsigned int stat_lwip_tcp_send_segmentation;
unsigned int stat_lwip_tcp_send_error;
unsigned int stat_lwip_udp_send_error;
unsigned int stat_lwip_tcp_received_packets;
unsigned int stat_lwip_tcp_received_bytes;
unsigned int stat_lwip_tcp_sent_packets;
unsigned int stat_lwip_tcp_sent_bytes;
unsigned int stat_lwip_udp_received_packets;
unsigned int stat_lwip_udp_received_bytes;
unsigned int stat_lwip_udp_sent_packets;
unsigned int stat_lwip_udp_sent_bytes;
unsigned int stat_init_display_time_us;
unsigned int stat_init_io_time_us;
unsigned int stat_init_associate_time_us;
unsigned int stat_init_ip_time_us;

unsigned int stat_i2c_sda_stucks;
unsigned int stat_i2c_sda_stuck_max_period;
unsigned int stat_i2c_bus_locks;
unsigned int stat_i2c_bus_lock_max_period;
unsigned int stat_i2c_soft_resets;
unsigned int stat_i2c_hard_resets;

unsigned int stat_display_update_min_us = ~0UL;
unsigned int stat_display_update_max_us;

unsigned int stat_sntp_received;
unsigned int stat_sntp_poll;

unsigned int stat_spi_slave_interrupts;
unsigned int stat_spi_slave_i2s_interrupts;

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

static void explain_exception(string_t *dst)
{
	const struct rst_info *rst_info;
	const char *exception;

	rst_info = system_get_rst_info();

	switch(rst_info->reason)
	{
		case(2):
		{
			switch(rst_info->exccause)
			{
				case(0):	exception = "illegal instruction";			break;
				case(1):	exception = "syscall";						break;
				case(2):	exception = "instruction fetch failed";		break;
				case(3):	exception = "data read/write failure";		break;
				case(4):	exception = "level 1 interrupt";			break;
				case(6):	exception = "division by zero";				break;
				case(9):	exception = "unaligned memory access";		break;
				case(28):	exception = "load prohibited";				break;
				case(29):	exception = "store prohibited";				break;
				default:	exception = "<unknown>";					break;
			}

			string_format(dst, "exception: %s (%lu), epc1: %lx, epc2: %lx, epc3: %lx, excvaddr: %lx, depc: %lx\n",
					exception, rst_info->exccause, rst_info->epc1, rst_info->epc2, rst_info->epc3, rst_info->excvaddr, rst_info->depc);

			break;
		}

		case(0):	string_append(dst, "power on reset");			break;
		case(1):	string_append(dst, "hardware watchdog reset");	break;
		case(3):	string_append(dst, "software watchdog reset");	break;
		case(4):	string_append(dst, "user reset");				break;
		case(5):	string_append(dst, "deep sleep wake");			break;
		case(6):	string_append(dst, "external system reset");	break;
		default:	string_append(dst, "<unknown>\n");				break;
	}
}

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
	unsigned int stack_size = stack_bottom - stack_top;
	int stack_used = -1; // no painted words found, overflow
	int stack_free = -1;
	unsigned int heap;
	uint32_t *sp;

	for(sp = (typeof(sp))stack_top; sp < (typeof(sp))stack_bottom; sp++)
		if(*sp != stack_paint_magic)
			break;

	if(sp != (typeof(sp))stack_top)
	{
		stack_free = (unsigned int)sp - stack_top;
		stack_used = stack_bottom - (unsigned int)sp;
	}

	heap = xPortGetFreeHeapSize();

	if(heap < stat_heap_min)
		stat_heap_min = heap;

	if(heap > stat_heap_max)
		stat_heap_max = heap;

	string_format(dst,
			"> firmware version date: %s\n"
			"> SDK version: %s\n"
			"> system id: %lu\n"
			"> cpu frequency: %u MHz\n"
			"> heap free current: %u, min: %u, max: %u bytes\n"
			">\n"
			"> stack:\n"
			">   bottom: %p, top: %p\n"
			">   initial: %p (%lu bytes), current: %p (%lu bytes)\n"
			">   painted: %d bytes, not painted: %u bytes\n"
			">   size: %u bytes, used: %d bytes, free: %d bytes\n",
				__DATE__ " " __TIME__,
				system_get_sdk_version(),
				system_get_chip_id(),
				system_get_cpu_freq(),
				heap, stat_heap_min, stat_heap_max,
				(void *)stack_bottom, (void *)stack_top,
				stat_stack_sp_initial, (uint32_t)stack_bottom - (uint32_t)stat_stack_sp_initial, &sp, (uint32_t)stack_bottom - (uint32_t)&sp,
				stat_stack_painted, stack_size - stat_stack_painted,
				stack_size, stack_used, stack_free);

	string_append(dst, ">\n> reset cause: ");
	explain_exception(dst);
	string_append(dst, "\n");
}

void stats_flash(string_t *dst)
{
	uint32_t flash_id = spi_flash_get_id();
	unsigned int flash_manufacturer_id	= (flash_id & 0x000000ff) >> 0;
	unsigned int flash_speed			= (flash_id & 0x0000ff00) >> 8;
	unsigned int flash_size				= (flash_id & 0x00ff0000) >> 16;
	unsigned int current_partition;
	partition_item_t partition_item;

	string_format(dst,
			"> spi flash id: %08x, manufacturer: %s, speed: %02x MHz, size: %d kib / %d MiB\n",
				(unsigned int)flash_id, manufacturer_id_to_string(flash_manufacturer_id), flash_speed, 1 << (flash_size - 10), 1 << (flash_size - 17));

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
				case(SYSTEM_PARTITION_CUSTOMER_BEGIN+7):				string_append(dst, "picture slot 0   "); break;
				case(SYSTEM_PARTITION_CUSTOMER_BEGIN+8):				string_append(dst, "picture slot 1   "); break;
				case(SYSTEM_PARTITION_CUSTOMER_BEGIN+9):				string_append(dst, "font slot 0      "); break;
				case(SYSTEM_PARTITION_CUSTOMER_BEGIN+10):				string_append(dst, "font slot 1      "); break;
				case(SYSTEM_PARTITION_CUSTOMER_BEGIN+11):				string_append(dst, "spare space      "); break;
#endif
				default:												string_append(dst, "unknown partition"); break;
			}

			string_format(dst, " start: 0x%06lx, end: 0x%06lx, size: %4lu kB\n",
					(unsigned long int)partition_item.addr,
					(unsigned long int)(partition_item.addr + partition_item.size - 1),
					partition_item.size / 1024);
		}
	}
}

void stats_time(string_t *dst)
{
	unsigned int Y, M, D, h, m, s;
	const char *time_source;

	time_source = time_get(&h, &m, &s, &Y, &M, &D);

	string_append(dst,   "> uptime: ");
	time_uptime_stats(dst);
	string_append(dst, "\n> system: ");
	time_system_stats(dst);
	string_append(dst, "\n> rtc:    ");
	time_rtc_stats(dst);
	string_append(dst, "\n> timer:  ");
	time_timer_stats(dst);
	string_append(dst, "\n> ntp:    ");
	time_sntp_stats(dst);
	string_append(dst, "\n");
	string_format(dst, "> time:   %04u/%02u/%02u %02u:%02u:%02u, source: %s\n", Y, M, D, h, m, s, time_source);
	string_format(dst, "> sntp:   poll: %u, received: %u\n", stat_sntp_poll, stat_sntp_received);
}

void stats_counters(string_t *dst)
{
	string_format(dst,
			"> INTERRUPTS\n"
			">   uart spurious: %8u\n"
			">   uart0 rx:      %8u, tx: %u\n"
			">   uart1 tx:      %8u\n"
			">   nonmaskable:   %u, while masked: %u\n"
			">   spi slave/i2s: %u, i2s: %u\n",
				stat_uart_spurious,
				stat_uart0_rx_interrupts, stat_uart0_tx_interrupts,
				stat_uart1_tx_interrupts,
				stat_pwm_timer_interrupts, stat_pwm_timer_interrupts_while_nmi_masked,
				stat_spi_slave_interrupts, stat_spi_slave_i2s_interrupts);

	string_format(dst,
			">\n> TIMERS\n"
			">   fast: %u, slow: %u\n",
				stat_fast_timer, stat_slow_timer);

	string_format(dst,
			">\n> TASKS\n");

	unsigned int prio;

	for(prio = 0; prio < 3; prio++)
		string_format(dst,
			">  prio %u posted: %8u, post failed: %3u, executed: %8u, max queue size: %u\n",
				prio, stat_task_posted[prio], stat_task_post_failed[prio], stat_task_executed[prio], stat_task_max_queue[prio]);

	string_format(dst,
			">\n> COMMANDS PROCESSED\n"
			">  udp: %u, tcp: %u, uart: %u\n",
				stat_update_command_udp, stat_update_command_tcp, stat_update_command_uart);

	string_format(dst,
			">\n> BUFFER OVERFLOWS\n"
			">  cmd receive:  %4u, send: %u\n"
			">  uart receive: %4u, send: %u\n",
				stat_cmd_receive_buffer_overflow, stat_cmd_send_buffer_overflow,
				stat_uart_receive_buffer_overflow, stat_uart_send_buffer_overflow);

	string_format(dst,
			">\n> CONFIG\n"
			">  read requests: %u\n"
			">  loads: %u\n"
			">  write requests: %u, succeeded: %u, aborted: %u\n",
				stat_config_read_requests,
				stat_config_read_loads,
				stat_config_write_requests, stat_config_write_saved, stat_config_write_aborted);

	string_format(dst,
			">\n> LWIP\n"
			">  udp received packets: %6u, bytes: %u\n"
			">  tcp received packets: %6u, bytes: %u\n"
			">  udp sent     packets: %6u, bytes: %u\n"
			">  tcp sent     packets: %6u, bytes: %u\n"
			">  tcp send segmentation events: %u\n"
			">  tcp error events: %u\n"
			">  udp send error events: %u\n",
				stat_lwip_udp_received_packets,
				stat_lwip_udp_received_bytes,
				stat_lwip_tcp_received_packets,
				stat_lwip_tcp_received_bytes,
				stat_lwip_udp_sent_packets,
				stat_lwip_udp_sent_bytes,
				stat_lwip_tcp_sent_packets,
				stat_lwip_tcp_sent_bytes,
				stat_lwip_tcp_send_segmentation,
				stat_lwip_tcp_send_error,
				stat_lwip_udp_send_error);

	string_format(dst,
			">\n> INIT TIME\n"
			">  io: %u ms, display: %u ms\n"
			">  associate: %u ms, obtain ip address: %u ms\n",
				stat_init_io_time_us / 1000, stat_init_display_time_us / 1000,
				stat_init_associate_time_us / 1000, stat_init_ip_time_us / 1000);

	string_format(dst,
			">\n> MISCELLANUOUS\n"
			">  pin change counts:   %u\n"
			">  display updated:     %u\n"
			">  primary PWM cycles:  %u\n"
			">  uart data processed: %u\n",
				stat_pc_counts,
				stat_update_display,
				stat_pwm_cycles,
				stat_update_uart);

	string_format(dst,
			">\n> DEBUG COUNTERS\n"
			">  1: 0x%08x %d\n"
			">  2: 0x%08x %d\n"
			">  3: 0x%08x %d\n",
				(unsigned int)stat_debug_1, stat_debug_1,
				(unsigned int)stat_debug_2, stat_debug_2,
				(unsigned int)stat_debug_3, stat_debug_3);

	system_print_meminfo();
}

void stats_i2c(string_t *dst)
{
	i2c_info_t			i2c_info;
	i2c_sensor_info_t	i2c_sensor_info;

	i2c_get_info(&i2c_info);
	i2c_sensor_get_info(&i2c_sensor_info);

	string_format(dst,
			"> i2c sda stucks: %u\n"
			"> i2c sda max stuck periods: %u\n"
			"> i2c bus locks: %u\n"
			"> i2c bus max locked periods: %u\n"
			"> i2c soft resets: %u\n"
			"> i2c hard resets: %u\n"
			"> i2c multiplexer found: %s\n"
			"> i2c buses: %d\n",
				stat_i2c_sda_stucks,
				stat_i2c_sda_stuck_max_period,
				stat_i2c_bus_locks,
				stat_i2c_bus_lock_max_period,
				stat_i2c_soft_resets,
				stat_i2c_hard_resets,
				yesno(i2c_info.multiplexer),
				i2c_info.buses);

	string_format(dst,
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

	string_format(dst,
			"> i2c sensors periodic called: %u\n"
			"> i2c sensors periodic sensor called: %u\n"
			"> i2c sensors periodic wrapped: %u\n"
			"> i2c sensors periodic current bus: %u\n"
			"> i2c sensors periodic current sensor: %u\n",
				i2c_sensor_info.periodic_called,
				i2c_sensor_info.periodic_sensor_called,
				i2c_sensor_info.periodic_wrapped,
				i2c_sensor_info.periodic_current_bus,
				i2c_sensor_info.periodic_current_sensor);
}

void stats_wlan(string_t *dst)
{
	sdk_mac_addr_t mac_addr;
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
