/* PS2 network layer.
 * ALL network work (loading drivers, getting an IP, connecting, sending, receiving) runs in a background thread,
 * so the game never freezes: if something stalls, the screen keeps updating and shows the exact step it is stuck on.
 * Uses the ps2sdk network stack: DEV9 + NETMAN + SMAP on the IOP, lwIP (libps2ip) on the EE.
 * (Based on the version that compiled for you; the changes are the worker thread and the ring buffers.) */
#include "net.h"
#include "net_config.h"
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <string.h>
#include <stdio.h>
#include <netman.h>
#include <ps2ip.h>
/* DelayThread() is declared in its own header in the PS2SDK; fall back to usleep() if that header isn't there */
#if defined(__has_include)
# if __has_include(<delaythread.h>)
#  include <delaythread.h>
#  define HAVE_DELAYTHREAD 1
# endif
#endif
#ifndef HAVE_DELAYTHREAD
# include <unistd.h>
# define DelayThread(us) usleep(us)
#endif

extern void *_gp;
extern unsigned char dev9_irx[];   extern unsigned int size_dev9_irx;
extern unsigned char netman_irx[]; extern unsigned int size_netman_irx;
extern unsigned char smap_irx[];   extern unsigned int size_smap_irx;

#define RXN 32768
#define TXN 4096
static volatile int started, failed, ipready, cst, closed;          /* cst: 0 idle, 1 connecting, 2 connected, -1 failed */
static volatile int req_gen, conn_gen, req_want, req_port;          /* main thread bumps req_gen to ask for a connect/close */
static char req_ip[20];
static char status[48] = "NETWORK OFF";
static unsigned char rx[RXN], tx[TXN];
static volatile int rxh, rxt, txh, txt;                             /* head = next write, tail = next read */
static int sock = -1;
static char wstack[65536] __attribute__((aligned(16)));

static void setst(const char *s) { snprintf(status, sizeof status, "%s", s); }

/* "192.168.1.100" -> network-order address, 1 = ok. Replaces inet_addr(), which this SDK doesn't declare. */
static int parse_ip(const char *s, unsigned *out) {
    unsigned a, b, c, d; char extra; struct ip4_addr x;
    if (!s || sscanf(s, "%u.%u.%u.%u%c", &a, &b, &c, &d, &extra) != 4 || a > 255 || b > 255 || c > 255 || d > 255) return 0;
    IP4_ADDR(&x, a, b, c, d); *out = (unsigned)x.addr; return 1;
}
static int load_irx(void *buf, unsigned size) { int ret = 0; int id = SifExecModuleBuffer(buf, size, 0, NULL, &ret); return id >= 0 && ret != 1; }

static int bring_up(void) {
    setst("NETWORK STEP 1/6: LOADING DEV9");
    if (!load_irx(dev9_irx, size_dev9_irx)) { setst("DEV9 DRIVER FAILED"); return -1; }
    setst("NETWORK STEP 2/6: LOADING NETMAN");
    if (!load_irx(netman_irx, size_netman_irx)) { setst("NETMAN DRIVER FAILED"); return -2; }
    setst("NETWORK STEP 3/6: LOADING SMAP");
    if (!load_irx(smap_irx, size_smap_irx)) { setst("NO NETWORK ADAPTER FOUND"); return -3; }
    setst("NETWORK STEP 4/6: STARTING NETMAN");
    if (NetManInit() < 0) { setst("NETMAN INIT FAILED"); return -4; }
    setst("NETWORK STEP 5/6: STARTING IP STACK");
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
    return 0;
}

