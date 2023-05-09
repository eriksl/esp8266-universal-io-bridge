#include "lwip/inet.h"
#include "lwip/err.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"
#include "lwip/mem.h"

#include <stdint.h>
#include <string.h>

// include dhcpserver.h 
// {
typedef struct dhcps_state{
        int16_t state;
} dhcps_state;

typedef struct dhcps_msg {
        uint8_t op, htype, hlen, hops;
        uint8_t xid[4];
        uint16_t secs, flags;
        uint8_t ciaddr[4];
        uint8_t yiaddr[4];
        uint8_t siaddr[4];
        uint8_t giaddr[4];
        uint8_t chaddr[16];
        uint8_t sname[64];
        uint8_t file[128];
        uint8_t options[312];
}dhcps_msg;

typedef enum {
    DHCPS_TYPE_DYNAMIC,
    DHCPS_TYPE_STATIC
} dhcps_type_t;

typedef enum {
    DHCPS_STATE_ONLINE,
    DHCPS_STATE_OFFLINE
} dhcps_state_t;

struct dhcps_pool{
    struct ip_addr ip;
    uint8_t mac[6];
    uint32_t lease_timer;
    dhcps_type_t type;
    dhcps_state_t state;

};

typedef struct _list_node{
    void *pnode;
    struct _list_node *pnext;
}list_node;

extern uint32_t dhcps_lease_time;
#define DHCPS_LEASE_TIMER  dhcps_lease_time  //0x05A0
#define DHCPS_MAX_LEASE 0x64
#define BOOTP_BROADCAST 0x8000

#define DHCP_REQUEST        1
#define DHCP_REPLY          2
#define DHCP_HTYPE_ETHERNET 1
#define DHCP_HLEN_ETHERNET  6
#define DHCP_MSG_LEN      236

#define DHCPS_SERVER_PORT  67
#define DHCPS_CLIENT_PORT  68

#define DHCPDISCOVER  1
#define DHCPOFFER     2
#define DHCPREQUEST   3
#define DHCPDECLINE   4
#define DHCPACK       5
#define DHCPNAK       6
#define DHCPRELEASE   7

#define DHCP_OPTION_SUBNET_MASK   1
#define DHCP_OPTION_ROUTER        3
#define DHCP_OPTION_DNS_SERVER    6
#define DHCP_OPTION_REQ_IPADDR   50
#define DHCP_OPTION_LEASE_TIME   51
#define DHCP_OPTION_MSG_TYPE     53
#define DHCP_OPTION_SERVER_ID    54
#define DHCP_OPTION_INTERFACE_MTU 26
#define DHCP_OPTION_PERFORM_ROUTER_DISCOVERY 31
#define DHCP_OPTION_BROADCAST_ADDRESS 28
#define DHCP_OPTION_REQ_LIST     55
#define DHCP_OPTION_END         255

//#define USE_CLASS_B_NET 1
#define DHCPS_DEBUG          0
#define MAX_STATION_NUM      8

#define DHCPS_STATE_OFFER 1
#define DHCPS_STATE_DECLINE 2
#define DHCPS_STATE_ACK 3
#define DHCPS_STATE_NAK 4
#define DHCPS_STATE_IDLE 5
#define DHCPS_STATE_RELEASE 6

#define   dhcps_router_enabled(offer)   ((offer & OFFER_ROUTER) != 0)

struct ip_info
{
    struct ip_addr ip;
    struct ip_addr netmask;
    struct ip_addr gw;
};

void dhcps_start(struct ip_info *info);
void dhcps_stop(void);
// }

// erik code (from NONOS-SDK include files)
// {
enum dhcps_offer_option
{
	OFFER_START = 0,
	OFFER_ROUTER,
	OFFER_END
};

enum
{
	STATION_IF = 0,
	SOFTAP_IF,
};

enum
{
	NULL_MODE = 0,
	STATION_MODE,
	SOFTAP_MODE,
	STATIONAP_MODE,
};

enum dhcp_status
{
	DHCP_STOPPED = 0,
	DHCP_STARTED
};

struct dhcps_lease
{
	_Bool enable;
	struct ip_addr start_ip;
	struct ip_addr end_ip;
};

_Bool		wifi_get_ip_info(uint8_t if_index, struct ip_info *);
_Bool		wifi_softap_get_dhcps_lease(struct dhcps_lease *);
_Bool		wifi_softap_reset_dhcps_lease_time(void);
_Bool		wifi_softap_set_dhcps_lease(struct dhcps_lease *);
uint32_t	wifi_softap_get_dhcps_lease_time(void);
_Bool		wifi_softap_set_dhcps_lease_time(uint32_t);
_Bool		wifi_softap_set_dhcps_offer_option(uint8_t, void *);
uint8_t		wifi_get_opmode(void);
enum dhcp_status	wifi_softap_dhcps_status(void);
// } Espressif code

////////////////////////////////////////////////////////////////////////////////////
//static const uint8_t xid[4] = {0xad, 0xde, 0x12, 0x23};
//static u8_t old_xid[4] = {0};
static const uint32_t magic_cookie __attribute__((section(".irom.text"))) = 0x63538263;
static struct udp_pcb *pcb_dhcps = NULL;
static struct ip_addr broadcast_dhcps;
static struct ip_addr server_address;
static struct ip_addr client_address;//added

