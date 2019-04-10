#ifndef _sdk_h_
#define _sdk_h_

#include "attribute.h"

#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>

enum
{
	SPI_FLASH_SEC_SIZE = 4096,
};

typedef enum
{
	SPI_FLASH_RESULT_OK,
	SPI_FLASH_RESULT_ERR,
	SPI_FLASH_RESULT_TIMEOUT,
} SpiFlashOpResult;

typedef enum
{
	GPIO_PIN_INTR_DISABLE = 0,
	GPIO_PIN_INTR_POSEDGE = 1,
	GPIO_PIN_INTR_NEGEDGE = 2,
	GPIO_PIN_INTR_ANYEDGE = 3,
	GPIO_PIN_INTR_LOLEVEL = 4,
	GPIO_PIN_INTR_HILEVEL = 5,
} GPIO_INT_TYPE;

typedef enum
{
	SYSTEM_PARTITION_INVALID = 0,
	SYSTEM_PARTITION_BOOTLOADER,
	SYSTEM_PARTITION_OTA_1,
	SYSTEM_PARTITION_OTA_2,
	SYSTEM_PARTITION_RF_CAL,
	SYSTEM_PARTITION_PHY_DATA,
	SYSTEM_PARTITION_SYSTEM_PARAMETER,
	SYSTEM_PARTITION_AT_PARAMETER,
	SYSTEM_PARTITION_SSL_CLIENT_CERT_PRIVKEY,
	SYSTEM_PARTITION_SSL_CLIENT_CA,
	SYSTEM_PARTITION_SSL_SERVER_CERT_PRIVKEY,
	SYSTEM_PARTITION_SSL_SERVER_CA,
	SYSTEM_PARTITION_WPA2_ENTERPRISE_CERT_PRIVKEY,
	SYSTEM_PARTITION_WPA2_ENTERPRISE_CA,
	SYSTEM_PARTITION_CUSTOMER_BEGIN = 100,
	SYSTEM_PARTITION_MAX
} partition_type_t;

typedef struct
{
	partition_type_t	type;
	uint32_t			addr;
	uint32_t			size;
} partition_item_t;

enum flash_size_map
{
	FLASH_SIZE_4M_MAP_256_256 = 0,
	FLASH_SIZE_2M,
	FLASH_SIZE_8M_MAP_512_512,
	FLASH_SIZE_16M_MAP_512_512,
	FLASH_SIZE_32M_MAP_512_512,
	FLASH_SIZE_16M_MAP_1024_1024,
	FLASH_SIZE_32M_MAP_1024_1024,
	FLASH_SIZE_32M_MAP_2048_2048,
	FLASH_SIZE_64M_MAP_1024_1024,
	FLASH_SIZE_128M_MAP_1024_1024,
};

enum rst_reason
{
	REASON_DEFAULT_RST = 0,
	REASON_WDT_RST,
	REASON_EXCEPTION_RST,
	REASON_SOFT_WDT_RST,
	REASON_SOFT_RESTART,
	REASON_DEEP_SLEEP_AWAKE,
	REASON_EXT_SYS_RST,
};

struct rst_info
{
	enum rst_reason	reason;
	uint32_t 		exccause;
	uint32_t		epc1;
	uint32_t		epc2;
	uint32_t		epc3;
	uint32_t		excvaddr;
	uint32_t		depc;
};

typedef enum
{
	OK = 0,
	FAIL,
	PENDING,
	BUSY,
	CANCEL,
} STATUS;

typedef enum _auth_mode
{
	AUTH_OPEN = 0,
	AUTH_WEP,
	AUTH_WPA_PSK,
	AUTH_WPA2_PSK,
	AUTH_WPA_WPA2_PSK,
	AUTH_MAX
} AUTH_MODE;

typedef enum _cipher_type
{
	CIPHER_NONE = 0,
	CIPHER_WEP40,
	CIPHER_WEP104,
	CIPHER_TKIP,
	CIPHER_CCMP,
	CIPHER_TKIP_CCMP,
	CIPHER_UNKNOWN,
} CIPHER_TYPE;