static void do_connect(int g) {
    unsigned addr; struct sockaddr_in sa; memset(&sa, 0, sizeof sa);
    if (!parse_ip(req_ip, &addr)) { cst = -1; return; }
    sa.sin_family = AF_INET; sa.sin_port = htons((unsigned short)req_port); sa.sin_addr.s_addr = addr;
    int s = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { cst = -1; return; }
    setst("CONNECTING TO THE RELAY");
    int r = lwip_connect(s, (struct sockaddr *)&sa, sizeof sa);       /* may take a while: fine, we are not the game thread */
    if (g != req_gen) { lwip_close(s); return; }                      /* the player cancelled/retried meanwhile */
    if (r == 0) { sock = s; closed = 0; cst = 2; setst("CONNECTED TO RELAY"); } else { lwip_close(s); cst = -1; setst("RELAY REFUSED OR NOT REACHABLE"); }
}
static void pump_io(void) {
    while (txt != txh && sock >= 0) {                                 /* send what the game queued */
        int n = (txh > txt ? txh : TXN) - txt, r = lwip_send(sock, tx + txt, n, 0);
        if (r <= 0) { closed = 1; lwip_close(sock); sock = -1; return; }
        txt = (txt + r) % TXN;
    }
    int used = (rxh - rxt + RXN) % RXN;
    if (RXN - 1 - used < 1100) { DelayThread(5000); return; }         /* receive buffer nearly full: let the game catch up */
    fd_set rs; FD_ZERO(&rs); FD_SET(sock, &rs); struct timeval tv = {0, 20000};
    if (lwip_select(sock + 1, &rs, NULL, NULL, &tv) > 0) {
        unsigned char t[1024]; int r = lwip_recv(sock, t, sizeof t, 0);
        if (r <= 0) { closed = 1; lwip_close(sock); sock = -1; return; }
        for (int i = 0; i < r; i++) { rx[rxh] = t[i]; rxh = (rxh + 1) % RXN; }
    }
}
static void worker(void *arg) {
    (void)arg;
    if (bring_up() < 0) { failed = 1; for (;;) DelayThread(1000000); }
    for (int waited = 0;; waited++) {                                 /* wait for the router to hand out an address */
        t_ip_info info; char b[48];
        if (ps2ip_getconfig("sm0", &info) >= 0 && info.ipaddr.s_addr != 0) break;
        snprintf(b, sizeof b, "NETWORK STEP 6/6: WAITING FOR IP (%ds)", waited / 5); setst(b);
        DelayThread(200000);
    }
    ipready = 1; setst("NETWORK READY");
    for (;;) {
        int g = req_gen;
        if (g != conn_gen) {
            conn_gen = g;
            if (sock >= 0) { lwip_close(sock); sock = -1; }
            if (req_want) do_connect(g);
        }
        if (sock >= 0 && cst == 2) pump_io(); else DelayThread(20000);
    }
}

/* ---------------------------------------------------------------- net.h (called by the game thread; never blocks) */
int net_init(void) {
    if (started) return 0;
    ee_thread_t t; memset(&t, 0, sizeof t);
    t.func = (void *)worker; t.stack = wstack; t.stack_size = sizeof wstack; t.gp_reg = &_gp; t.initial_priority = 0x20;
    int id = CreateThread(&t);
    if (id < 0) { setst("COULD NOT CREATE NETWORK THREAD"); failed = 1; return -1; }
    if (StartThread(id, NULL) < 0) { setst("COULD NOT START NETWORK THREAD"); failed = 1; return -1; }
    started = 1; setst("NETWORK STARTING..."); return 0;
}
void net_poll(void) {}
int net_ready(void) { return ipready; }
int net_failed(void) { return failed; }
const char *net_status(void) { return status; }
int net_open(const char *ip, int port) {
    snprintf(req_ip, sizeof req_ip, "%s", ip); req_port = port;
    rxh = rxt = txh = txt = 0; closed = 0; cst = 1; req_want = 1; req_gen++;
    return 0;
}
int net_state(void) { return cst; }
int net_write(const char *d, int n) {
    if (cst != 2 || closed) return -1;
    int used = (txh - txt + TXN) % TXN;
    if (n > TXN - 1 - used) return 0;                                 /* queue full: drop (state updates repeat anyway) */
    for (int i = 0; i < n; i++) { tx[txh] = (unsigned char)d[i]; txh = (txh + 1) % TXN; }
    return n;
}
int net_read(char *d, int n) {
    int avail = (rxh - rxt + RXN) % RXN;
    if (!avail) return closed ? -1 : 0;
    if (n > avail) n = avail;
    for (int i = 0; i < n; i++) { d[i] = (char)rx[rxt]; rxt = (rxt + 1) % RXN; }
    return n;
}
void net_close(void) { req_want = 0; cst = 0; closed = 0; rxh = rxt = txh = txt = 0; req_gen++; }