static struct dhcps_lease dhcps_lease;
//static _Bool dhcps_lease_flag = 1;
static list_node *unit_plist = NULL;
static uint8_t offer = 0xFF;
static _Bool renew = 0;
#define DHCPS_LEASE_TIME_DEF    (120)
uint32_t dhcps_lease_time = DHCPS_LEASE_TIME_DEF;  //minute

void wifi_softap_dhcps_client_leave(char *bssid, struct ip_addr *ip,_Bool force);
uint32_t wifi_softap_dhcps_client_update(char *bssid, struct ip_addr *ip);
void wifi_softap_set_station_info(const uint8_t mac[6], const ip_addr_t *ip_address);
struct netif * eagle_lwip_getif(uint8_t index);

/******************************************************************************
 * FunctionName : node_insert_to_list
 * Description  : insert the node to the list
 * Parameters   : arg -- Additional argument to pass to the callback function
 * Returns      : none
*******************************************************************************/

void node_insert_to_list(list_node **phead, list_node* pinsert);
void node_insert_to_list(list_node **phead, list_node* pinsert)
{
    list_node *plist = NULL;
    struct dhcps_pool *pdhcps_pool = NULL;
    struct dhcps_pool *pdhcps_node = NULL;
    if (*phead == NULL)
        *phead = pinsert;
    else {
        plist = *phead;
        pdhcps_node = pinsert->pnode;
        pdhcps_pool = plist->pnode;

        if(pdhcps_node->ip.addr < pdhcps_pool->ip.addr) {
            pinsert->pnext = plist;
            *phead = pinsert;
        } else {
            while (plist->pnext != NULL) {
                pdhcps_pool = plist->pnext->pnode;
                if (pdhcps_node->ip.addr < pdhcps_pool->ip.addr) {
                    pinsert->pnext = plist->pnext;
                    plist->pnext = pinsert;
                    break;
                }
                plist = plist->pnext;
            }

            if(plist->pnext == NULL) {
                plist->pnext = pinsert;
            }
        }
    }
//  pinsert->pnext = NULL;
}

/******************************************************************************
 * FunctionName : node_delete_from_list
 * Description  : remove the node from list
 * Parameters   : arg -- Additional argument to pass to the callback function
 * Returns      : none
*******************************************************************************/
void node_remove_from_list(list_node **phead, list_node* pdelete);
void node_remove_from_list(list_node **phead, list_node* pdelete)
{
    list_node *plist = NULL;

    plist = *phead;
    if (plist == NULL){
        *phead = NULL;
    } else {
        if (plist == pdelete){
            *phead = plist->pnext;
            pdelete->pnext = NULL;
        } else {
            while (plist != NULL) {
                if (plist->pnext == pdelete){
                    plist->pnext = pdelete->pnext;
                    pdelete->pnext = NULL;
                }
                plist = plist->pnext;
            }
        }
    }
}
///////////////////////////////////////////////////////////////////////////////////
/*
 *       DHCP msg
 *
 * @param optptr -- DHCP msg
 * @param type --                          option
 *
 * @return uint8_t*             DHCP msg
 */
///////////////////////////////////////////////////////////////////////////////////
static uint8_t* add_msg_type(uint8_t *optptr, uint8_t type)
{

        *optptr++ = DHCP_OPTION_MSG_TYPE;
        *optptr++ = 1;
        *optptr++ = type;
        return optptr;
}
///////////////////////////////////////////////////////////////////////////////////
/*
 *       DHCP msg                        offer
 *
 * @param optptr -- DHCP msg
 *
 * @return uint8_t*             DHCP msg
 */
