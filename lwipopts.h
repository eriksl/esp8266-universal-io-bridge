// lwip core

#define MEM_SIZE					(9 * 1024)
#define MEMP_NUM_PBUF				8
#define MEMP_NUM_UDP_PCB			4
#define MEMP_NUM_TCP_PCB			2
#define MEMP_NUM_TCP_PCB_LISTEN		2
#define MEMP_NUM_TCP_SEG			TCP_SND_QUEUELEN
#define MEMP_NUM_REASSDATA			1
#define MEMP_NUM_FRAG_PBUF			0
#define MEMP_NUM_ARP_QUEUE			0
#define MEMP_NUM_IGMP_GROUP			10
#define ARP_TABLE_SIZE				2
#define ARP_QUEUEING				0
#define ETHARP_TRUST_IP_MAC			1
#define IP_REASSEMBLY				1
#define IP_FRAG						1
#define IP_DEFAULT_TTL				128
#define IP_SOF_BROADCAST			0
#define IP_SOF_BROADCAST_RECV		0
#define LWIP_BROADCAST_PING			1
#define LWIP_MULTICAST_PING			1
#define LWIP_RAW					0
#define LWIP_DHCP					1
#define LWIP_IGMP					1
#define LWIP_DNS					0
#define DHCP_DOES_ARP_CHECK			0
#define TCP_QUEUE_OOSEQ				0
#define TCP_MSS						1460
#define TCP_WND						(2 * TCP_MSS)
#define TCP_SND_BUF					(4 * TCP_MSS)
#define TCP_SND_QUEUELEN			(4 * TCP_SND_BUF / TCP_MSS)
#define TCP_SNDLOWAT				1
#define TCP_SNDQUEUELOWAT			(TCP_SND_QUEUELEN - 1)
#define TCP_OVERSIZE				TCP_MSS
#define TCP_MAXRTX					2
#define PBUF_POOL_SIZE				2
#define LWIP_NETIF_HOSTNAME			1
#define LWIP_NETCONN				0
#define LWIP_SOCKET					0
#define LWIP_TCP_KEEPALIVE			0
#define LWIP_TCP_TIMESTAMPS			0
#define LWIP_STATS					0
#define LWIP_WND_SCALE				0

// lwip contributed code
#define DHCP_MAXRTX					4
#define LWIP_MDNS					0
