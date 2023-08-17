/* WiFi station Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdlib.h>

#include "AppMain.h"
#include "AppMediaSrc_FileSrc.h"

#include "kernel.h"
#include "netdev_ipaddr.h"
#include "netdev.h"

#define IPSTR "%d.%d.%d.%d"
#define app_ip4_addr_get_byte(ipaddr, idx) (((const uint8_t*)ipaddr)[idx])
#define app_ip4_addr1(ipaddr) app_ip4_addr_get_byte(ipaddr, 0)
#define app_ip4_addr2(ipaddr) app_ip4_addr_get_byte(ipaddr, 1)
#define app_ip4_addr3(ipaddr) app_ip4_addr_get_byte(ipaddr, 2)
#define app_ip4_addr4(ipaddr) app_ip4_addr_get_byte(ipaddr, 3)

#define app_ip4_addr1_16(ipaddr) ((uint16_t)app_ip4_addr1(ipaddr))
#define app_ip4_addr2_16(ipaddr) ((uint16_t)app_ip4_addr2(ipaddr))
#define app_ip4_addr3_16(ipaddr) ((uint16_t)app_ip4_addr3(ipaddr))
#define app_ip4_addr4_16(ipaddr) ((uint16_t)app_ip4_addr4(ipaddr))

#define IP2STR(ipaddr) app_ip4_addr1_16(ipaddr), \
    app_ip4_addr2_16(ipaddr), \
    app_ip4_addr3_16(ipaddr), \
    app_ip4_addr4_16(ipaddr)

static char eth0_ip[72];

char* app_get_ip( void )
{
	struct netdev *pdev;

    pdev = netdev_get_by_name("eth0");
    if (pdev == NULL)
    {
        printf("cannot get eth0 device\n");
        return NULL;
    }

    memset(eth0_ip, 0, sizeof(eth0_ip)/sizeof(eth0_ip[0]));
    // printf("eth0 addr:%s, %d\n", inet_ntoa(pdev->ip_addr), inet_addr(inet_ntoa(pdev->ip_addr)));
    uint32_t addr = inet_addr(inet_ntoa(pdev->ip_addr));
    printf("Connected with IP Address:" IPSTR "\n", IP2STR(&addr));
    memcpy(eth0_ip, &addr, 4);
    // printf("Trans IP Address: %d.%d.%d.%d\n", eth0_ip[0], eth0_ip[1], eth0_ip[2], eth0_ip[3]);

    return eth0_ip;
}

static void *webrtc_test_thread(void *argv)
{
    WebRTCAppMain(&gAppMediaSrc);
    return NULL;
}

static int webrtc_test(int argc, char **argv)
{
    printf("enter %s,build time(%s)\n", __func__, V_BUILD_TIME);

    //create thread for webrtc test
    pthread_t tid;
    pthread_attr_t attr;
    struct sched_param sched;
    unsigned char sched_policy = SCHED_OTHER;

    sched.sched_priority = 20;
    pthread_attr_setschedparam(&attr, &sched);
    pthread_attr_setschedpolicy(&attr, sched_policy);

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    pthread_create(&tid, &attr, webrtc_test_thread, NULL);
    pthread_setname_np(tid, "webrtc_test");
}

SHELL_CMD_EXPORT_ALIAS(webrtc_test, webrtc_test, webrtc demo);