enum
{
	STATION_IDLE = 0,
	STATION_CONNECTING,
	STATION_WRONG_PASSWORD,
	STATION_NO_AP_FOUND,
	STATION_CONNECT_FAIL,
	STATION_GOT_IP
};

enum
{
	NULL_MODE = 0,
	STATION_MODE,
	SOFTAP_MODE,
	STATIONAP_MODE,
};

struct bss_info
{
	struct
	{
		struct bss_info *stqe_next;
	} next;
	uint8_t			bssid[6];
	uint8_t			ssid[32];
	uint8_t			ssid_len;
	uint8_t			channel;
	int8_t			rssi;
	AUTH_MODE		authmode;
	uint8_t			is_hidden;
	int16_t			freq_offset;
	int16_t			freqcal_val;
	uint8_t			*esp_mesh_ie;
	uint8_t			simple_pair;
	CIPHER_TYPE		pairwise_cipher;
	CIPHER_TYPE		group_cipher;
	unsigned int	phy_11b:1;
	unsigned int	phy_11g:1;
	unsigned int	phy_11n:1;
	unsigned int	wps:1;
	unsigned int	reserved:28;
};

struct softap_config
{
	char		ssid[32];
	char		password[64];
	uint8_t		ssid_len;
	uint8_t		channel;
	AUTH_MODE	authmode;			// don't support AUTH_WEP in softAP mode
	uint8_t		ssid_hidden;		// default 0
	uint8_t		max_connection;		// default 4, max 4
	uint16_t	beacon_interval;	// support 100 ~ 60000 ms, default 100
};

typedef enum
{
	WIFI_SCAN_TYPE_ACTIVE = 0,
	WIFI_SCAN_TYPE_PASSIVE,
} wifi_scan_type_t;

typedef struct
{
	uint32_t min; // units: millisecond, values above 1500ms may cause station to disconnect from AP and are not recommended.
	uint32_t max;
} wifi_active_scan_time_t;

typedef union
{
	wifi_active_scan_time_t active;
	uint32_t				passive; // units: millisecond, values above 1500ms may cause station to disconnect from AP and are not recommended.
} wifi_scan_time_t;

struct scan_config
{
	char				*ssid;			// ssid == NULL, don't filter ssid.
	char				*bssid;			// bssid == NULL, don't filter bssid.
	uint8_t				channel;		// channel == 0, scan all channels, otherwise scan set channel.
	uint8_t				show_hidden;	// show_hidden == 1, can get hidden ssid routers' info.
	wifi_scan_type_t	scan_type;		// scan type, active or passive
	wifi_scan_time_t	scan_time;		// scan time per channel
};

typedef struct
{
	int8_t		rssi;
	AUTH_MODE	authmode;
} wifi_fast_scan_threshold_t;

struct station_config
{
	char						ssid[32];
	char						password[64];
	uint8_t						bssid_set;	// if bssid_set is 1, station will just connect to the router with both ssid[] and bssid[] matched.
	uint8_t						bssid[6];
	wifi_fast_scan_threshold_t	threshold;
	bool						open_and_wep_mode_disable; // can connect to open/wep router by default.
};

typedef void (*scan_done_cb_t)(void *arg, STATUS status);

enum phy_mode
{
	PHY_MODE_11B = 1,
	PHY_MODE_11G = 2,
	PHY_MODE_11N = 3
};

enum sleep_type
{
	NONE_SLEEP_T = 0,
	LIGHT_SLEEP_T,
	MODEM_SLEEP_T
};

enum sleep_level
{
	MIN_SLEEP_T,
	MAX_SLEEP_T
};

enum
{
	STATION_IF = 0,
	SOFTAP_IF,
};

enum
{
	WLAN_STATE_INIT =	0,
	WLAN_STATE_AUTH =	2,
	WLAN_STATE_ASSOC =	3,
	WLAN_STATE_RUN =	5,
};