///////////////////////////////////////////////////////////////////////////////////
static uint8_t* add_offer_options(uint8_t *optptr)
{
        struct ip_addr ipadd;

        ipadd.addr = *( (u32_t *) &server_address);

#ifdef USE_CLASS_B_NET
        *optptr++ = DHCP_OPTION_SUBNET_MASK;
        *optptr++ = 4;  //length
        *optptr++ = 255;
        *optptr++ = 240;
        *optptr++ = 0;
        *optptr++ = 0;
#else
        *optptr++ = DHCP_OPTION_SUBNET_MASK;
        *optptr++ = 4;
        *optptr++ = 255;
        *optptr++ = 255;
        *optptr++ = 255;
        *optptr++ = 0;
#endif

        *optptr++ = DHCP_OPTION_LEASE_TIME;
        *optptr++ = 4;
        *optptr++ = ((DHCPS_LEASE_TIMER * 60) >> 24) & 0xFF;
        *optptr++ = ((DHCPS_LEASE_TIMER * 60) >> 16) & 0xFF;
        *optptr++ = ((DHCPS_LEASE_TIMER * 60) >> 8) & 0xFF;
        *optptr++ = ((DHCPS_LEASE_TIMER * 60) >> 0) & 0xFF;

        *optptr++ = DHCP_OPTION_SERVER_ID;
        *optptr++ = 4;
        *optptr++ = ip4_addr1( &ipadd);
        *optptr++ = ip4_addr2( &ipadd);
        *optptr++ = ip4_addr3( &ipadd);
        *optptr++ = ip4_addr4( &ipadd);

        if (dhcps_router_enabled(offer)){
            struct ip_info if_ip;
            bzero(&if_ip, sizeof(struct ip_info));
            wifi_get_ip_info(SOFTAP_IF, &if_ip);

            *optptr++ = DHCP_OPTION_ROUTER;
            *optptr++ = 4;
            *optptr++ = ip4_addr1( &if_ip.gw);
            *optptr++ = ip4_addr2( &if_ip.gw);
            *optptr++ = ip4_addr3( &if_ip.gw);
            *optptr++ = ip4_addr4( &if_ip.gw);
        }

#ifdef USE_DNS
        *optptr++ = DHCP_OPTION_DNS_SERVER;
        *optptr++ = 4;
        *optptr++ = ip4_addr1( &ipadd);
        *optptr++ = ip4_addr2( &ipadd);
        *optptr++ = ip4_addr3( &ipadd);
        *optptr++ = ip4_addr4( &ipadd);
#endif

#ifdef CLASS_B_NET
        *optptr++ = DHCP_OPTION_BROADCAST_ADDRESS;
        *optptr++ = 4;
        *optptr++ = ip4_addr1( &ipadd);
        *optptr++ = 255;
        *optptr++ = 255;
        *optptr++ = 255;
#else
        *optptr++ = DHCP_OPTION_BROADCAST_ADDRESS;
        *optptr++ = 4;
        *optptr++ = ip4_addr1( &ipadd);
        *optptr++ = ip4_addr2( &ipadd);
        *optptr++ = ip4_addr3( &ipadd);
        *optptr++ = 255;
#endif

        *optptr++ = DHCP_OPTION_INTERFACE_MTU;
        *optptr++ = 2;
#ifdef CLASS_B_NET
        *optptr++ = 0x05;
        *optptr++ = 0xdc;
#else
        *optptr++ = 0x02;
        *optptr++ = 0x40;
#endif

        *optptr++ = DHCP_OPTION_PERFORM_ROUTER_DISCOVERY;
        *optptr++ = 1;
        *optptr++ = 0x00;

        *optptr++ = 43;
        *optptr++ = 6;

        *optptr++ = 0x01;
        *optptr++ = 4;
        *optptr++ = 0x00;
        *optptr++ = 0x00;
        *optptr++ = 0x00;
        *optptr++ = 0x02;

        return optptr;
}
///////////////////////////////////////////////////////////////////////////////////
/*
 *       DHCP msg
 *
 * @param optptr -- DHCP msg
 *
 * @return uint8_t*             DHCP msg
 */
///////////////////////////////////////////////////////////////////////////////////
static uint8_t* add_end(uint8_t *optptr)
{

        *optptr++ = DHCP_OPTION_END;
        return optptr;
}
///////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////
static void create_msg(struct dhcps_msg *m)
{
        struct ip_addr client;

        client.addr = client_address.addr;

        m->op = DHCP_REPLY;
        m->htype = DHCP_HTYPE_ETHERNET;
        m->hlen = 6;
        m->hops = 0;
//        MEMCPY((char *) xid, (char *) m->xid, sizeof(m->xid));
        m->secs = 0;
        m->flags = htons(BOOTP_BROADCAST);

        MEMCPY((char *) m->yiaddr, (char *) &client.addr, sizeof(m->yiaddr));

        memset((char *) m->ciaddr, 0, sizeof(m->ciaddr));
        memset((char *) m->siaddr, 0, sizeof(m->siaddr));
        memset((char *) m->giaddr, 0, sizeof(m->giaddr));
        memset((char *) m->sname, 0, sizeof(m->sname));
        memset((char *) m->file, 0, sizeof(m->file));

        memset((char *) m->options, 0, sizeof(m->options));

//For xiaomi crash bug
		uint32_t magic_cookie1 = magic_cookie;
        MEMCPY((char *) m->options, &magic_cookie1, sizeof(magic_cookie1));
}
///////////////////////////////////////////////////////////////////////////////////
/*
 *                     OFFER
 *
 * @param -- m                               DHCP msg
 */
///////////////////////////////////////////////////////////////////////////////////
static void send_offer(struct dhcps_msg *m)
{
        uint8_t *end;
        struct pbuf *p, *q;
        u8_t *data;
        u16_t cnt=0;
        u16_t i;
        create_msg(m);

        end = add_msg_type(&m->options[4], DHCPOFFER);
        end = add_offer_options(end);
        end = add_end(end);

        p = pbuf_alloc(PBUF_TRANSPORT, sizeof(struct dhcps_msg), PBUF_RAM);
        if(p != NULL){

            q = p;
            while(q != NULL){
                data = (u8_t *)q->payload;
                for(i=0; i<q->len; i++)
                {
                    data[i] = ((u8_t *) m)[cnt++];
                }

                q = q->next;
            }
        }else{

            return;
        }
        udp_sendto( pcb_dhcps, p, &broadcast_dhcps, DHCPS_CLIENT_PORT );
        if(p->ref != 0){
            pbuf_free(p);
        }
}
///////////////////////////////////////////////////////////////////////////////////
/*
 *                     NAK
 *
 * @param m                               DHCP msg
 */
