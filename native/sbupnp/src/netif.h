#ifndef SBUPNP_NETIF_H
#define SBUPNP_NETIF_H
#include <stddef.h>
int sbupnp_interface_allowed(const char *name, unsigned flags, const char *ipv4);
int sbupnp_choose_interface(const char *requested, char *name, size_t capacity);
#endif