enum
{
	USER_TASK_PRIO_0 = 0,
	USER_TASK_PRIO_1,
	USER_TASK_PRIO_2,
	USER_TASK_PRIO_MAX
};

typedef uint8_t sdk_mac_addr_t[6];

enum dhcp_status
{
	DHCP_STOPPED = 0,
	DHCP_STARTED
};

enum dhcps_offer_option
{
	OFFER_START = 0,
	OFFER_ROUTER,
	OFFER_END
};

struct dhcps_lease
{
	bool enable;
	struct ip_addr start_ip;
	struct ip_addr end_ip;
};

typedef struct
{
	uint32_t sig;
	uint32_t par;
} os_event_t;

enum
{
	EVENT_STAMODE_CONNECTED = 0,
	EVENT_STAMODE_DISCONNECTED,
	EVENT_STAMODE_AUTHMODE_CHANGE,
	EVENT_STAMODE_GOT_IP,
	EVENT_STAMODE_DHCP_TIMEOUT,
	EVENT_SOFTAPMODE_STACONNECTED,
	EVENT_SOFTAPMODE_STADISCONNECTED,
	EVENT_SOFTAPMODE_PROBEREQRECVED,
	EVENT_OPMODE_CHANGED,
	EVENT_SOFTAPMODE_DISTRIBUTE_STA_IP,
	EVENT_MAX,
};

enum
{
	REASON_DUMMY_1 = 0,
	REASON_UNSPECIFIED,
	REASON_AUTH_EXPIRE,
	REASON_AUTH_LEAVE,
	REASON_ASSOC_EXPIRE,
	REASON_ASSOC_TOOMANY,
	REASON_NOT_AUTHED,
	REASON_NOT_ASSOCED,
	REASON_ASSOC_LEAVE,
	REASON_ASSOC_NOT_AUTHED,
	REASON_DISASSOC_PWRCAP_BAD,
	REASON_DISASSOC_SUPCHAN_BAD,
	REASON_DUMMY_2,
	REASON_IE_INVALID,
	REASON_MIC_FAILURE,
	REASON_4WAY_HANDSHAKE_TIMEOUT,
	REASON_GROUP_KEY_UPDATE_TIMEOUT,
	REASON_IE_IN_4WAY_DIFFERS,
	REASON_GROUP_CIPHER_INVALID,
	REASON_PAIRWISE_CIPHER_INVALID,
	REASON_AKMP_INVALID,
	REASON_UNSUPP_RSN_IE_VERSION,
	REASON_INVALID_RSN_IE_CAP,
	REASON_802_1X_AUTH_FAILED,
	REASON_CIPHER_SUITE_REJECTED,
	REASON_BEACON_TIMEOUT = 200,
	REASON_NO_AP_FOUND,
	REASON_AUTH_FAIL,
	REASON_ASSOC_FAIL,
	REASON_HANDSHAKE_TIMEOUT,
};

typedef struct
{
	char	ssid[32];
	uint8_t	ssid_len;
	char	bssid[6];
	uint8_t	channel;
} Event_StaMode_Connected_t;

typedef struct
{
	char	ssid[32];
	uint8_t ssid_len;
	char	bssid[6];
	uint8_t reason;
} Event_StaMode_Disconnected_t;

typedef struct
{
	uint8_t old_mode;
	uint8_t new_mode;
} Event_StaMode_AuthMode_Change_t;

typedef struct
{
	struct ip_addr ip;
	struct ip_addr mask;
	struct ip_addr gw;
} Event_StaMode_Got_IP_t;

typedef struct
{
	sdk_mac_addr_t	mac;
	uint8_t			aid;
} Event_SoftAPMode_StaConnected_t;

typedef struct
{
	sdk_mac_addr_t		mac;
	struct ip_addr		ip;
	uint8_t aid;
} Event_SoftAPMode_Distribute_Sta_IP_t;

typedef struct
{
	sdk_mac_addr_t	mac;
			uint8_t			aid;
} Event_SoftAPMode_StaDisconnected_t;

