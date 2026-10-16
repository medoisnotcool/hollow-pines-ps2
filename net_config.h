/* ---- network settings (edit these if you like, then rebuild) ---- */
#ifndef NET_CONFIG_H
#define NET_CONFIG_H
#define NET_PORT 7777                    /* must match the relay (TCP_PORT, default 7777) */
#define RELAY_IP_DEFAULT 192, 168, 1, 100 /* shown on the connect screen at start; change it there with the D-pad */

#define NET_USE_DHCP 1                   /* 1: router gives the PS2 an address. 0: use the fixed address below */
#define PS2_STATIC_IP   192, 168, 1, 50  /* only used when NET_USE_DHCP is 0 */
#define PS2_STATIC_MASK 255, 255, 255, 0
#define PS2_STATIC_GW   192, 168, 1, 1
#endif