///////////////////////////////////////////////////////////////////////////////////
static void send_nak(struct dhcps_msg *m)
{

        u8_t *end;
        struct pbuf *p, *q;
        u8_t *data;
        u16_t cnt=0;
        u16_t i;
        create_msg(m);

        end = add_msg_type(&m->options[4], DHCPNAK);
        end = add_end(end);

        p = pbuf_alloc(PBUF_TRANSPORT, sizeof(struct dhcps_msg), PBUF_RAM);
        if(p != NULL){

            q = p;
            while(q != NULL){
                data = (u8_t *)q->payload;
                for(i=0; i<q->len; i++)
                {
                    data[i] = ((u8_t *) m)[cnt++];
                }

                q = q->next;
            }
        }else{

            return;
        }
        udp_sendto( pcb_dhcps, p, &broadcast_dhcps, DHCPS_CLIENT_PORT );
        if(p->ref != 0){
            pbuf_free(p);
        }
}
///////////////////////////////////////////////////////////////////////////////////
/*
 *                     ACK      DHCP
 *
 * @param m                               DHCP msg
 */
///////////////////////////////////////////////////////////////////////////////////
static void send_ack(struct dhcps_msg *m)
{

        u8_t *end;
        struct pbuf *p, *q;
        u8_t *data;
        u16_t cnt=0;
        u16_t i;
        create_msg(m);

        end = add_msg_type(&m->options[4], DHCPACK);
        end = add_offer_options(end);
        end = add_end(end);

        p = pbuf_alloc(PBUF_TRANSPORT, sizeof(struct dhcps_msg), PBUF_RAM);

        if(p != NULL){

            q = p;
            while(q != NULL){
                data = (u8_t *)q->payload;
                for(i=0; i<q->len; i++)
                {
                    data[i] = ((u8_t *) m)[cnt++];
                }

                q = q->next;
            }
        }else{

            return;
        }
        udp_sendto( pcb_dhcps, p, &broadcast_dhcps, DHCPS_CLIENT_PORT );

        if(p->ref != 0){
            pbuf_free(p);
        }
}
///////////////////////////////////////////////////////////////////////////////////
/*
 *             DHCP                            DHCP                                                            DHCP
 *
 * @param optptr DHCP msg
 * @param len                                 ?(byte)
 *
 * @return uint8_t                           DHCP Server
 */
///////////////////////////////////////////////////////////////////////////////////
static uint8_t parse_options(uint8_t *optptr, int16_t len)
{
        struct ip_addr client;
        _Bool is_dhcp_parse_end = 0;
        struct dhcps_state s;

        client.addr = *( (u32_t *) &client_address);//                  DHCP                IP

        u8_t *end = optptr + len;
        u16_t type = 0;

        s.state = DHCPS_STATE_IDLE;

        while (optptr < end) {
            switch ((int16_t) *optptr) {

                case DHCP_OPTION_MSG_TYPE:  //53
                        type = *(optptr + 2);
                        break;

                case DHCP_OPTION_REQ_IPADDR://50
                        //os_printf("dhcps:0x%08x,0x%08x\n",client.addr,*(uint32_t*)(optptr+2));
                        if( memcmp( (char *) &client.addr, (char *) optptr+2,4)==0 ) {
                            s.state = DHCPS_STATE_ACK;
                        }else {
                            s.state = DHCPS_STATE_NAK;
                        }
                        break;
                case DHCP_OPTION_END:
                        {
                            is_dhcp_parse_end = 1;
                        }
                        break;
            }

            if(is_dhcp_parse_end){
                    break;
            }

            optptr += optptr[1] + 2;
        }

        switch (type){
            case DHCPDISCOVER://1
                s.state = DHCPS_STATE_OFFER;
                break;

            case DHCPREQUEST://3
                if ( !(s.state == DHCPS_STATE_ACK || s.state == DHCPS_STATE_NAK) ) {
                    if(renew == 1) {
                        s.state = DHCPS_STATE_ACK;
                    } else {
                        s.state = DHCPS_STATE_NAK;
                    }
                }
                break;

            case DHCPDECLINE://4
                s.state = DHCPS_STATE_IDLE;
                break;

            case DHCPRELEASE://7
                s.state = DHCPS_STATE_RELEASE;
                break;
        }
        return s.state;
}
///////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////
static int16_t parse_msg(struct dhcps_msg *m, u16_t len)
{
    if(memcmp((char *)m->options,
            &magic_cookie,
            sizeof(magic_cookie)) == 0){
        struct ip_addr ip;
        MEMCPY(&ip.addr,m->ciaddr,sizeof(ip.addr));
        client_address.addr = wifi_softap_dhcps_client_update((char *)m->chaddr,&ip);

        int16_t ret = parse_options(&m->options[4], len);

        if(ret == DHCPS_STATE_RELEASE) {
            wifi_softap_dhcps_client_leave((char *)m->chaddr,&ip,1); // force to delete
            client_address.addr = ip.addr;
        }

        return ret;
    }
    return 0;
}
///////////////////////////////////////////////////////////////////////////////////
/*
 * DHCP                                                                                 LWIP UDP
 *                     udp_recv()                  LWIP                    .
 *
 * @param arg
 * @param pcb               UDP                   ?
 * @param p                     UDP                             ?
 * @param addr               UDP                          IP
 * @param port               UDP                          UDP
 */
