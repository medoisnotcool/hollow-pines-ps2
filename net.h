/* Thin TCP layer. net_ps2.c = the real PS2 (lwIP via ps2ip/netman), net_host.c = desktop test build. */
#ifndef NET_H
#define NET_H
int  net_init(void);                 /* 0 = ok. PS2: loads the network drivers. Safe to call twice. */
void net_poll(void);                 /* call every frame */
int  net_ready(void);                /* 1 once the PS2 has an IP address */
int  net_failed(void);               /* 1 if the network drivers could not start (status text says why) */
const char *net_status(void);        /* short text for the screen */
int  net_open(const char *ip, int port);   /* start a non-blocking connect; 0 = started */
int  net_state(void);                /* 0 idle, 1 connecting, 2 connected, -1 failed */
int  net_write(const char *d, int n);
int  net_read(char *d, int n);       /* >0 bytes, 0 nothing yet, -1 connection closed */
void net_close(void);
#endif
