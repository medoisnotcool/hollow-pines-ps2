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
/* Making a socket non-blocking. In this SDK lwip_ioctl() is a macro that calls plain ioctl(), which isn't in the
 * libraries we link. So we look up each possible function by its real symbol name. They are 'weak', meaning a missing
 * one just becomes NULL instead of breaking the link; set_nonblock() then uses whichever one exists. */
extern int p_lwip_ioctl(int, long, void *) __asm__("lwip_ioctl") __attribute__((weak));
extern int p_ioctl(int, int, ...)          __asm__("ioctl")      __attribute__((weak));
extern int p_lwip_fcntl(int, int, int)     __asm__("lwip_fcntl") __attribute__((weak));
static void set_nonblock(int s) {
    int on = 1;
    if (p_lwip_ioctl) { p_lwip_ioctl(s, FIONBIO, &on); return; }
    if (p_ioctl)      { p_ioctl(s, FIONBIO, &on); return; }
#if defined(F_SETFL) && defined(O_NONBLOCK)
    if (p_lwip_fcntl) { p_lwip_fcntl(s, F_SETFL, O_NONBLOCK); return; }
#endif
    /* none available: the socket stays blocking (a connect to an unreachable PC may pause the game for a few seconds) */
}

extern unsigned char dev9_irx[];   extern unsigned int size_dev9_irx;
extern unsigned char netman_irx[]; extern unsigned int size_netman_irx;
extern unsigned char smap_irx[];   extern unsigned int size_smap_irx;

static int inited, ready, sock = -1, cst;
static char status[48] = "NETWORK OFF";
static int dhcp_wait;   /* frames spent waiting for an address */

/* "192.168.1.100" -> network-order address, 1 = ok. Replaces inet_addr(), which this SDK doesn't declare. */
static int parse_ip(const char *s, unsigned *out) {
    unsigned a, b, c, d; char extra; struct ip4_addr x;
    if (!s || sscanf(s, "%u.%u.%u.%u%c", &a, &b, &c, &d, &extra) != 4 || a > 255 || b > 255 || c > 255 || d > 255) return 0;
    IP4_ADDR(&x, a, b, c, d); *out = (unsigned)x.addr; return 1;
}

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
    if (ps2ip_getconfig("sm0", &info) >= 0 && info.ipaddr.s_addr != 0) { ready = 1; snprintf(status, sizeof status, "NETWORK READY"); return; }
    dhcp_wait++;
    if (dhcp_wait > 60 * 15) snprintf(status, sizeof status, "NO IP ADDRESS - CHECK CABLE");
}
int net_ready(void) { return ready; }
const char *net_status(void) { return status; }

int net_open(const char *ip, int port) {
    net_close();
    struct sockaddr_in sa; unsigned addr; memset(&sa, 0, sizeof sa);
    if (!parse_ip(ip, &addr)) { cst = -1; return -1; }
    sa.sin_family = AF_INET; sa.sin_port = htons((unsigned short)port); sa.sin_addr.s_addr = addr;
    sock = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { cst = -1; return -1; }
    set_nonblock(sock);
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