///////////////////////////////////////////////////////////////////////////////////
static void handle_dhcp(void *arg,
                                    struct udp_pcb *pcb,
                                    struct pbuf *p,
                                    struct ip_addr *addr,
                                    uint16_t port)
{
        struct dhcps_msg *pmsg_dhcps = NULL;
        int16_t tlen = 0;
        u16_t i = 0;
        u16_t dhcps_msg_cnt = 0;
        u8_t *p_dhcps_msg = NULL;
        u8_t *data = NULL;

        if (p==NULL) return;

        pmsg_dhcps = (struct dhcps_msg *)mem_calloc(1, sizeof(struct dhcps_msg));
        if (NULL == pmsg_dhcps){
            pbuf_free(p);
            return;
        }
        p_dhcps_msg = (u8_t *)pmsg_dhcps;
        tlen = p->tot_len;
        data = p->payload;

        for(i=0; i<p->len; i++){
            p_dhcps_msg[dhcps_msg_cnt++] = data[i];
        }

        if(p->next != NULL) {
            data = p->next->payload;
            for(i=0; i<p->next->len; i++){
                p_dhcps_msg[dhcps_msg_cnt++] = data[i];
            }
        }

        /*
         * DHCP
        */

        switch(parse_msg(pmsg_dhcps, tlen - 240)) {

            case DHCPS_STATE_OFFER://1
                 send_offer(pmsg_dhcps);
                 break;
            case DHCPS_STATE_ACK://3
                 send_ack(pmsg_dhcps);

                 wifi_softap_set_station_info(pmsg_dhcps->chaddr, (void *)&client_address.addr);
                 break;
            case DHCPS_STATE_NAK://4
                 send_nak(pmsg_dhcps);
                 break;
            default :
                 break;
        }
        pbuf_free(p);
        mem_free(pmsg_dhcps);
        pmsg_dhcps = NULL;
}
///////////////////////////////////////////////////////////////////////////////////
static void wifi_softap_init_dhcps_lease(uint32_t ip)
{
    uint32_t softap_ip = 0,local_ip = 0;
    uint32_t start_ip = 0;
    uint32_t end_ip = 0;
//  if (dhcps_lease_flag) {
    if (dhcps_lease.enable == 1) {
        softap_ip = htonl(ip);
        start_ip = htonl(dhcps_lease.start_ip.addr);
        end_ip = htonl(dhcps_lease.end_ip.addr);
        /*config ip information can't contain local ip*/
        if ((start_ip <= softap_ip) && (softap_ip <= end_ip)) {
            dhcps_lease.enable = 0;
        } else {
            /*config ip information must be in the same segment as the local ip*/
            softap_ip >>= 8;
            if (((start_ip >> 8 != softap_ip) || (end_ip >> 8 != softap_ip))
                    || (end_ip - start_ip > DHCPS_MAX_LEASE)) {
                dhcps_lease.enable = 0;
            }
        }
    }

    if (dhcps_lease.enable == 0) {
        local_ip = softap_ip = htonl(ip);
        softap_ip &= 0xFFFFFF00;
        local_ip &= 0xFF;
        if (local_ip >= 0x80)
            local_ip -= DHCPS_MAX_LEASE;
        else
            local_ip ++;

        bzero(&dhcps_lease, sizeof(dhcps_lease));
        dhcps_lease.start_ip.addr = softap_ip | local_ip;
        dhcps_lease.end_ip.addr = softap_ip | (local_ip + DHCPS_MAX_LEASE - 1);
        dhcps_lease.start_ip.addr = htonl(dhcps_lease.start_ip.addr);
        dhcps_lease.end_ip.addr= htonl(dhcps_lease.end_ip.addr);
    }
//  dhcps_lease.start_ip.addr = htonl(dhcps_lease.start_ip.addr);
//  dhcps_lease.end_ip.addr= htonl(dhcps_lease.end_ip.addr);
//  os_printf("start_ip = 0x%x, end_ip = 0x%x\n",dhcps_lease.start_ip, dhcps_lease.end_ip);
}
///////////////////////////////////////////////////////////////////////////////////
void dhcps_start(struct ip_info *info)
{
    struct netif * apnetif = (struct netif *)eagle_lwip_getif(0x01);

    if(apnetif->dhcps_pcb != NULL) {
        udp_remove(apnetif->dhcps_pcb);
    }

    pcb_dhcps = udp_new();

    apnetif->dhcps_pcb = pcb_dhcps;

    IP4_ADDR(&broadcast_dhcps, 255, 255, 255, 255);

    server_address = info->ip;
    wifi_softap_init_dhcps_lease(server_address.addr);

    udp_bind(pcb_dhcps, IP_ADDR_ANY, DHCPS_SERVER_PORT);
    udp_recv(pcb_dhcps, handle_dhcp, NULL);
}

