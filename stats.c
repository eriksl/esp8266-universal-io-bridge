#include "stats.h"

#include "util.h"
#include "init.h"
#include "config.h"
#include "sys_time.h"
#include "i2c.h"
#include "i2c_sensor.h"
#include "rboot-interface.h"
#include "sdk.h"
#include "lwip-interface.h"

stat_flags_t stat_flags;
stat_uart_t stat_uart;

unsigned int stat_uart_spurious;
unsigned int stat_fast_timer;
unsigned int stat_slow_timer;
unsigned int stat_pwm_cycles;
unsigned int stat_timer_interrupts;
unsigned int stat_pwm_timer_interrupts;
unsigned int stat_pwm_timer_interrupts_while_nmi_masked;
unsigned int stat_pc_counts;
unsigned int stat_renc_invalid_state;
unsigned int stat_uart_receive_buffer_overflow;
unsigned int stat_uart_send_buffer_overflow;
unsigned int stat_update_uart;
unsigned int stat_update_display;
unsigned int stat_cmd_udp;
unsigned int stat_cmd_tcp;
unsigned int stat_cmd_uart;
unsigned int stat_cmd_receive_buffer_overflow;
unsigned int stat_cmd_send_buffer_overflow;
unsigned int stat_cmd_udp_packet_incomplete;
unsigned int stat_cmd_tcp_too_many_segments;
unsigned int stat_cmd_invalid_packet_length;
unsigned int stat_cmd_timeout;
unsigned int stat_cmd_checksum_error;
unsigned int stat_cmd_duplicate;
unsigned int stat_display_picture_load_worker_called;
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
unsigned int stat_lwip_tcp_send_error;
unsigned int stat_lwip_udp_send_error;
unsigned int stat_lwip_tcp_received_packets;
unsigned int stat_lwip_udp_received_packets;
unsigned int stat_lwip_tcp_received_bytes;
unsigned int stat_lwip_udp_received_bytes;
unsigned int stat_lwip_tcp_sent_packets;
unsigned int stat_lwip_udp_sent_packets;
unsigned int stat_lwip_tcp_sent_bytes;
unsigned int stat_lwip_udp_sent_bytes;
unsigned int stat_lwip_tcp_locked;
unsigned int stat_lwip_udp_locked;
unsigned int stat_lwip_tcp_send_segmentation;
unsigned int stat_lwip_unicast_received;
unsigned int stat_lwip_broadcast_received;
unsigned int stat_lwip_multicast_received;
unsigned int stat_lwip_broadcast_dropped;
unsigned int stat_lwip_multicast_dropped;
unsigned int stat_lwip_unicast_dropped;
unsigned int stat_broadcast_group_received;
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

unsigned int stat_spi_slave_interrupts;
unsigned int stat_spi_slave_i2s_interrupts;
unsigned int stat_spi_wait_cycles;
unsigned int stat_spi_largest_chunk;
unsigned int stat_spi_8;
unsigned int stat_spi_16;

unsigned int stat_font_render_time;

int stat_debug_1;
int stat_debug_2;
int stat_debug_3;

