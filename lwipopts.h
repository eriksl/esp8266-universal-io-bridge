// lwip core

#define MEM_SIZE					(6 * 1024)
#define MEMP_NUM_PBUF				32
#define MEMP_NUM_UDP_PCB			4
#define MEMP_NUM_TCP_PCB			2
#define MEMP_NUM_TCP_PCB_LISTEN		2
#define MEMP_NUM_TCP_SEG			TCP_SND_QUEUELEN
#define MEMP_NUM_REASSDATA			2
#define MEMP_NUM_FRAG_PBUF			4
#define MEMP_NUM_ARP_QUEUE			0
#define MEMP_NUM_IGMP_GROUP			2
#define ARP_TABLE_SIZE				2
#define ARP_QUEUEING				0
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
#define DHCP_DOES_ARP_CHECK			0
#define TCP_QUEUE_OOSEQ				0
#define TCP_MSS						1460
#define TCP_WND						(1 * TCP_MSS)
#define TCP_SND_BUF					(3 * TCP_MSS)
#define TCP_SND_QUEUELEN			6
#define TCP_SNDLOWAT				(TCP_SND_BUF / 2)
#define TCP_SNDQUEUELOWAT			(TCP_SND_QUEUELEN / 2)
#define TCP_MAXRTX					12
#define PBUF_POOL_SIZE				2
#define LWIP_NETIF_HOSTNAME			1
#define LWIP_NETCONN				0
#define LWIP_SOCKET					0
#define LWIP_TCP_KEEPALIVE			0
#define LWIP_STATS					0

// lwip contributed code
#define DHCP_MAXRTX					4
#define LWIP_MDNS					0