void dhcps_stop(void)
{
    struct netif * apnetif = (struct netif *)eagle_lwip_getif(0x01);

    udp_disconnect(pcb_dhcps);
//  dhcps_lease_flag = 1;
    if(apnetif->dhcps_pcb != NULL) {
        udp_remove(apnetif->dhcps_pcb);
        apnetif->dhcps_pcb = NULL;
    }

    //udp_remove(pcb_dhcps);
    list_node *pnode = NULL;
    list_node *pback_node = NULL;
    struct dhcps_pool* dhcp_node = NULL;
    struct ip_addr ip_zero;

    memset(&ip_zero,0x0,sizeof(ip_zero));
    pnode = unit_plist;
    while (pnode != NULL) {
        pback_node = pnode;
        pnode = pback_node->pnext;
        node_remove_from_list(&unit_plist, pback_node);
        dhcp_node = (struct dhcps_pool*)pback_node->pnode;
        //wifi_softap_dhcps_client_leave(dhcp_node->mac,&dhcp_node->ip,1); // force to delete
        wifi_softap_set_station_info(dhcp_node->mac, &ip_zero);
        mem_free(pback_node->pnode);
        pback_node->pnode = NULL;
        mem_free(pback_node);
        pback_node = NULL;
    }
}

/******************************************************************************
 * FunctionName : wifi_softap_set_dhcps_lease
 * Description  : set the lease information of DHCP server
 * Parameters   : please -- Additional argument to set the lease information,
 *                          Little-Endian.
 * Returns      : 1 or 0
*******************************************************************************/
_Bool wifi_softap_set_dhcps_lease(struct dhcps_lease *please)
{
    struct ip_info info;
    uint32_t softap_ip = 0;
    uint32_t start_ip = 0;
    uint32_t end_ip = 0;

    uint8_t opmode = wifi_get_opmode();

    if (opmode == STATION_MODE || opmode == NULL_MODE) {
        return 0;
    }

    if (please == NULL || wifi_softap_dhcps_status() == DHCP_STARTED)
        return 0;

    if(please->enable) {
        bzero(&info, sizeof(struct ip_info));
        wifi_get_ip_info(SOFTAP_IF, &info);
        softap_ip = htonl(info.ip.addr);
        start_ip = htonl(please->start_ip.addr);
        end_ip = htonl(please->end_ip.addr);

        /*config ip information can't contain local ip*/
        if ((start_ip <= softap_ip) && (softap_ip <= end_ip))
            return 0;

        /*config ip information must be in the same segment as the local ip*/
        softap_ip >>= 8;
        if ((start_ip >> 8 != softap_ip)
                || (end_ip >> 8 != softap_ip)) {
            return 0;
        }

        if (end_ip - start_ip > DHCPS_MAX_LEASE)
            return 0;

        bzero(&dhcps_lease, sizeof(dhcps_lease));
//      dhcps_lease.start_ip.addr = start_ip;
//      dhcps_lease.end_ip.addr = end_ip;
        dhcps_lease.start_ip.addr = please->start_ip.addr;
        dhcps_lease.end_ip.addr = please->end_ip.addr;
    }
    dhcps_lease.enable = please->enable;
//  dhcps_lease_flag = 0;
    return 1;
}

/******************************************************************************
 * FunctionName : wifi_softap_get_dhcps_lease
 * Description  : get the lease information of DHCP server
 * Parameters   : please -- Additional argument to get the lease information,
 *                          Little-Endian.
 * Returns      : 1 or 0
*******************************************************************************/
_Bool wifi_softap_get_dhcps_lease(struct dhcps_lease *please)
{
    uint8_t opmode = wifi_get_opmode();

    if (opmode == STATION_MODE || opmode == NULL_MODE) {
        return 0;
    }

    if (NULL == please)
        return 0;

//  if (dhcps_lease_flag){
    if (dhcps_lease.enable == 0){
        if (wifi_softap_dhcps_status() == DHCP_STOPPED)
            return 0;
    } else {
//      bzero(please, sizeof(dhcps_lease));
//      if (wifi_softap_dhcps_status() == DHCP_STOPPED){
//          please->start_ip.addr = htonl(dhcps_lease.start_ip.addr);
//          please->end_ip.addr = htonl(dhcps_lease.end_ip.addr);
//      }
    }

//  if (wifi_softap_dhcps_status() == DHCP_STARTED){
//      bzero(please, sizeof(dhcps_lease));
//      please->start_ip.addr = dhcps_lease.start_ip.addr;
//      please->end_ip.addr = dhcps_lease.end_ip.addr;
//  }
    please->start_ip.addr = dhcps_lease.start_ip.addr;
    please->end_ip.addr = dhcps_lease.end_ip.addr;
    return 1;
}

