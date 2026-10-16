/* PS2 network layer: loads the drivers, gets an IP (DHCP) and gives mp.c a simple non-blocking TCP socket.
 * Uses the ps2sdk network stack: DEV9 + NETMAN + SMAP on the IOP, lwIP (libps2ip) on the EE.
 * If the compiler complains about a function name in THIS file, tell me the exact error - everything
 * network related is kept here on purpose, the rest of the game never touches the network stack directly. */
#include "net.h"
#include "net_config.h"
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <string.h>
#include <stdio.h>
#include <netman.h>
#include <ps2ip.h>

extern unsigned char dev9_irx[];   extern unsigned int size_dev9_irx;
extern unsigned char netman_irx[]; extern unsigned int size_netman_irx;
extern unsigned char smap_irx[];   extern unsigned int size_smap_irx;

static int inited, ready, sock = -1, cst;
static char status[48] = "NETWORK OFF";
static int dhcp_wait;   /* frames spent waiting for an address */

static int load_irx(void *buf, unsigned size) { int ret = 0; int id = SifExecModuleBuffer(buf, size, 0, NULL, &ret); return id >= 0 && ret != 1; }

int net_init(void) {
    if (inited) return 0;
    if (!load_irx(dev9_irx, size_dev9_irx)) { snprintf(status, sizeof status, "DEV9 DRIVER FAILED"); return -1; }
    if (!load_irx(netman_irx, size_netman_irx)) { snprintf(status, sizeof status, "NETMAN DRIVER FAILED"); return -2; }
    if (!load_irx(smap_irx, size_smap_irx)) { snprintf(status, sizeof status, "NO NETWORK ADAPTER FOUND"); return -3; }
    if (NetManInit() < 0) { snprintf(status, sizeof status, "NETMAN INIT FAILED"); return -4; }
    {
        struct ip4_addr ip, nm, gw;
        IP4_ADDR(&ip, 0, 0, 0, 0); IP4_ADDR(&nm, 0, 0, 0, 0); IP4_ADDR(&gw, 0, 0, 0, 0);
#if !NET_USE_DHCP
        IP4_ADDR(&ip, PS2_STATIC_IP); IP4_ADDR(&nm, PS2_STATIC_MASK); IP4_ADDR(&gw, PS2_STATIC_GW);
#endif
        ps2ipInit(&ip, &nm, &gw);
#if NET_USE_DHCP
        { t_ip_info info; if (ps2ip_getconfig("sm0", &info) >= 0) { info.dhcp_enabled = 1; ps2ip_setconfig(&info); } }
#endif
    }
    inited = 1; dhcp_wait = 0; snprintf(status, sizeof status, "WAITING FOR IP ADDRESS...");
    return 0;
}
void net_poll(void) {
    if (!inited || ready) return;
    t_ip_info info;
    if (ps2ip_getconfig("sm0", &info) >= 0 && info.ipaddr.addr != 0) { ready = 1; snprintf(status, sizeof status, "NETWORK READY"); return; }
    dhcp_wait++;
    if (dhcp_wait > 60 * 15) snprintf(status, sizeof status, "NO IP ADDRESS - CHECK CABLE");
}
int net_ready(void) { return ready; }
const char *net_status(void) { return status; }

int net_open(const char *ip, int port) {
    net_close();
    struct sockaddr_in sa; memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET; sa.sin_port = htons((unsigned short)port); sa.sin_addr.s_addr = inet_addr(ip);
    sock = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { cst = -1; return -1; }
    { int on = 1; lwip_ioctl(sock, FIONBIO, &on); }
    if (lwip_connect(sock, (struct sockaddr *)&sa, sizeof sa) == 0) cst = 2; else cst = 1;   /* "failed" here just means "in progress" */
    return 0;
}
int net_state(void) {
    if (cst == 1 && sock >= 0) {
        fd_set w; FD_ZERO(&w); FD_SET(sock, &w); struct timeval tv = {0, 0};
        if (lwip_select(sock + 1, NULL, &w, NULL, &tv) > 0) {
            int err = 0; socklen_t l = sizeof err;
            lwip_getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &l); cst = err ? -1 : 2;
        }
    }
    return cst;
}
int net_write(const char *d, int n) { return sock < 0 ? -1 : lwip_send(sock, d, n, 0); }
int net_read(char *d, int n) {
    if (sock < 0) return -1;
    fd_set r; FD_ZERO(&r); FD_SET(sock, &r); struct timeval tv = {0, 0};
    if (lwip_select(sock + 1, &r, NULL, NULL, &tv) <= 0) return 0;     /* nothing waiting */
    int k = lwip_recv(sock, d, n, 0);
    return k > 0 ? k : -1;                                              /* readable but 0/-1 bytes = closed */
}
void net_close(void) { if (sock >= 0) lwip_close(sock); sock = -1; cst = 0; }
