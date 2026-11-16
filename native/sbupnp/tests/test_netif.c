#define _GNU_SOURCE
#include "netif.h"
#include <assert.h>
#include <net/if.h>
#include <stdio.h>
int main(int argc, char **argv)
{
    unsigned up = IFF_UP | IFF_MULTICAST;
    assert(sbupnp_interface_allowed("wlan0", up, "192.168.1.78"));
    assert(sbupnp_interface_allowed("br0", up, "10.0.0.2"));
    assert(!sbupnp_interface_allowed("tun0", up, "10.8.0.2"));
    assert(!sbupnp_interface_allowed("vpn0", up | IFF_POINTOPOINT, "10.8.0.2"));
    assert(!sbupnp_interface_allowed("lo", up | IFF_LOOPBACK, "127.0.0.1"));
    assert(!sbupnp_interface_allowed("eth0", up, "8.8.8.8"));
    assert(!sbupnp_interface_allowed("eth0", IFF_MULTICAST, "192.168.1.2"));
    char name[IF_NAMESIZE];
    if (!sbupnp_choose_interface(argc > 1 ? argv[1] : NULL, name, sizeof name)) return 1;
    puts(name);
    return 0;
}
