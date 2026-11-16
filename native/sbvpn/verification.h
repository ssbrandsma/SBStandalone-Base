#ifndef SB_VPN_VERIFICATION_H
#define SB_VPN_VERIFICATION_H
#include <stddef.h>

typedef struct {
	char code[3], country[96], region[128], city[128];
} VpnLocation;

int vpn_verify_ip(const char *, size_t, char[16]);
int vpn_verify_location(const char *, size_t, const char *, VpnLocation *);
#endif
