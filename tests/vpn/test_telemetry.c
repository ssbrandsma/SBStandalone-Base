#include "traffic.h"
#include "verification.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static VpnTrafficSample sample(uint64_t rx, uint64_t tx, unsigned index)
{
	VpnTrafficSample s = {rx, tx, 10, 20, index, 1};
	return s;
}

int main(void)
{
	uint64_t n;
	assert(!vpn_traffic_counter("18446744073709551615\n", &n) &&
	       n == UINT64_MAX);
	assert(vpn_traffic_counter("18446744073709551616", &n));
	assert(vpn_traffic_counter("-1", &n));
	assert(vpn_traffic_counter("12x", &n));
	assert(vpn_traffic_counter("", &n));
	VpnTrafficSample raw;
	const char *proc =
		" eth1: 999 1 0 0 0 0 0 0 999 1 0 0 0 0 0 0\n sbvpn01: 888 1 0 "
		"0 0 0 0 0 888 1 0 0 0 0 0 0\n sbvpn0: 4294967301 18 0 0 0 0 0 "
		"0 200 3 0 0 0 0 0 0\n";
	assert(!vpn_traffic_proc(proc, "sbvpn0", &raw) &&
	       raw.rx == 4294967301ULL && raw.tx == 200 &&
	       raw.rx_packets == 18 && raw.source == 2);
	assert(vpn_traffic_proc(proc, "vpn0", &raw));
	assert(vpn_traffic_proc(proc, "../eth1", &raw));
	assert(vpn_traffic_proc("sbvpn0: -1 2\n", "sbvpn0", &raw));
	assert(vpn_traffic_read("../eth1",0,&raw));
 assert(!vpn_traffic_read("lo",0,&raw)&&raw.index&&raw.source);
	VpnTraffic t = {0};
	vpn_traffic_begin(&t, "a", "explicit_connect", 0, 100);
	vpn_traffic_attach(&t, "sbvpn0", 64);
	raw = sample(10, 20, 5);
	vpn_traffic_update(&t, &raw, 0, 100);
	assert(t.rx == 10 && t.tx == 20 && t.rx_rate == 0);
	raw = sample(7510, 2520, 5);
	vpn_traffic_update(&t, &raw, 2500, 102);
	assert(t.rx_rate == 3000 && t.tx_rate == 1000);
	vpn_traffic_pause(&t, 0);
	assert(t.rx_rate == 0 && t.rx == 7510);
	vpn_traffic_attach(&t, "sbvpn0", 64);
	raw = sample(25, 50, 6);
	vpn_traffic_update(&t, &raw, 8000, 108);
	assert(t.rx == 7535 && t.tx == 2570 && t.rx_rate == 0);
	vpn_traffic_missing(&t);
	char *json = vpn_traffic_json(&t, 8000);
	assert(strstr(json, "\"available\":false"));
	free(json);
	raw = sample(2025, 1050, 6);
	vpn_traffic_update(&t, &raw, 10000, 110);
	assert(t.rx_rate == 0);
	raw = sample(4025, 2050, 6);
	vpn_traffic_update(&t, &raw, 12000, 112);
	assert(t.rx_rate == 1000 && t.tx_rate == 500);
	vpn_traffic_pause(&t, 1);
	uint64_t frozen = t.rx;
	raw = sample(99999, 99999, 6);
	vpn_traffic_update(&t, &raw, 14000, 114);
	assert(t.completed && t.rx == frozen && t.rx_rate == 0);
	vpn_traffic_begin(&t, "b", "profile_switch", 15000, 115);
	assert(!t.has_sample && t.rx == 0 && !strcmp(t.profile, "b"));
	vpn_traffic_attach(&t, "sbvpn0", 32);
	raw = sample(UINT32_MAX - 4, UINT32_MAX - 2, 7);
	vpn_traffic_update(&t, &raw, 15000, 115);
	raw = sample(3, 1, 7);
	vpn_traffic_update(&t, &raw, 17000, 117);
	assert(t.rx == 4294967299ULL && t.tx == 4294967297ULL && t.wraps == 1 &&
	       t.rx_rate == 4 && t.tx_rate == 2);
	vpn_traffic_begin(&t, "b", "explicit_connect", 18000, 118);
	vpn_traffic_attach(&t, "sbvpn0", 32);
	raw = sample(1000, 2000, 8);
	vpn_traffic_update(&t, &raw, 18000, 118);
	raw = sample(10, 20, 8);
	vpn_traffic_update(&t, &raw, 20000, 120);
	assert(t.resets == 1 && t.rx == 1010 && t.tx == 2020 && t.rx_rate == 0);
	raw = sample(8, 9, 9);
	vpn_traffic_update(&t, &raw, 22000, 122);
	assert(t.resets == 2 && t.rx == 1018 && t.tx == 2029 && t.rx_rate == 0);
	VpnTraffic restarted = {0};
	vpn_traffic_begin(&restarted, "b", "manager_restart", 24000, 124);
	assert(!restarted.rx && !restarted.has_sample);
	json = vpn_traffic_json(&restarted, 24000);
	assert(strstr(json, "manager_restart"));
	free(json);
	vpn_traffic_attach(&restarted, "sbvpn0", 64);
	raw = sample(1ULL << 40, 1ULL << 39, 10);
	vpn_traffic_update(&restarted, &raw, 24000, 124);
	assert(restarted.rx == (1ULL << 40));
	raw.rx += 4096;
	vpn_traffic_update(&restarted, &raw, 26000, 126);
	assert(restarted.rx_rate == 2048);
	char ip[16];
	assert(!vpn_verify_ip(" 8.8.4.4\r\n", strlen(" 8.8.4.4\r\n"), ip) &&
	       !strcmp(ip, "8.8.4.4"));
	assert(vpn_verify_ip("127.0.0.1", 9, ip));
	assert(vpn_verify_ip("2001:4860::1", 12, ip));
	assert(vpn_verify_ip("<html>error</html>", 18, ip));
	VpnLocation location;
	const char *geo =
		"{\"success\":true,\"ip\":\"8.8.4.4\",\"country_code\":\"US\","
		"\"country\":\"United States\",\"region\":\"New "
		"York\",\"city\":\"New York\"}";
	assert(!vpn_verify_location(geo, strlen(geo), "8.8.4.4", &location) &&
	       !strcmp(location.city, "New York"));
	assert(vpn_verify_location(geo, strlen(geo), "1.1.1.1", &location));
	const char *missing =
		"{\"success\":true,\"ip\":\"8.8.4.4\",\"country_code\":\"US\"}";
	assert(!vpn_verify_location(
		       missing, strlen(missing), "8.8.4.4", &location) &&
	       !location.city[0]);
	const char *nullable = "{\"success\":true,\"ip\":\"8.8.4.4\","
		"\"country_code\":\"US\",\"region\":null,\"city\":null}";
	assert(!vpn_verify_location(nullable, strlen(nullable), "8.8.4.4", &location) &&
	       !location.city[0] && !location.region[0]);
	const char *bad = "{\"success\":true,\"ip\":\"8.8.4.4\",\"country_"
			  "code\":\"US\",\"city\":123}";
	assert(vpn_verify_location(bad, strlen(bad), "8.8.4.4", &location));
	puts("Traffic/verification: strict parsing, interface isolation, "
	     "64-bit totals, wrap/reset/recreation, measured rates, "
	     "reconnect/switch/disconnect/restart semantics and bound location "
	     "passed");
	return 0;
}