unsigned int stat_heap_min, stat_heap_max;

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

			string_format(dst, "exception: %s (%u), epc1: %x, epc2: %x, epc3: %x, excvaddr: %x, depc: %x\n",
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
	roflash static const char git_commit[] = GIT_COMMIT;

	int32_t stack_size = stack_bottom - stack_top;
	int32_t stack_used = -1;
	int32_t stack_free = -1;
	unsigned int heap;
	uint32_t *sp;

	if(stack_stack_painted > 0)
	{
		for(sp = (uint32_t *)stack_top; (uint32_t)sp < stack_bottom; sp++)
			if(*sp != stack_paint_magic)
				break;

		if((uint32_t)sp != stack_top)
		{
			stack_free = (int32_t)sp - stack_top;
			stack_used = stack_bottom - (int32_t)sp;
		}
	}

	heap = xPortGetFreeHeapSize();

	if(heap < stat_heap_min)
		stat_heap_min = heap;

	if(heap > stat_heap_max)
		stat_heap_max = heap;

	string_format(dst,
			"> firmware\n"
			">   date: %s %s\n"
			">   git commit: %s\n"
			">\n"
			"> heap:\n"
			">   free: current: %u, min: %u, max: %u\n"
			">\n"
			"> stack:\n"
			">   bottom: %p (%d kiB), top: %p (%d kiB), initial: %p (%d), current: %p (%d)\n"
			">   size: %d, painted: %d, not painted: %d\n"
			">   currently used: %d/%d%%, max used: %d/%d%%, unused: %d/%d%%\n"
			">\n"
			"> system\n"
			">   id: %02x:%02x:%02x\n"
			">   cpu frequency: %u MHz\n"
			">   SDK version: %s\n",
				__DATE__, __TIME__,
				git_commit,
				heap, stat_heap_min, stat_heap_max,
					(void *)stack_bottom, stack_bottom >> 10,
					(void *)stack_top, stack_top >> 10,
					stack_stack_sp_initial, stack_bottom - (int32_t)stack_stack_sp_initial,
					&sp, stack_bottom - (int32_t)&sp,
				stack_size, stack_stack_painted, stack_size - stack_stack_painted,
					stack_bottom - (int32_t)&sp, ((stack_bottom - (int32_t)&sp) * 100) / stack_size,
					stack_used, (stack_used * 100) / stack_size,
					stack_free, (stack_free * 100) / stack_size,
				(system_get_chip_id() & 0x00ff0000) >> 16,
				(system_get_chip_id() & 0x0000ff00) >>  8,
				(system_get_chip_id() & 0x000000ff) >>  0,
				system_get_cpu_freq(),
				system_get_sdk_version());

	string_append(dst, ">   reset cause: ");
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

	rboot_if_info(dst);
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
				case(SYSTEM_PARTITION_CUSTOMER_BEGIN+1):				string_append(dst, "RBOOT boot       "); break;
				case(SYSTEM_PARTITION_CUSTOMER_BEGIN+2):				string_append(dst, "RBOOT config     "); break;
				case(SYSTEM_PARTITION_CUSTOMER_BEGIN+3):				string_append(dst, "image slot 0     "); break;
				case(SYSTEM_PARTITION_CUSTOMER_BEGIN+4):				string_append(dst, "image slot 1     "); break;
				case(SYSTEM_PARTITION_CUSTOMER_BEGIN+5):				string_append(dst, "sequencer slot 0 "); break;
				case(SYSTEM_PARTITION_CUSTOMER_BEGIN+6):				string_append(dst, "sequencer slot 1 "); break;
				case(SYSTEM_PARTITION_CUSTOMER_BEGIN+7):				string_append(dst, "picture slot 0   "); break;
				case(SYSTEM_PARTITION_CUSTOMER_BEGIN+8):				string_append(dst, "picture slot 1   "); break;
				case(SYSTEM_PARTITION_CUSTOMER_BEGIN+9):				string_append(dst, "font slot 0      "); break;
				case(SYSTEM_PARTITION_CUSTOMER_BEGIN+10):				string_append(dst, "font slot 1      "); break;
				case(SYSTEM_PARTITION_CUSTOMER_BEGIN+11):				string_append(dst, "spare space      "); break;
				default:												string_append(dst, "unknown partition"); break;
			}

			string_format(dst, " 0x%06x-0x%06x [%4u kB]\n",
					(uint32_t)partition_item.addr,
					(uint32_t)(partition_item.addr + partition_item.size - 1),
					partition_item.size / 1024);
		}
	}
}

void stats_time(string_t *dst)
{
	time_stats(dst);
}