static void kill_oldest_dhcps_pool(void)
{
    list_node *pre = NULL, *p = NULL;
    list_node *minpre = NULL, *minp = NULL;
    struct dhcps_pool *pdhcps_pool = NULL, *pmin_pool = NULL;
    pre = unit_plist;
    p = pre->pnext;
    minpre = pre;
    minp = p;
    while (p != NULL){
        pdhcps_pool = p->pnode;
        pmin_pool = minp->pnode;
        if (pdhcps_pool->lease_timer < pmin_pool->lease_timer){
            minp = p;
            minpre = pre;
        }
        pre = p;
        p = p->pnext;
    }

	if(minp)
		minpre->pnext = minp->pnext;

	if(pdhcps_pool)
		pdhcps_pool->state = DHCPS_STATE_OFFLINE;

	if(minp)
	{
		mem_free(minp->pnode);
		minp->pnode = NULL;
		mem_free(minp);
	}
    minp = NULL;
}

void dhcps_coarse_tmr(void);
void dhcps_coarse_tmr(void)
{
    uint8_t num_dhcps_pool = 0;
    list_node *pback_node = NULL;
    list_node *pnode = NULL;
    struct dhcps_pool *pdhcps_pool = NULL;
    pnode = unit_plist;
    while (pnode != NULL) {
        pdhcps_pool = pnode->pnode;
        if ( pdhcps_pool->type == DHCPS_TYPE_DYNAMIC) {
            pdhcps_pool->lease_timer --;
        }
        if (pdhcps_pool->lease_timer == 0){
            pback_node = pnode;
            pnode = pback_node->pnext;
            node_remove_from_list(&unit_plist,pback_node);
            mem_free(pback_node->pnode);
            pback_node->pnode = NULL;
            mem_free(pback_node);
            pback_node = NULL;
        } else {
            pnode = pnode ->pnext;
            num_dhcps_pool ++;
        }
    }

    if (num_dhcps_pool >= MAX_STATION_NUM)
        kill_oldest_dhcps_pool();
}

_Bool wifi_softap_set_dhcps_offer_option(uint8_t level, void* optarg)
{
    _Bool offer_flag = 1;
    if (optarg == NULL && wifi_softap_dhcps_status() == 0)
        return 0;

    if (level <= OFFER_START || level >= OFFER_END)
        return 0;

    switch (level){
        case OFFER_ROUTER:
            offer = (*(uint8_t *)optarg) & 0x01;
            offer_flag = 1;
            break;
        default :
            offer_flag = 0;
            break;
    }
    return offer_flag;
}

_Bool wifi_softap_set_dhcps_lease_time(uint32_t minute)
{
    uint8_t opmode = wifi_get_opmode();

    if (opmode == STATION_MODE || opmode == NULL_MODE) {
        return 0;
    }

    if (wifi_softap_dhcps_status() == DHCP_STARTED) {
        return 0;
    }

    if(minute == 0) {
        return 0;
    }
    dhcps_lease_time = minute;
    return 1;
}

_Bool wifi_softap_reset_dhcps_lease_time(void)
{
    uint8_t opmode = wifi_get_opmode();

    if (opmode == STATION_MODE || opmode == NULL_MODE) {
        return 0;
    }

    if (wifi_softap_dhcps_status() == DHCP_STARTED) {
        return 0;
    }
    dhcps_lease_time = DHCPS_LEASE_TIME_DEF;
    return 1;
}

uint32_t wifi_softap_get_dhcps_lease_time(void) // minute
{
    return dhcps_lease_time;
}

void wifi_softap_dhcps_client_leave(char *bssid, struct ip_addr *ip,_Bool force)
{
    struct dhcps_pool *pdhcps_pool = NULL;
    list_node *pback_node = NULL;

    if ((bssid == NULL) || (ip == NULL)) {
        return;
    }

    for (pback_node = unit_plist; pback_node != NULL;pback_node = pback_node->pnext) {
        pdhcps_pool = pback_node->pnode;
        if (memcmp(pdhcps_pool->mac, bssid, sizeof(pdhcps_pool->mac)) == 0){
            if (memcmp(&pdhcps_pool->ip.addr, &ip->addr, sizeof(pdhcps_pool->ip.addr)) == 0) {
                if ((pdhcps_pool->type == DHCPS_TYPE_STATIC) || (force)) {
                    if(pback_node != NULL) {
                        node_remove_from_list(&unit_plist,pback_node);
                        mem_free(pback_node);
                        pback_node = NULL;
                    }

                    if (pdhcps_pool != NULL) {
                        mem_free(pdhcps_pool);
                        pdhcps_pool = NULL;
                    }
                } else {
                    pdhcps_pool->state = DHCPS_STATE_OFFLINE;
                }

                struct ip_addr ip_zero;
                memset(&ip_zero,0x0,sizeof(ip_zero));
                wifi_softap_set_station_info((const uint8_t *)bssid, &ip_zero);
                break;
            }
        }
    }
}

