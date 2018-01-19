#include "stats.h"

#include "util.h"
#include "config.h"
#include "time.h"
#include "i2c.h"

#include <c_types.h>
#include <user_interface.h>

#if IMAGE_OTA == 1
#include <rboot-api.h>
#endif

stat_called_t stat_called =
{
	.user_rf_cal_sector_set = 0,
	.user_rf_pre_init = 0
};

int stat_uart_rx_interrupts;
int stat_uart_tx_interrupts;
int stat_fast_timer;
int stat_slow_timer;
int stat_timer_interrupts;
int stat_pwm_timer_interrupts;
int stat_pc_interrupts;
int stat_i2c_init_time_us;
int stat_display_init_time_us;
int stat_cmd_receive_buffer_overflow;
int stat_cmd_send_buffer_overflow;
int stat_uart_receive_buffer_overflow;
int stat_uart_send_buffer_overflow;

int stat_update_uart;
int stat_update_longop;
int stat_update_command_udp;
int stat_update_command_tcp;
int stat_update_display;
int stat_update_ntp;
int stat_update_idle;

volatile uint32_t	*stat_stack_sp_initial;
int					stat_stack_painted;

static const char *flash_map[] =
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

static const char *reset_map[] =
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

static const char *phy[] = {
	"unknown",
	"802.11b",
	"802.11g",
	"802.11n",
	"unknown"
};

static const char *slp[] =
{
	"none",
	"light",
	"modem",
	"unknown"
};

typedef struct
{
	unsigned int id;
	const char *name;
} manufacturer_t;

static const manufacturer_t manufacturers[] =
{
	{	0xc8,	"GigaDevice"	},
	{	0xe0,	"Berg Micro"	},
	{	0xef,	"Winbond"		},
	{	0,		(const char *)0	}
};

irom attr_pure static const char *manufacturer_id_to_string(unsigned int id)
{
	const manufacturer_t *manufacturer;

	for(manufacturer = manufacturers; manufacturer->id && manufacturer->name; manufacturer++)
		if(manufacturer->id == id)
			return(manufacturer->name);

	return("unknown");
}

irom void stats_firmware(string_t *dst)
{
#if IMAGE_OTA == 1
	rboot_config rcfg;
#endif
	const struct rst_info *rst_info;
	uint32_t flash_id = spi_flash_get_id();
	unsigned int flash_manufacturer_id	= (flash_id & 0x000000ff) >> 0;
	unsigned int flash_speed			= (flash_id & 0x0000ff00) >> 8;
	unsigned int flash_size				= (flash_id & 0x00ff0000) >> 16;
	unsigned int stack_size = (uint32_t)stack_bottom - (uint32_t)sysram_top;
	unsigned int stack_used = 0;
	unsigned int stack_free = stack_size;
	uint32_t *sp;

	for(sp = (void *)sysram_top; (sp < stat_stack_sp_initial) && (*sp == stack_paint_magic); sp++)
	{
		stack_used += 4;
		stack_free -= 4;
	}

	rst_info = system_get_rst_info();

	string_format(dst,
			"> firmware version date: %s\n"
			"> SDK version: %s\n"
			"> system id: %u\n"
			"> spi flash id: %08x, manufacturer: %s, speed: %02x MHz, size: %u kib / %u MiB\n"
			"> cpu frequency: %u MHz\n"
			"> flash map: %s\n"
			"> reset cause: %s\n"
			"> config sector address: %x\n"
			"> rf calibration sector address: %x\n"
			"> value of initial stack pointer: %p\n"
			"> value of current stack pointer: %p\n"
			"> stack painted: %u bytes\n"
			"> stack not painted: %u bytes\n"
			"> stack size: %u bytes\n"
			"> stack used: %u bytes\n"
			"> stack free: %u bytes\n"
			"> heap free: %u bytes\n",
				__DATE__ " " __TIME__,
				system_get_sdk_version(),
				system_get_chip_id(),
				flash_id, manufacturer_id_to_string(flash_manufacturer_id), flash_speed, 1 << (flash_size - 10), 1 << (flash_size - 17),
				system_get_cpu_freq(),
				flash_map[system_get_flash_size_map()],
				reset_map[rst_info->reason],
				USER_CONFIG_SECTOR * 0x1000,
				RFCAL_ADDRESS,
				stat_stack_sp_initial,
				&sp,
				stat_stack_painted,
				stack_size - stat_stack_painted,
				stack_size,
				stack_used,
				stack_free,
				system_get_free_heap_size());

	system_print_meminfo();

#if IMAGE_OTA == 1
	rcfg = rboot_get_config();

	string_format(dst,
			">\n"
			"> OTA image information\n"
			"> magic: 0x%x\n"
			"> version: %u\n"
			"> mode: %x\n"
			"> current: %u\n"
			"> count: %u\n"
			"> rom 0: 0x%06x\n"
			"> rom 1: 0x%06x\n",
			rcfg.magic,
			rcfg.version,
			rcfg.mode,
			rcfg.current_rom,
			rcfg.count,
			rcfg.roms[0],
			rcfg.roms[1]);
#else
	string_append(dst, ">\n> No OTA image\n");
#endif
}

