#ifndef SB_VPN_TRAFFIC_H
#define SB_VPN_TRAFFIC_H
#include <stdint.h>
#include <stddef.h>

typedef struct {
	uint64_t rx, tx, rx_packets, tx_packets;
	unsigned index;
	int source; /* 1=sysfs, 2=proc; pinned per tunnel attachment */
} VpnTrafficSample;

typedef struct {
	uint64_t rx, tx, rx_packets, tx_packets, rx_rate, tx_rate;
	uint64_t sampled_ms, started_ms, rate_ms, rate_rx, rate_tx;
	int64_t sampled_at, started_at;
	VpnTrafficSample previous;
	unsigned session, resets, wraps;
	int has_sample, running, completed, have_previous, counter_bits, source,
		sample_failed, rate_ready;
	char profile[33], interface[16], reason[24];
} VpnTraffic;

int vpn_traffic_counter(const char *, uint64_t *);
int vpn_traffic_proc(const char *, const char *, VpnTrafficSample *);
int vpn_traffic_read(const char *, int, VpnTrafficSample *);
int vpn_traffic_bits(void);
void vpn_traffic_begin(
	VpnTraffic *, const char *, const char *, uint64_t, int64_t);
void vpn_traffic_attach(VpnTraffic *, const char *, int);
void vpn_traffic_pause(VpnTraffic *, int);
void vpn_traffic_missing(VpnTraffic *);
void vpn_traffic_update(VpnTraffic *,
			const VpnTrafficSample *,
			uint64_t,
			int64_t);
char *vpn_traffic_json(const VpnTraffic *, uint64_t);
#endif