typedef struct
{
	int				rssi;
	sdk_mac_addr_t	mac;
} Event_SoftAPMode_ProbeReqRecved_t;

typedef struct
{
	uint8_t old_opmode;
	uint8_t new_opmode;
} Event_OpMode_Change_t;

typedef union
{
	Event_StaMode_Connected_t				connected;
	Event_StaMode_Disconnected_t			disconnected;
	Event_StaMode_AuthMode_Change_t			auth_change;
	Event_StaMode_Got_IP_t					got_ip;
	Event_SoftAPMode_StaConnected_t			sta_connected;
	Event_SoftAPMode_Distribute_Sta_IP_t	distribute_sta_ip;
	Event_SoftAPMode_StaDisconnected_t		sta_disconnected;
	Event_SoftAPMode_ProbeReqRecved_t		ap_probereqrecved;
	Event_OpMode_Change_t					opmode_changed;
} Event_Info_u;

typedef struct _esp_event
{
	uint32_t		event;
	Event_Info_u	event_info;
} System_Event_t;

typedef void (*wifi_event_handler_cb_t)(System_Event_t *event);

struct ETSEventTag
{
	uint32_t sig;
	uint32_t par;
};

typedef void ETSTimerFunc(void *);
typedef struct ETSEventTag ETSEvent;
typedef void (*os_task_t)(ETSEvent *);

typedef struct _ETSTIMER_ {
	struct _ETSTIMER_	*timer_next;
	uint32_t			timer_expire;
	uint32_t			timer_period;
	ETSTimerFunc		*timer_func;
	void				*timer_arg;
} os_timer_t;

typedef void (*init_done_cb_t)(void);

typedef struct {
  uint32_t		i[2];
  uint32_t		buf[4];
  unsigned char in[64];
  unsigned char digest[16];
} MD5_CTX;

typedef struct {
    unsigned int h0, h1, h2, h3, h4;
    unsigned int Nl, Nh;
    unsigned int data[16];
    unsigned int num;
} SHA_CTX;

struct tm
{
  int	tm_sec;
  int	tm_min;
  int	tm_hour;
  int	tm_mday;
  int	tm_mon;
  int	tm_year;
  int	tm_wday;
  int	tm_yday;
  int	tm_isdst;
};

attr_nonnull int ets_vsnprintf(char *, size_t, const char *, va_list);
attr_nonnull struct tm *sntp_localtime(const uint32_t *);

attr_nonnull int MD5Init(MD5_CTX *context);
attr_nonnull int MD5Update(MD5_CTX *context, const void *, unsigned int length);
attr_nonnull int MD5Final(unsigned char *hash, MD5_CTX *context);

attr_nonnull int SHA1Init(SHA_CTX *context);
attr_nonnull int SHA1Update(SHA_CTX *context, const void *, unsigned int length);
attr_nonnull int SHA1Final(unsigned char *md, SHA_CTX *context);

enum { SHA_DIGEST_LENGTH = 20 };

uint16_t			system_adc_read(void);
uint32_t			system_get_chip_id(void);
uint8_t				system_get_cpu_freq(void);
enum flash_size_map	system_get_flash_size_map(void);
uint32_t			system_get_free_heap_size(void);
const char *		system_get_sdk_version(void);
struct rst_info* 	system_get_rst_info(void);
uint32_t			system_get_rtc_time(void);
uint32_t			system_get_time(void);
void				system_init_done_cb(init_done_cb_t);
bool				system_partition_get_item(partition_type_t type, partition_item_t* partition_item);
bool				system_partition_table_regist(const partition_item_t *partition_table, uint32_t partition_num, uint32_t map);
void				system_print_meminfo(void);
bool				system_os_post(uint8_t, uint32_t, uint32_t);
bool				system_os_task(os_task_t, uint8_t, os_event_t *, uint8_t);
bool				system_rtc_mem_read(uint8_t, void *, uint16_t);
bool				system_rtc_mem_write(uint8_t, const void *, uint16_t);
void				system_restart(void);
void				system_set_os_print(uint8_t onoff);
uint32_t			system_rtc_clock_cali_proc(void);
void				system_soft_wdt_feed(void);
bool				system_update_cpu_freq(uint8_t);

