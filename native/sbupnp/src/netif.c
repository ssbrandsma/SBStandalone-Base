#define _GNU_SOURCE
#include "netif.h"
#include "core.h"
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <stdio.h>
#include <string.h>

int sbupnp_interface_allowed(const char *name, unsigned flags, const char *ipv4)
{
    char url[64];
    if (!name || !ipv4 || !(flags & IFF_UP) || !(flags & IFF_MULTICAST) ||
        (flags & (IFF_LOOPBACK | IFF_POINTOPOINT))) return 0;
    /* OpenVPN and common routed overlays must never become the SSDP LAN. */
    const char *tunnels[] = {"tun", "tap", "wg", "tailscale"};
    for (unsigned i = 0; i < sizeof tunnels / sizeof *tunnels; i++)
        if (!strncmp(name, tunnels[i], strlen(tunnels[i]))) return 0;
    if (snprintf(url, sizeof url, "http://%s:49152/", ipv4) >= (int)sizeof url) return 0;
    return sbupnp_url(url, NULL);
}

int sbupnp_choose_interface(const char *requested, char *name, size_t capacity)
{
    struct ifaddrs *addresses = NULL;
    int found = 0;
    if (!name || !capacity || getifaddrs(&addresses)) return 0;
    for (struct ifaddrs *p = addresses; p; p = p->ifa_next) {
        char ip[INET_ADDRSTRLEN];
        if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET ||
            (requested && strcmp(requested, p->ifa_name)) ||
            !inet_ntop(AF_INET, &((struct sockaddr_in *)p->ifa_addr)->sin_addr, ip, sizeof ip) ||
            !sbupnp_interface_allowed(p->ifa_name, p->ifa_flags, ip)) continue;
        if (strlen(p->ifa_name) >= capacity) continue;
        strcpy(name, p->ifa_name);
        found = 1;
        break;
    }
    freeifaddrs(addresses);
    return found;
}