void stats_counters(string_t *dst)
{
	string_format(dst,
			"> INTERRUPTS\n"
			">   nonmaskable:   %u, while masked: %u\n"
			">   spi slave/i2s: %u, i2s: %u\n",
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
			">  udp: %u, tcp: %u, uart: %u\n"
			">  timeouts: %u, checksum errors: %u, duplicates: %u\n"
			">  ip receive buffer overflows: %u, send buffer overflows: %u, incomplete packets: %u, too many segments: %u, invalid length: %u\n"
			">  uart receive overflows: %u, uart send overflows: %u\n",
				stat_cmd_udp, stat_cmd_tcp, stat_cmd_uart,
				stat_cmd_timeout, stat_cmd_checksum_error, stat_cmd_duplicate,
				stat_cmd_receive_buffer_overflow, stat_cmd_send_buffer_overflow, stat_cmd_udp_packet_incomplete, stat_cmd_tcp_too_many_segments, stat_cmd_invalid_packet_length,
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
			">\n> INIT TIME\n"
			">  io: %u ms, display: %u ms\n"
			">  associate: %u ms, obtain ip address: %u ms\n",
				stat_init_io_time_us / 1000, stat_init_display_time_us / 1000,
				stat_init_associate_time_us / 1000, stat_init_ip_time_us / 1000);

	string_format(dst,
			">\n> MISCELLANUOUS\n"
			">  gpio pin change counts: %u\n"
			">  invalid encoder states: %u\n"
			">  display updated:        %u\n"
			">  picture load worker:    %u\n"
			">  primary PWM cycles:     %u\n"
			">  uart data processed:    %u\n"
			">  spi wait cycles:        %u\n"
			">  spi max chunk size:     %u\n"
			">  write_8 used:           %u\n"
			">  write_16 used:          %u\n",
				stat_pc_counts,
				stat_renc_invalid_state,
				stat_update_display,
				stat_display_picture_load_worker_called,
				stat_pwm_cycles,
				stat_update_uart,
				stat_spi_wait_cycles,
				stat_spi_largest_chunk / 8,
				stat_spi_8, stat_spi_16);

	string_format(dst,
			">\n> DEBUG COUNTERS\n"
			">  1: 0x%08x %d\n"
			">  2: 0x%08x %d\n"
			">  3: 0x%08x %d\n",
				(unsigned int)stat_debug_1, stat_debug_1,
				(unsigned int)stat_debug_2, stat_debug_2,
				(unsigned int)stat_debug_3, stat_debug_3);

	string_format(dst,
			">\n> FONT\n"
			">  max font render time: %u usec\n", stat_font_render_time);

	system_print_meminfo();
}

void stats_uart(string_t *dst)
{
	unsigned int ix;

	string_format(dst,
			"> * spurious interrupts: %u\n", stat_uart_spurious);

	for(ix = 0; ix < uarts; ix++)
		string_format(dst,
			"> %u rx: %8u, tx: %8u, RX posted: %4u, max: %4u, skipped: %4u, TX posted: %4u, max: %4u, skipped: %4u\n",
				ix,
				stat_uart.instance[ix].rx_interrupts, stat_uart.instance[ix].tx_interrupts,
				stat_uart.instance[ix].rx_posted, stat_uart.instance[ix].rx_posted_max, stat_uart.instance[ix].rx_posted_skipped,
				stat_uart.instance[ix].tx_posted, stat_uart.instance[ix].tx_posted_max, stat_uart.instance[ix].tx_posted_skipped);
}

