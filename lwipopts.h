// lwip core

#define MEM_SIZE					8192
#define MEMP_NUM_PBUF				32
#define MEMP_NUM_UDP_PCB			8
#define MEMP_NUM_TCP_PCB			4
#define MEMP_NUM_TCP_PCB_LISTEN		4
#define MEMP_NUM_TCP_SEG			16
#define MEMP_NUM_ARP_QUEUE			8
#define MEMP_NUM_IGMP_GROUP			4
#define MEMP_NUM_SYS_TIMEOUT		8
#define ARP_TABLE_SIZE				5
#define ARP_QUEUEING				1
#define ETHARP_TRUST_IP_MAC			1
#define IP_REASSEMBLY				0
#define IP_FRAG						0
#define IP_DEFAULT_TTL				128
#define IP_SOF_BROADCAST			0
#define IP_SOF_BROADCAST_RECV		0
#define LWIP_BROADCAST_PING			1
#define LWIP_MULTICAST_PING			1
#define LWIP_RAW					0
#define LWIP_DHCP					1
#define LWIP_IGMP					1
#define LWIP_DNS					0
#define TCP_WND						(4 * TCP_MSS)
#define TCP_MAXRTX					4
#define TCP_MSS						1460
#define TCP_SND_BUF					(4 * TCP_MSS)
#define PBUF_POOL_SIZE				4
#define LWIP_NETIF_HOSTNAME			1
#define LWIP_NETCONN				0
#define LWIP_SOCKET					0
#define LWIP_TCP_KEEPALIVE			1
#define LWIP_STATS					0

// lwip contributed code
#define DHCP_MAXRTX					4
#define LWIP_MDNS					0