void				ets_delay_us(uint32_t);
void				ets_install_putc1(void (*)(char));
void				ets_timer_setfn(os_timer_t *, ETSTimerFunc *, void *);
void				ets_timer_arm_new(os_timer_t *, uint32_t, bool, bool);
void				ets_timer_disarm(os_timer_t *);

attr_inline void	os_delay_us(uint32_t s) { ets_delay_us(s); }
attr_inline void	os_install_putc1(void (*p)(char)) { ets_install_putc1(p); }
attr_inline void	os_timer_setfn(os_timer_t *ptimer, ETSTimerFunc *pfunction, void *parg) { ets_timer_setfn(ptimer, pfunction, parg); }
attr_inline void	os_timer_arm(os_timer_t *ptimer, uint32_t time, bool repeat_flag) { ets_timer_arm_new(ptimer, time, repeat_flag, 0); }
attr_inline void	os_timer_disarm(os_timer_t *ptimer) { ets_timer_disarm(ptimer); }
uint32_t			os_random(void);

uint32_t			spi_flash_get_id(void);
SpiFlashOpResult	spi_flash_erase_sector(uint16_t);
SpiFlashOpResult	spi_flash_write(uint32_t, const void * attr_flash_align, uint32_t);
SpiFlashOpResult	spi_flash_read(uint32_t, void * attr_flash_align, uint32_t);

uint8_t				wifi_get_channel(void);
bool				wifi_get_macaddr(uint8_t if_index, sdk_mac_addr_t);
uint8_t				wifi_get_opmode(void);
bool				wifi_get_ip_info(uint8_t if_index, struct ip_info *);
enum phy_mode		wifi_get_phy_mode(void);
enum sleep_type		wifi_get_sleep_type(void);
void				wifi_set_event_handler_cb(wifi_event_handler_cb_t);
bool				wifi_set_opmode(uint8_t opmode);
bool				wifi_set_opmode_current(uint8_t);
bool				wifi_set_sleep_type(enum sleep_type);
enum dhcp_status	wifi_softap_dhcps_status(void);
uint32_t			wifi_softap_get_dhcps_lease_time(void);
bool				wifi_softap_get_dhcps_lease(struct dhcps_lease *);
bool				wifi_softap_reset_dhcps_lease_time(void);
bool				wifi_softap_set_dhcps_offer_option(uint8_t, void *);
bool				wifi_softap_set_dhcps_lease_time(uint32_t);
bool				wifi_softap_set_config_current(struct softap_config *);
bool				wifi_softap_set_dhcps_lease(struct dhcps_lease *);
bool				wifi_station_connect(void);
bool				wifi_station_disconnect(void);
uint8_t				wifi_station_get_auto_connect(void);
bool				wifi_station_get_config_default(struct station_config *);
bool				wifi_station_get_config(struct station_config *);
int8_t				wifi_station_get_rssi(void);
uint8_t				wifi_station_get_connect_status(void);
bool				wifi_station_scan(struct scan_config *config, scan_done_cb_t);
bool				wifi_station_set_auto_connect(uint8_t);
bool				wifi_station_set_config(struct station_config *);

void		gpio_init(void);
void		gpio_pin_intr_state_set(uint32_t, GPIO_INT_TYPE);

void		sntp_init(void);								// FIXME
void		sntp_stop(void);								// FIXME
int8_t		sntp_get_timezone(void);						// FIXME
ip_addr_t	sntp_getserver(uint8_t);						// FIXME
uint32_t	sntp_get_current_timestamp(void);				// FIXME
const char*	sntp_get_real_time(uint32_t);					// FIXME
void		sntp_setserver(uint8_t idx, ip_addr_t *addr);	// FIXME
bool		sntp_set_timezone(int8_t timezone);				// FIXME

#endif