uint32_t wifi_softap_dhcps_client_update(char *bssid, struct ip_addr *ip)
{
    struct dhcps_pool *pdhcps_pool = NULL;
    list_node *pback_node = NULL;
    list_node *pmac_node = NULL;
    list_node *pip_node = NULL;
    _Bool flag = 0;
    uint32_t start_ip = dhcps_lease.start_ip.addr;
    uint32_t end_ip = dhcps_lease.end_ip.addr;
    dhcps_type_t type = DHCPS_TYPE_DYNAMIC;
    if (bssid == NULL) {
        return IPADDR_ANY;
    }

    if (ip) {
        if (IPADDR_BROADCAST == ip->addr) {
            return IPADDR_ANY;
        } else if (IPADDR_ANY == ip->addr) {
            ip = NULL;
        } else {
            type = DHCPS_TYPE_STATIC;
        }
    }

    renew = 0;
    for (pback_node = unit_plist; pback_node != NULL;pback_node = pback_node->pnext) {
        pdhcps_pool = pback_node->pnode;
        //os_printf("mac:"MACSTR"bssid:"MACSTR"\r\n",MAC2STR(pdhcps_pool->mac),MAC2STR(bssid));
        if (memcmp(pdhcps_pool->mac, bssid, sizeof(pdhcps_pool->mac)) == 0){
            pmac_node = pback_node;
            if (ip == NULL) {
                flag = 1;
                break;
            }
        }
        if (ip != NULL) {
            if (memcmp(&pdhcps_pool->ip.addr, &ip->addr, sizeof(pdhcps_pool->ip.addr)) == 0) {
                pip_node = pback_node;
            }
        } else if (flag == 0){
            if (memcmp(&pdhcps_pool->ip.addr, &start_ip, sizeof(pdhcps_pool->ip.addr)) != 0) {
                flag = 1;
            } else {
                start_ip = htonl((ntohl(start_ip) + 1));
            }
        }
    }

    if ((ip == NULL) && (flag == 0)) {
        if (unit_plist == NULL) {
            if (start_ip <= end_ip) {
                flag = 1;
            } else {
                return IPADDR_ANY;
            }
        } else {
            if (start_ip > end_ip) {
                return IPADDR_ANY;
            }
            //start_ip = htonl((ntohl(start_ip) + 1));
            flag = 1;
        }
    }

    if (pmac_node != NULL) { // update new ip
        if (pip_node != NULL){
            pdhcps_pool = pip_node->pnode;

            if (pip_node != pmac_node) {
                if(pdhcps_pool->state != DHCPS_STATE_OFFLINE) { // ip is used
                    return IPADDR_ANY;
                }

                // mac exists and ip exists in other node,delete mac
                node_remove_from_list(&unit_plist,pmac_node);
                mem_free(pmac_node->pnode);
                pmac_node->pnode = NULL;
                mem_free(pmac_node);
                pmac_node = pip_node;
                MEMCPY(pdhcps_pool->mac, bssid, sizeof(pdhcps_pool->mac));
            } else {
                renew = 1;
                type = DHCPS_TYPE_DYNAMIC;
            }

            pdhcps_pool->lease_timer = DHCPS_LEASE_TIMER;
            pdhcps_pool->type = type;
            pdhcps_pool->state = DHCPS_STATE_ONLINE;

        } else {
            pdhcps_pool = pmac_node->pnode;
            if (ip != NULL) {
                pdhcps_pool->ip.addr = ip->addr;
            } else if (flag == 1) {
                pdhcps_pool->ip.addr = start_ip;
            } else {    // no ip to distribute
                return IPADDR_ANY;
            }

            node_remove_from_list(&unit_plist,pmac_node);
            pdhcps_pool->lease_timer = DHCPS_LEASE_TIMER;
            pdhcps_pool->type = type;
            pdhcps_pool->state = DHCPS_STATE_ONLINE;
            node_insert_to_list(&unit_plist,pmac_node);
        }
    } else { // new station
        if (pip_node != NULL) { // maybe ip has used
            pdhcps_pool = pip_node->pnode;
            if (pdhcps_pool->state != DHCPS_STATE_OFFLINE) {
                return IPADDR_ANY;
            }
            MEMCPY(pdhcps_pool->mac, bssid, sizeof(pdhcps_pool->mac));
            pdhcps_pool->lease_timer = DHCPS_LEASE_TIMER;
            pdhcps_pool->type = type;
            pdhcps_pool->state = DHCPS_STATE_ONLINE;
        } else {
            pdhcps_pool = (struct dhcps_pool *)mem_calloc(1, sizeof(struct dhcps_pool));
            if (ip != NULL) {
                pdhcps_pool->ip.addr = ip->addr;
            } else if (flag == 1) {
                pdhcps_pool->ip.addr = start_ip;
            } else {    // no ip to distribute
                mem_free(pdhcps_pool);
                return IPADDR_ANY;
            }
            if (pdhcps_pool->ip.addr > end_ip) {
                mem_free(pdhcps_pool);
                return IPADDR_ANY;
            }
            MEMCPY(pdhcps_pool->mac, bssid, sizeof(pdhcps_pool->mac));
            pdhcps_pool->lease_timer = DHCPS_LEASE_TIMER;
            pdhcps_pool->type = type;
            pdhcps_pool->state = DHCPS_STATE_ONLINE;
            pback_node = (list_node *)mem_calloc(1, sizeof(list_node ));
            pback_node->pnode = pdhcps_pool;
            pback_node->pnext = NULL;
            node_insert_to_list(&unit_plist,pback_node);
        }
    }

    return pdhcps_pool->ip.addr;
}
