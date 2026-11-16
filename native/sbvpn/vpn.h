#ifndef SB_VPN_H
#define SB_VPN_H
#include "mongoose.h"
#include <stdint.h>
#include <sys/types.h>

static inline void vpn_wipe(void *memory, size_t size)
{
	volatile unsigned char *p = memory;
	while (size--)
		*p++ = 0;
}

#define VPN_LIMIT 131072u
#define VPN_CONFIG_LIMIT 49152u
#define VPN_PROFILES 16
#define VPN_SOCKET "/tmp/standalonebase/vpn/control.sock"
#define VPN_INTERFACE "sbvpn0"

typedef struct {
	char config[VPN_CONFIG_LIMIT + 1];
	char tls_min[8];
	char host[8][254];
	unsigned port[8];
	char proto[8][16];
	int remotes, needs_auth, has_ca, has_cert, has_key;
} VpnProfile;

typedef struct {
	int startup, reconnect, kill, dns, country_verify;
	char selected[33], dns_server[16];
} VpnSettings;

typedef struct {
	char physical[16], gateway[16], subnet[32], endpoint[16], protocol[16];
	unsigned port;
	char endpoint_interface[16], endpoint_gateway[16], local_subnets[8][32],
		local_interfaces[8][16];
	int local_count;
	char local_ipv6[8][96];
	int local_ipv6_count;
	uint64_t address_signature;
	char tunnel_gateway[16], tunnel_address[16], tunnel_netmask[16],
		tunnel_peer[16], dns[3][16];
	int dns_count, routes, guard4, guard6, dns_active, tunnel_mtu;
	char resolver[512], original_dns[2048], owned_dns[256];
} VpnNetwork;

int vpn_json_valid(const char *, size_t);
int vpn_profile_parse(const char *, VpnProfile *, char *, size_t);
int vpn_id(const char *);
int vpn_private_dir(const char *);
int vpn_store_open(const char *);
char *vpn_store_read(int, const char *, size_t);
int vpn_store_write(int, const char *, const char *);
int vpn_store_delete(int, const char *);
char *vpn_public_profile(struct mg_str, const char *);
int vpn_process_record(int, pid_t, const char *);
int vpn_process_recover(int);
int vpn_network_refresh_dns(VpnNetwork *, const char *);
int vpn_exec(const char *, char *const[], char *, size_t, unsigned);
const char *vpn_program(const char *);
int vpn_network_discover(VpnNetwork *);
int vpn_network_policy_standard(void);
int vpn_network_conflicts(const VpnNetwork *);
void vpn_network_endpoint(VpnNetwork *);
int vpn_network_apply(VpnNetwork *, const VpnSettings *, const char *);
int vpn_network_cleanup(VpnNetwork *, int, const char *);
int vpn_network_guard(VpnNetwork *, const VpnSettings *, const char *);
int vpn_network_dns(VpnNetwork *, const VpnSettings *, const char *);
int vpn_network_journal(VpnNetwork *, const char *);
int vpn_network_recover(VpnNetwork *, const char *);
int vpn_ipv6(void);
int vpn_firewall_capability(int);
int vpn_dns_capability(void);
int vpn_trusted_dir(const char *);
int vpn_socket(int, int, int, int);
int vpn_rpc_connect(const char *, int);
char *vpn_rpc(const char *, const char *);
#endif