irom void stats_time(string_t *dst)
{
	unsigned int uptime_secs, uptime_msecs, uptime_raw1, uptime_raw2, uptime_base, uptime_wraps;
	unsigned int system_secs, system_msecs, system_raw1, system_raw2, system_base, system_wraps;
	unsigned int rtc_secs, rtc_msecs, rtc_raw1, rtc_raw2, rtc_base, rtc_wraps;
	unsigned int timer_secs, timer_msecs, timer_raw1, timer_raw2, timer_base, timer_wraps;
	unsigned int ntp_secs, ntp_msecs, ntp_raw1, ntp_raw2, ntp_base, ntp_wraps;
	unsigned int Y, M, D, h, m, s;
	const char *time_source;

	time_uptime_get(&uptime_secs, &uptime_msecs, &uptime_raw1, &uptime_raw2, &uptime_base, &uptime_wraps);
	time_system_get(&system_secs, &system_msecs, &system_raw1, &system_raw2, &system_base, &system_wraps);
	time_rtc_get(&rtc_secs, &rtc_msecs, &rtc_raw1, &rtc_raw2, &rtc_base, &rtc_wraps);
	time_timer_get(&timer_secs, &timer_msecs, &timer_raw1, &timer_raw2, &timer_base, &timer_wraps);
	time_ntp_get(&ntp_secs, &ntp_msecs, &ntp_raw1, &ntp_raw2, &ntp_base, &ntp_wraps);
	time_source = time_get(&h, &m, &s, &Y, &M, &D);

	string_format(dst,
			"> uptime: %u.%03u s (r1=%u,r2=%u,b=%u,w=%u)\n"
			"> system: %u.%03u s (r1=%u,r2=%u,b=%u,w=%u,d=%d)\n"
			"> rtc: %u.%03u s (r1=%u,r2=%u,b=%u,w=%u,d=%d)\n"
			"> timer: %u.%03u s (r1=%u,r2=%u,b=%u,w=%u,d=%d)\n"
			"> ntp: %u.%03u s (r1=%u,r2=%u,b=%u,w=%u)\n"
			"> time: %04u/%02u/%02u %02u:%02u:%02u, source: %s\n",
				uptime_secs, uptime_msecs, uptime_raw1, uptime_raw2, uptime_base, uptime_wraps,
				system_secs, system_msecs, system_raw1, system_raw2, system_base, system_wraps, system_secs - ntp_secs,
				rtc_secs, rtc_msecs, rtc_raw1, rtc_raw2, rtc_base, rtc_wraps, rtc_secs - ntp_secs,
				timer_secs, timer_msecs, timer_raw1, timer_raw2, timer_base, timer_wraps, timer_secs - ntp_secs,
				ntp_secs, ntp_msecs, ntp_raw1, ntp_raw2, ntp_base, ntp_wraps,
				Y, M, D, h, m, s, time_source);
}

irom void stats_counters(string_t *dst)
{
	string_format(dst,
			"> user_rf_cal_sector_set called: %s\n"
			"> user_rf_pre_init called: %s\n"
			"> int uart rx: %u\n"
			"> int uart tx: %u\n"
			"> fast timer fired: %u\n"
			"> slow timer fired: %u\n"
			"> pwm timer int fired: %u\n"
			"> pc int fired: %u\n"
			"> uart updated: %u\n"
			"> longops processed: %u\n"
			"> commands/udp processed: %u\n"
			"> commands/tcp processed: %u\n"
			"> display updated: %u\n"
			"> ntp updated: %u\n"
			"> background idle: %u\n"
			"> cmd receive buffer overflow events: %u\n"
			"> cmd send buffer overflow events: %u\n"
			"> uart receive buffer overflow events: %u\n"
			"> uart send buffer overflow events: %u\n",
				yesno(stat_called.user_rf_cal_sector_set),
				yesno(stat_called.user_rf_pre_init),
				stat_uart_rx_interrupts,
				stat_uart_tx_interrupts,
				stat_fast_timer,
				stat_slow_timer,
				stat_pwm_timer_interrupts,
				stat_pc_interrupts,
				stat_update_uart,
				stat_update_longop,
				stat_update_command_udp,
				stat_update_command_tcp,
				stat_update_display,
				stat_update_ntp,
				stat_update_idle,
				stat_cmd_receive_buffer_overflow,
				stat_cmd_send_buffer_overflow,
				stat_uart_receive_buffer_overflow,
				stat_uart_send_buffer_overflow);
}

irom void stats_i2c(string_t *dst)
{
	i2c_info_t i2c_info;

	i2c_get_info(&i2c_info);

	string_format(dst,
			"> i2c clock delay: %u\n"
			"> display initialisation time: %u us\n"
			"> i2c initialisation time: %u us\n"
			"> i2c multiplexer found: %s\n"
			"> i2c buses: %u\n",
				i2c_info.delay,
				stat_display_init_time_us,
				stat_i2c_init_time_us,
				yesno(i2c_info.multiplexer),
				i2c_info.buses);
}

irom void stats_wlan(string_t *dst)
{
	uint8 mac_addr[6];
	struct ip_info ip_addr_info;
	struct station_config config;

	wifi_station_get_config_default(&config);

	string_format(dst, "> default ssid: \"%s\", passwd: \"%s\"\n",
			config.ssid, config.password);

	wifi_station_get_config(&config);

	string_format(dst, "> current ssid: \"%s\", passwd: \"%s\"\n",
			config.ssid, config.password);

	string_format(dst,
			"> phy mode: %s\n"
			"> sleep mode: %s\n"
			"> channel: %u\n"
			"> signal strength: %d dB\n"
			">\n",
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