void stats_lwip(string_t *dst)
{
	string_format(dst,
			"> LWIP\n"
			">  tcp received packets: %6u, bytes: %u\n"
			">  udp received packets: %6u, bytes: %u\n"
			">  tcp sent     packets: %6u, bytes: %u\n"
			">  udp sent     packets: %6u, bytes: %u\n"
			">  tcp locked buffer dropped: %u\n"
			">  udp locked buffer dropped: %u\n"
			">  udp errors: %u\n"
			">  tcp errors: %u\n"
			">  unicast received: %u, dropped: %u\n"
			">  broadcast received: %u, dropped: %u, group included: %u\n"
			">  multicast received: %u, dropped: %u\n",
				stat_lwip_tcp_received_packets,
				stat_lwip_tcp_received_bytes,
				stat_lwip_udp_received_packets,
				stat_lwip_udp_received_bytes,
				stat_lwip_tcp_sent_packets,
				stat_lwip_tcp_sent_bytes,
				stat_lwip_udp_sent_packets,
				stat_lwip_udp_sent_bytes,
				stat_lwip_tcp_locked,
				stat_lwip_udp_locked,
				stat_lwip_udp_send_error,
				stat_lwip_tcp_send_error,
				stat_lwip_unicast_received, stat_lwip_unicast_dropped,
				stat_lwip_broadcast_received, stat_lwip_broadcast_dropped, stat_broadcast_group_received,
				stat_lwip_multicast_received, stat_lwip_multicast_dropped);

	string_append(dst, "\nbound TCP sockets\n\n");
	lwip_netstat_bound(dst);

	string_append(dst, "\nlistening TCP sockets\n\n");
	lwip_netstat_listening(dst);

	string_append(dst, "\nactive TCP sockets\n\n");
	lwip_netstat_active(dst);

	string_append(dst, "\ntimewait TCP sockets\n");
	lwip_netstat_timewait(dst);
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
			"> i2c sensors periodic called: %u\n",
				i2c_sensor_info.periodic_called);

	string_format(dst,
			"> i2c sensors detect called: %u\n"
			"> i2c sensors detect succeeded: %u\n"
			"> i2c sensors detect bus select failed: %u\n"
			"> i2c sensors detect skip disabled: %u (%u)\n"
			"> i2c sensors detect skip secondary: %u (%u)\n"
			"> i2c sensors detect skip found on bus 0: %u\n"
			"> i2c sensors detect skip dup address: %u\n"
			"> i2c sensors detect failed: %u\n"
			"> i2c sensors detect current bus: %u\n"
			"> i2c sensors detect current sensor id: %u\n"
			"> i2c sensors detect started: %s\n"
			"> i2c sensors detect finished: %s\n"
			"> i2c sensors detect duration: %u ms\n",
				i2c_sensor_info.detect_called,
				i2c_sensor_info.detect_succeeded,
				i2c_sensor_info.detect_bus_select_failed,
				i2c_sensor_info.detect_skip_disabled,
				i2c_sensor_info.detect_skip_disabled / i2c_info.buses,
				i2c_sensor_info.detect_skip_secondary,
				i2c_sensor_info.detect_skip_secondary / i2c_info.buses,
				i2c_sensor_info.detect_skip_found_on_bus_0,
				i2c_sensor_info.detect_skip_duplicate_address,
				i2c_sensor_info.detect_failed,
				i2c_sensor_info.detect_current_bus,
				i2c_sensor_info.detect_current_sensor,
				yesno(i2c_sensor_info.detect_started),
				yesno(i2c_sensor_info.detect_finished),
				(uint32_t)((i2c_sensor_info.detect_finished_us - i2c_sensor_info.detect_started_us) / 1000));

	string_format(dst,
			"> i2c sensors init called: %u\n"
			"> i2c sensors init succeeded: %u\n"
			"> i2c sensors init skipped: %u\n"
			"> i2c sensors init failed: %u\n"
			"> i2c sensors init current sensor id: %u\n"
			"> i2c sensors init started: %s\n"
			"> i2c sensors init finished: %s\n"
			"> i2c sensors init duration: %u ms\n",
				i2c_sensor_info.init_called,
				i2c_sensor_info.init_succeeded,
				i2c_sensor_info.init_skipped,
				i2c_sensor_info.init_failed,
				i2c_sensor_info.init_current_sensor,
				yesno(i2c_sensor_info.init_started),
				yesno(i2c_sensor_info.init_finished),
				(uint32_t)((i2c_sensor_info.init_finished_us - i2c_sensor_info.init_started_us) / 1000));

	string_format(dst,
			"> i2c sensors background called: %u\n"
			"> i2c sensors background bus select failed: %u\n"
			"> i2c sensors background succeeded: %u\n"
			"> i2c sensors background failed: %u\n"
			"> i2c sensors background wrapped: %u\n"
			"> i2c sensors background current sensor: %u\n"
			"> i2c sensors background finished: %u\n",
				i2c_sensor_info.background_called,
				i2c_sensor_info.background_bus_select_failed,
				i2c_sensor_info.background_succeeded,
				i2c_sensor_info.background_failed,
				i2c_sensor_info.background_wrapped,
				i2c_sensor_info.background_current_sensor,
				i2c_sensor_info.background_finished);
}
