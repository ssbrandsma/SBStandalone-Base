/* Only protocol 186 / metric 32760 routes and private SBVPN chains are owned.
 */
#include "vpn.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

static int ipv4(const char *s)
{
	struct in_addr a;
	return inet_pton(AF_INET, s, &a) == 1 && a.s_addr != 0 &&
	       (ntohl(a.s_addr) >> 24) != 127 && (ntohl(a.s_addr) >> 24) != 0 &&
	       ntohl(a.s_addr) < 0xe0000000u;
}

int vpn_ipv6(void)
{
	int fd = socket(AF_INET6, SOCK_DGRAM, 0);
	if (fd < 0)
		return 0;
	close(fd);
	return 1;
}

int vpn_network_discover(VpnNetwork *n)
{
	n->local_count = 0;
	n->local_ipv6_count = 0;
	n->address_signature = 0;
	memset(n->local_ipv6, 0, sizeof n->local_ipv6);
	FILE *f = fopen("/proc/net/route", "r");
	if (!f)
		return -1;
	char line[256], iface[16];
	unsigned long dst, gw, mask;
	unsigned flags, metric;
	int found = 0;
	unsigned best_metric = ~0u;
	while (fgets(line, sizeof line, f))
		if (sscanf(line,
			   "%15s %lx %lx %x %*u %*u %u %lx",
			   iface,
			   &dst,
			   &gw,
			   &flags,
			   &metric,
			   &mask) == 6 &&
		    dst == 0 && mask == 0 && (flags & 1) &&
		    strcmp(iface, VPN_INTERFACE) &&
		    (!found || metric < best_metric)) {
			strcpy(n->physical, iface);
			struct in_addr a;
			a.s_addr = (uint32_t)gw;
			inet_ntop(AF_INET, &a, n->gateway, sizeof n->gateway);
			found = 1;
			best_metric = metric;
		}
	fclose(f);
	if (!found)
		return -1;
	struct ifaddrs *list;
	if (getifaddrs(&list))
		return -1;
	found = 0;
	for (struct ifaddrs *p = list; p; p = p->ifa_next)
		if (p->ifa_addr && p->ifa_netmask &&
		    (p->ifa_flags & IFF_BROADCAST) && (p->ifa_flags & IFF_UP) &&
		    (p->ifa_flags & IFF_RUNNING) &&
		    p->ifa_addr->sa_family == AF_INET &&
		    !strcmp(p->ifa_name, n->physical)) {
			uint32_t address = ((struct sockaddr_in *)p->ifa_addr)
						   ->sin_addr.s_addr,
				 netmask =
					 ((struct sockaddr_in *)p->ifa_netmask)
						 ->sin_addr.s_addr;
			struct in_addr a = {address & netmask};
			unsigned bits = 0;
			uint32_t m = ntohl(netmask);
			while (m & 0x80000000u) {
				bits++;
				m <<= 1;
			}
			if (m || !bits)
				continue;
			char ip[16];
			inet_ntop(AF_INET, &a, ip, sizeof ip);
			snprintf(
				n->subnet, sizeof n->subnet, "%s/%u", ip, bits);
			found = 1;
			break;
		}
	for (struct ifaddrs *p = list; p; p = p->ifa_next)
		if (p->ifa_addr && p->ifa_netmask &&
		    p->ifa_addr->sa_family == AF_INET &&
		    (p->ifa_flags & IFF_UP) && (p->ifa_flags & IFF_BROADCAST) &&
		    !(p->ifa_flags & IFF_LOOPBACK) &&
		    strcmp(p->ifa_name, VPN_INTERFACE)) {
			uint64_t address_hash = 1469598103934665603ull;
			const unsigned char *bytes =
				(const unsigned char *)&(
					(struct sockaddr_in *)p->ifa_addr)
					->sin_addr;
			for (unsigned j = 0; j < 4; j++) {
				address_hash ^= bytes[j];
				address_hash *= 1099511628211ull;
			}
			for (const char *j = p->ifa_name; *j; j++) {
				address_hash ^= (unsigned char)*j;
				address_hash *= 1099511628211ull;
			}
			n->address_signature ^= address_hash;

			struct in_addr a = {
				((struct sockaddr_in *)p->ifa_addr)
					->sin_addr.s_addr &
				((struct sockaddr_in *)p->ifa_netmask)
					->sin_addr.s_addr};
			uint32_t m =
				ntohl(((struct sockaddr_in *)p->ifa_netmask)
					      ->sin_addr.s_addr);
			unsigned bits = 0;
			while (m & 0x80000000u) {
				bits++;
				m <<= 1;
			}
			if (m || !bits)
				continue;
			char ip[16], cidr[32];
			inet_ntop(AF_INET, &a, ip, sizeof ip);
			snprintf(cidr, sizeof cidr, "%s/%u", ip, bits);
			int duplicate = 0;
			for (int i = 0; i < n->local_count; i++)
				if (!strcmp(cidr, n->local_subnets[i]))
					duplicate = 1;
			if (duplicate)
				continue;
			if (n->local_count == 8) {
				freeifaddrs(list);
				return -1;
			}
			snprintf(n->local_subnets[n->local_count],
				 32,
				 "%s",
				 cidr);
			snprintf(n->local_interfaces[n->local_count++],
				 16,
				 "%s",
				 p->ifa_name);
		}
	for (struct ifaddrs *p = list; p; p = p->ifa_next)
		if (p->ifa_addr && p->ifa_netmask &&
		    p->ifa_addr->sa_family == AF_INET6 &&
		    (p->ifa_flags & IFF_UP) && (p->ifa_flags & IFF_BROADCAST) &&
		    !(p->ifa_flags & IFF_LOOPBACK) &&
		    strcmp(p->ifa_name, VPN_INTERFACE)) {
			struct in6_addr
				address = ((struct sockaddr_in6 *)p->ifa_addr)
						  ->sin6_addr,
				mask = ((struct sockaddr_in6 *)p->ifa_netmask)
					       ->sin6_addr;
			unsigned bits = 0;
			for (unsigned i = 0; i < 16; i++) {
				address.s6_addr[i] &= mask.s6_addr[i];
				for (unsigned b = 0; b < 8; b++)
					if (mask.s6_addr[i] & (128u >> b))
						bits++;
			}
			if (!bits)
				continue;
			char ip[80], prefix[96];
			inet_ntop(AF_INET6, &address, ip, sizeof ip);
			snprintf(prefix, sizeof prefix, "%s/%u", ip, bits);
			int duplicate = 0;
			for (int i = 0; i < n->local_ipv6_count; i++)
				if (!strcmp(prefix, n->local_ipv6[i]))
					duplicate = 1;
			if (duplicate)
				continue;
			if (n->local_ipv6_count == 8) {
				freeifaddrs(list);
				return -1;
			}
			strcpy(n->local_ipv6[n->local_ipv6_count++], prefix);
		}
	freeifaddrs(list);
	return found ? 0 : -1;
}

static int
attr(struct nlmsghdr *h, size_t cap, int type, const void *data, size_t size)
{
	size_t at = NLMSG_ALIGN(h->nlmsg_len), len = RTA_LENGTH(size);
	if (at + RTA_ALIGN(len) > cap)
		return -1;
	struct rtattr *a = (void *)((char *)h + at);
	a->rta_type = type;
	a->rta_len = (unsigned short)len;
	memcpy(RTA_DATA(a), data, size);
	h->nlmsg_len = (unsigned)(at + RTA_ALIGN(len));
	return 0;
}

static int route(int add,
		 const char *destination,
		 unsigned prefix,
		 const char *gateway,
		 const char *iface)
{
	struct {
		struct nlmsghdr h;
		struct rtmsg r;
		char data[128];
	} msg;

	memset(&msg, 0, sizeof msg);
	msg.h.nlmsg_len = NLMSG_LENGTH(sizeof msg.r);
	msg.h.nlmsg_type = add ? RTM_NEWROUTE : RTM_DELROUTE;
	msg.h.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK |
			    (add ? NLM_F_EXCL | NLM_F_CREATE : 0);
	msg.h.nlmsg_seq = 1;
	msg.r.rtm_family = AF_INET;
	msg.r.rtm_dst_len = (unsigned char)prefix;
	msg.r.rtm_table = RT_TABLE_MAIN;
	msg.r.rtm_protocol = 186;
	msg.r.rtm_scope =
		!strcmp(gateway, "0.0.0.0") ? RT_SCOPE_LINK : RT_SCOPE_UNIVERSE;
	msg.r.rtm_type = RTN_UNICAST;
	struct in_addr dest, gw;
	if (inet_pton(AF_INET, destination, &dest) != 1 ||
	    inet_pton(AF_INET, gateway, &gw) != 1)
		return -1;
	unsigned index = if_nametoindex(iface), priority = 32760;
	if (!index)
		return add ? -1 : 0;
	attr(&msg.h, sizeof msg, RTA_DST, &dest, sizeof dest);
	attr(&msg.h, sizeof msg, RTA_OIF, &index, sizeof index);
	attr(&msg.h, sizeof msg, RTA_PRIORITY, &priority, sizeof priority);
	if (gw.s_addr)
		attr(&msg.h, sizeof msg, RTA_GATEWAY, &gw, sizeof gw);
	int fd = vpn_socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE, 0);
	if (fd < 0)
		return -1;
	struct sockaddr_nl sa;
	memset(&sa, 0, sizeof sa);
	sa.nl_family = AF_NETLINK;
	int ok = -1;
	if (sendto(fd,
		   &msg,
		   msg.h.nlmsg_len,
		   0,
		   (struct sockaddr *)&sa,
		   sizeof sa) == (ssize_t)msg.h.nlmsg_len) {
		struct pollfd p = {fd, POLLIN, 0};
		if (poll(&p, 1, 1000) > 0) {
			char response[4096];
			ssize_t len = recv(fd, response, sizeof response, 0);
			for (struct nlmsghdr *h = (void *)response;
			     len > 0 && NLMSG_OK(h, (unsigned)len);
			     h = NLMSG_NEXT(h, len))
				if (h->nlmsg_type == NLMSG_ERROR) {
					int error =
						((struct nlmsgerr *)NLMSG_DATA(
							 h))
							->error;
					if (!error ||
					    (!add && (error == -ESRCH ||
						      error == -ENOENT ||
						      error == -ENODEV)))
						ok = 0;
					break;
				}
		}
	}
	close(fd);
	return ok;
}

static int fw_output(int six, char *const args[], char *output, size_t cap)
{
	const char *path = vpn_program(six ? "ip6tables" : "iptables");
	if (!path)
		return -1;
	char executable[512];
	snprintf(executable, sizeof executable, "%s", path);
	char *v[20];
	v[0] = executable;
	int i = 0;
	while (args[i] && i < 17) {
		v[i + 1] = args[i];
		i++;
	}
	v[i + 1] = NULL;
	return vpn_exec(executable, v, output, cap, 2000);
}

int vpn_firewall_capability(int six)
{
	char output[32768];
	return !fw_output(six,
			  (char *[]){"-S", "OUTPUT", NULL},
			  output,
			  sizeof output) &&
	       strlen(output) < sizeof output - 1;
}

static int fw(int six, char *const args[])
{
	return fw_output(six, args, NULL, 0);
}

static int remove_chain(int six, const char *chain)
{
	for (int i = 0; i < 4; i++)
		if (fw(six,
		       (char *[]){"-D", "OUTPUT", "-j", (char *)chain, NULL}))
			break;
	char rules[32768], jump[48];
	snprintf(jump, sizeof jump, "-j %s", chain);
	if (fw_output(six, (char *[]){"-S", NULL}, rules, sizeof rules) ||
	    strlen(rules) == sizeof rules - 1 || strstr(rules, jump))
		return -1;
	/* Never flush a chain while any jump can still reach it. */
	fw(six, (char *[]){"-F", (char *)chain, NULL});
	fw(six, (char *[]){"-X", (char *)chain, NULL});
	return 0;
}

static int guard_one(VpnNetwork *n, int six, int enable, int protect_dns)
{
	int old = six ? n->guard6 : n->guard4;
	char previous[16], next[16];
	snprintf(previous,
		 sizeof previous,
		 "SBVPN%d%c",
		 six ? 6 : 4,
		 old == 2 ? 'B' : 'A');
	snprintf(next,
		 sizeof next,
		 "SBVPN%d%c",
		 six ? 6 : 4,
		 old == 1 ? 'B' : 'A');
	if (!enable) {
		char a[16], b[16], output[32768];
		snprintf(a, sizeof a, "SBVPN%dA", six ? 6 : 4);
		snprintf(b, sizeof b, "SBVPN%dB", six ? 6 : 4);
		remove_chain(six, a);
		remove_chain(six, b);
		if (fw_output(six,
			      (char *[]){"-S", "OUTPUT", NULL},
			      output,
			      sizeof output) ||
		    strlen(output) == sizeof output - 1 || strstr(output, a) ||
		    strstr(output, b))
			return -1;
		if (six)
			n->guard6 = 0;
		else
			n->guard4 = 0;
		return 0;
	}
	if (fw(six, (char *[]){"-N", next, NULL}))
		return -1;
#define RULE(...)                                                              \
	do {                                                                   \
		if (fw(six, (char *[]){"-A", next, __VA_ARGS__, NULL}))        \
			goto bad;                                              \
	} while (0)
	RULE("-o", "lo", "-j", "RETURN");
	if (six) {
		if (protect_dns) {
			RULE("-p", "udp", "--dport", "53", "-j", "REJECT");
			RULE("-p", "tcp", "--dport", "53", "-j", "REJECT");
		}
		RULE("-d", "fe80::/10", "-j", "RETURN");
		RULE("-d", "ff02::/16", "-j", "RETURN");
		for (int i = 0; i < n->local_ipv6_count; i++)
			RULE("-d", n->local_ipv6[i], "-j", "RETURN");
	} else {
		RULE("-o", VPN_INTERFACE, "-j", "RETURN");
		char port[8];
		snprintf(port, sizeof port, "%u", n->port);
		RULE("-d",
		     n->endpoint,
		     "-p",
		     n->protocol[0] == 'u' ? "udp" : "tcp",
		     "--dport",
		     port,
		     "-j",
		     "RETURN");
		RULE("-p", "udp", "--dport", "53", "-j", "REJECT");
		RULE("-p", "tcp", "--dport", "53", "-j", "REJECT");
		for (int i = 0; i < n->local_count; i++)
			RULE("-d", n->local_subnets[i], "-j", "RETURN");
		RULE("-d", "224.0.0.0/4", "-j", "RETURN");
		RULE("-d", "169.254.0.0/16", "-j", "RETURN");
		RULE("-p",
		     "udp",
		     "--sport",
		     "68",
		     "--dport",
		     "67",
		     "-j",
		     "RETURN");
	}
	RULE("-j", "REJECT");
	if (fw(six, (char *[]){"-I", "OUTPUT", "1", "-j", next, NULL}))
		goto bad;
	if (six)
		n->guard6 = old == 1 ? 2 : 1;
	else
		n->guard4 = old == 1 ? 2 : 1;
	if (old > 0 && remove_chain(six, previous))
		return -1;
	return 0;
bad:
	remove_chain(six, next);
	return -1;
#undef RULE
}

int vpn_network_journal(VpnNetwork *n, const char *runtime)
{
	int dir = vpn_store_open(runtime);
	if (dir < 0)
		return -1;
	char *s = mg_mprintf(
		"{\"schema\":1,\"endpoint_interface\":%m,\"endpoint_gateway\":%"
		"m,\"physical\":%m,\"gateway\":%m,\"subnet\":%m,"
		"\"endpoint\":%m,\"protocol\":%m,\"port\":%u,\"tunnel_"
		"gateway\":%m,\"tunnel_address\":%m,\"routes\":%d,\"guard4\":%"
		"d,\"guard6\":%d,\"dns_active\":%d,\"resolver\":%m,\"original_"
		"dns\":%m,\"owned_dns\":%m,\"dns0\":%m,\"dns1\":%m,\"dns2\":%"
		"m}",
		MG_ESC(n->endpoint_interface),
		MG_ESC(n->endpoint_gateway),
		MG_ESC(n->physical),
		MG_ESC(n->gateway),
		MG_ESC(n->subnet),
		MG_ESC(n->endpoint),
		MG_ESC(n->protocol),
		n->port,
		MG_ESC(n->tunnel_gateway),
		MG_ESC(n->tunnel_address),
		n->routes,
		n->guard4,
		n->guard6,
		n->dns_active,
		MG_ESC(n->resolver),
		MG_ESC(n->original_dns),
		MG_ESC(n->owned_dns),
		MG_ESC(n->dns[0]),
		MG_ESC(n->dns[1]),
		MG_ESC(n->dns[2]));
	int r = s ? vpn_store_write(dir, "network.json", s) : -1;
	free(s);
	close(dir);
	return r;
}

static int repair_guard(VpnNetwork *n, int six)
{
	int flag = six ? n->guard6 : n->guard4;
	if (!flag)
		return 0;
	char output[32768], a[16], b[16], jump_a[48], jump_b[48];
	snprintf(a, sizeof a, "SBVPN%dA", six ? 6 : 4);
	snprintf(b, sizeof b, "SBVPN%dB", six ? 6 : 4);
	snprintf(jump_a, sizeof jump_a, "-A OUTPUT -j %s", a);
	snprintf(jump_b, sizeof jump_b, "-A OUTPUT -j %s", b);
	if (fw_output(six,
		      (char *[]){"-S", "OUTPUT", NULL},
		      output,
		      sizeof output) ||
	    strlen(output) == sizeof output - 1)
		return -1;
	char *pa = strstr(output, jump_a), *pb = strstr(output, jump_b);
	int active = pa && (!pb || pa < pb) ? 1 : pb ? 2 : 0;
	if (active != 1)
		remove_chain(six, a);
	if (active != 2)
		remove_chain(six, b);
	if (six)
		n->guard6 = active;
	else
		n->guard4 = active;
	return 0;
}

int vpn_network_recover(VpnNetwork *n, const char *runtime)
{
	int dir = vpn_store_open(runtime);
	if (dir < 0)
		return -1;
	char *s = vpn_store_read(dir, "network.json", 8192);
	struct stat journal;
	int present =
		!fstatat(dir, "network.json", &journal, AT_SYMLINK_NOFOLLOW);
	int absent = !present && errno == ENOENT;
	close(dir);
	if (!s)
		return absent ? 0 : -1;
	if (!vpn_json_valid(s, strlen(s))) {
		free(s);
		return -1;
	}
	struct mg_str j = mg_str(s);
	if (mg_json_get_long(j, "$.schema", 0) != 1) {
		free(s);
		return -1;
	}
#define READ(k, d)                                                             \
	do {                                                                   \
		char *v = mg_json_get_str(j, "$." k);                          \
		if (!v || strlen(v) >= sizeof(n->d)) {                         \
			free(v);                                               \
			free(s);                                               \
			return -1;                                             \
		}                                                              \
		strcpy(n->d, v);                                               \
		free(v);                                                       \
	} while (0)
	READ("endpoint_interface", endpoint_interface);
	READ("endpoint_gateway", endpoint_gateway);
	READ("physical", physical);
	READ("gateway", gateway);
	READ("subnet", subnet);
	READ("endpoint", endpoint);
	READ("protocol", protocol);
	READ("tunnel_gateway", tunnel_gateway);
	READ("tunnel_address", tunnel_address);
	READ("resolver", resolver);
	READ("original_dns", original_dns);
	READ("owned_dns", owned_dns);
	READ("dns0", dns[0]);
	READ("dns1", dns[1]);
	READ("dns2", dns[2]);
#undef READ
	n->port = (unsigned)mg_json_get_long(j, "$.port", 0);
	n->routes = (int)mg_json_get_long(j, "$.routes", 0);
	n->guard4 = (int)mg_json_get_long(j, "$.guard4", 0);
	n->guard6 = (int)mg_json_get_long(j, "$.guard6", 0);
	n->dns_active = (int)mg_json_get_long(j, "$.dns_active", 0);
	free(s);
	return repair_guard(n, 0) || repair_guard(n, 1) ? -1 : 0;
}

static char *resolver_read(const char *path)
{
	int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
	if (fd < 0)
		return NULL;
	struct stat st;
	if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != 0 ||
	    (st.st_mode & 0022) || st.st_size > 2047) {
		close(fd);
		return NULL;
	}
	char *s = calloc(1, 2048);
	if (!s) {
		close(fd);
		return NULL;
	}
	ssize_t r = read(fd, s, 2047);
	close(fd);
	if (r < 0) {
		free(s);
		return NULL;
	}
	s[r] = 0;
	return s;
}

int vpn_dns_capability(void)
{
	char *resolved = realpath("/etc/resolv.conf", NULL);
	if (!resolved)
		return 0;
	if (strlen(resolved) > 511) {
		free(resolved);
		return 0;
	}
	char *contents = resolver_read(resolved);
	char parent[512];
	strcpy(parent, resolved);
	char *slash = strrchr(parent, '/');
	if (!slash) {
		free(contents);
		free(resolved);
		return 0;
	}
	*slash = 0;
	int ok = contents && !vpn_trusted_dir(*parent ? parent : "/") &&
		 !access(*parent ? parent : "/", W_OK);
	free(contents);
	free(resolved);
	return ok;
}

static int resolver_write(const char *path, const char *text)
{
	char temp[600];
	unsigned char random[8];
	mg_random(random, sizeof random);
	snprintf(temp,
		 sizeof temp,
		 "%s.sbvpn-%02x%02x%02x%02x",
		 path,
		 random[0],
		 random[1],
		 random[2],
		 random[3]);
	int fd = open(temp,
		      O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
		      0644);
	if (fd < 0)
		return -1;
	size_t size = strlen(text);
	ssize_t n = write(fd, text, size);
	int failed = n != (ssize_t)size || fsync(fd);
	if (close(fd))
		failed = 1;
	if (!failed && rename(temp, path))
		failed = 1;
	if (failed)
		unlink(temp);
	return failed ? -1 : 0;
}

int vpn_network_dns(VpnNetwork *n,
		    const VpnSettings *settings,
		    const char *runtime)
{
	if (!settings->dns)
		return 0;
	if (!n->dns_count) {
		if (!ipv4(settings->dns_server))
			return -1;
		strcpy(n->dns[0], settings->dns_server);
		n->dns_count = 1;
	}
	if (!*n->resolver) {
		if (!vpn_dns_capability())
			return -1;
		char *resolved = realpath("/etc/resolv.conf", NULL);
		if (!resolved)
			return -1;
		if (strlen(resolved) >= sizeof n->resolver) {
			free(resolved);
			return -1;
		}
		strcpy(n->resolver, resolved);
		free(resolved);
		char *current = resolver_read(n->resolver);
		if (!current)
			return -1;
		strcpy(n->original_dns, current);
		free(current);
	}
	n->owned_dns[0] = 0;
	for (int i = 0; i < n->dns_count; i++) {
		if (!ipv4(n->dns[i]))
			return -1;
		size_t used = strlen(n->owned_dns);
		snprintf(n->owned_dns + used,
			 sizeof n->owned_dns - used,
			 "nameserver %s\n",
			 n->dns[i]);
	}
	n->dns_active = 1;
	if (vpn_network_journal(n, runtime) ||
	    resolver_write(n->resolver, n->owned_dns))
		return -1;
	return 0;
}

int vpn_network_guard(VpnNetwork *n,
		      const VpnSettings *settings,
		      const char *runtime)
{
	int kill = settings->kill; /* Journal intent before touching any owned
				      chains. */
	if (kill && !n->guard4)
		n->guard4 = -1;
	if (vpn_ipv6() && !n->guard6)
		n->guard6 = -1;
	if (vpn_network_journal(n, runtime))
		return -1;
	if (kill && guard_one(n, 0, 1, 1))
		return -1;
	if (vpn_ipv6() && guard_one(n, 1, 1, settings->dns))
		return -1;
	return vpn_network_journal(n, runtime);
}

static int subnet_contains(const char *subnet, const char *ip)
{
	char cidr[32];
	snprintf(cidr, sizeof cidr, "%s", subnet);
	char *slash = strchr(cidr, '/');
	if (!slash)
		return 0;
	*slash++ = 0;
	unsigned bits = (unsigned)atoi(slash);
	struct in_addr address, base;
	if (!bits || bits > 32 || inet_pton(AF_INET, cidr, &base) != 1 ||
	    inet_pton(AF_INET, ip, &address) != 1)
		return 0;
	uint32_t mask = 0xffffffffu << (32 - bits);
	return (ntohl(address.s_addr) & mask) == ntohl(base.s_addr);
}

static int physical_contains(const VpnNetwork *n, const char *ip)
{
	for (int i = 0; i < n->local_count; i++)
		if (subnet_contains(n->local_subnets[i], ip))
			return 1;
	return subnet_contains(n->subnet, ip);
}

void vpn_network_endpoint(VpnNetwork *n)
{
	snprintf(n->endpoint_interface,
		 sizeof n->endpoint_interface,
		 "%s",
		 n->physical);
	snprintf(n->endpoint_gateway,
		 sizeof n->endpoint_gateway,
		 "%s",
		 n->gateway);
	for (int i = 0; i < n->local_count; i++)
		if (subnet_contains(n->local_subnets[i], n->endpoint)) {
			snprintf(n->endpoint_interface,
				 16,
				 "%s",
				 n->local_interfaces[i]);
			strcpy(n->endpoint_gateway, "0.0.0.0");
			break;
		}
}

static const char *endpoint_gateway(const VpnNetwork *n)
{
	return *n->endpoint_gateway
		       ? n->endpoint_gateway
		       : (physical_contains(n, n->endpoint) ? "0.0.0.0"
							    : n->gateway);
}

static const char *endpoint_interface(const VpnNetwork *n)
{
	return *n->endpoint_interface ? n->endpoint_interface : n->physical;
}

static int configure_interface(VpnNetwork *n)
{
	if (!ipv4(n->tunnel_address) || !ipv4(n->tunnel_gateway) ||
	    physical_contains(n, n->tunnel_address) ||
	    physical_contains(n, n->tunnel_gateway))
		return -1;
	struct in_addr mask, address;
	const char *netmask =
		*n->tunnel_netmask ? n->tunnel_netmask : "255.255.255.255";
	if (inet_pton(AF_INET, netmask, &mask) != 1 ||
	    inet_pton(AF_INET, n->tunnel_address, &address) != 1)
		return -1;
	uint32_t m = ntohl(mask.s_addr), inverted = ~m;
	if ((inverted & (inverted + 1)) || !m)
		return -1;
	char cidr[32];
	snprintf(cidr, sizeof cidr, "%s", n->subnet);
	char *slash = strchr(cidr, '/');
	if (!slash)
		return -1;
	*slash++ = 0;
	unsigned bits = (unsigned)atoi(slash);
	struct in_addr lan;
	if (!bits || bits > 32 || inet_pton(AF_INET, cidr, &lan) != 1)
		return -1;
	uint32_t physical_mask = 0xffffffffu << (32 - bits),
		 combined = m & physical_mask;
	if ((ntohl(address.s_addr) & combined) ==
	    (ntohl(lan.s_addr) & combined))
		return -1;
	if (n->tunnel_mtu < 576 || n->tunnel_mtu > 1500)
		return -1;
	int fd = vpn_socket(AF_INET, SOCK_DGRAM, 0, 0);
	if (fd < 0)
		return -1;
	struct ifreq request;
	memset(&request, 0, sizeof request);
	snprintf(request.ifr_name, IFNAMSIZ, "%s", VPN_INTERFACE);
	struct sockaddr_in *a = (void *)&request.ifr_addr;
	a->sin_family = AF_INET;
	a->sin_addr = address;
	int failed = ioctl(fd, SIOCSIFADDR, &request);
	a = (void *)&request.ifr_netmask;
	a->sin_family = AF_INET;
	a->sin_addr = mask;
	if (!failed)
		failed = ioctl(fd, SIOCSIFNETMASK, &request);
	if (!failed && *n->tunnel_peer) {
		a = (void *)&request.ifr_dstaddr;
		a->sin_family = AF_INET;
		if (inet_pton(AF_INET, n->tunnel_peer, &a->sin_addr) != 1)
			failed = -1;
		else
			failed = ioctl(fd, SIOCSIFDSTADDR, &request);
	}
	if (!failed) {
		request.ifr_mtu = n->tunnel_mtu;
		failed = ioctl(fd, SIOCSIFMTU, &request);
	}
	if (!failed)
		failed = ioctl(fd, SIOCGIFFLAGS, &request);
	if (!failed) {
		request.ifr_flags |= IFF_UP;
		failed = ioctl(fd, SIOCSIFFLAGS, &request);
	}
	if (!failed) {
		failed = ioctl(fd, SIOCGIFADDR, &request);
		a = (void *)&request.ifr_addr;
		if (a->sin_addr.s_addr != address.s_addr)
			failed = -1;
	}
	close(fd);
	return failed ? -1 : 0;
}

int vpn_network_apply(VpnNetwork *n, const VpnSettings *s, const char *runtime)
{
	if (!ipv4(n->endpoint) || !ipv4(n->tunnel_gateway) ||
	    !if_nametoindex(VPN_INTERFACE))
		return -1; /* A tunnel address overlapping the physical LAN is
			      unsafe. */
	struct in_addr address, base;
	unsigned bits;
	char subnet[32];
	strcpy(subnet, n->subnet);
	char *slash = strchr(subnet, '/');
	if (!slash)
		return -1;
	*slash++ = 0;
	bits = (unsigned)atoi(slash);
	if (inet_pton(AF_INET, subnet, &base) != 1 ||
	    inet_pton(AF_INET, n->tunnel_address, &address) != 1 || bits > 32 ||
	    !bits)
		return -1;
	uint32_t mask = 0xffffffffu << (32 - bits);
	if ((ntohl(address.s_addr) & mask) == ntohl(base.s_addr))
		return -1;
	if (s->dns && !n->dns_count) {
		if (!ipv4(s->dns_server))
			return -2;
		strcpy(n->dns[0], s->dns_server);
		n->dns_count = 1;
	}
	for (int i = 0; i < n->dns_count; i++)
		if (!ipv4(n->dns[i]) || physical_contains(n, n->dns[i]) ||
		    !strcmp(n->dns[i], n->endpoint))
			return -2;
	if (configure_interface(n))
		return -1;
	n->routes = 1;
	if (vpn_network_journal(n, runtime))
		return -1;
	if (route(1,
		  n->endpoint,
		  32,
		  endpoint_gateway(n),
		  endpoint_interface(n)) ||
	    route(1, "224.0.0.0", 4, "0.0.0.0", n->physical) ||
	    route(1, "169.254.0.0", 16, "0.0.0.0", n->physical) ||
	    route(1, "0.0.0.0", 1, n->tunnel_gateway, VPN_INTERFACE) ||
	    route(1, "128.0.0.0", 1, n->tunnel_gateway, VPN_INTERFACE))
		return -1;
	for (int i = 0; i < n->dns_count; i++)
		if (route(1, n->dns[i], 32, n->tunnel_gateway, VPN_INTERFACE))
			return -1;
	if (vpn_network_dns(n, s, runtime))
		return -2;
	return 0;
}

int vpn_network_cleanup(VpnNetwork *n, int guards, const char *runtime)
{
	int result = 0;
	if (n->routes) {
		if (*n->endpoint)
			result |= route(0,
					n->endpoint,
					32,
					endpoint_gateway(n),
					endpoint_interface(n));
		result |= route(0, "224.0.0.0", 4, "0.0.0.0", n->physical);
		result |= route(0, "169.254.0.0", 16, "0.0.0.0", n->physical);
		if (*n->tunnel_gateway) {
			result |= route(0,
					"0.0.0.0",
					1,
					n->tunnel_gateway,
					VPN_INTERFACE);
			result |= route(0,
					"128.0.0.0",
					1,
					n->tunnel_gateway,
					VPN_INTERFACE);
			for (int i = 0; i < 3; i++)
				if (*n->dns[i])
					result |= route(0,
							n->dns[i],
							32,
							n->tunnel_gateway,
							VPN_INTERFACE);
		}
	}
	if (n->dns_active && *n->resolver) {
		char *current = resolver_read(n->resolver);
		if (!current)
			result = -1;
		else {
			if (!strcmp(current, n->owned_dns))
				result |= resolver_write(n->resolver,
							 n->original_dns);
			free(current);
		}
	}
	if (guards) {
		if (n->guard4)
			result |= guard_one(n, 0, 0, 0);
		if (n->guard6)
			result |= guard_one(n, 1, 0, 0);
	}
	if (!result) {
		n->routes = n->dns_active = 0;
		n->resolver[0] = n->original_dns[0] = n->owned_dns[0] = 0;
	}
	if (vpn_network_journal(n, runtime))
		result = -1;
	return result;
}

/* Track DHCP resolver replacement without restoring a stale pre-VPN snapshot.
 */
int vpn_network_refresh_dns(VpnNetwork *n, const char *runtime)
{
	if (!n->dns_active)
		return 0;
	char *current = resolver_read(n->resolver);
	if (!current)
		return -1;
	if (!strcmp(current, n->owned_dns)) {
		free(current);
		return 0;
	}
	snprintf(n->original_dns, sizeof n->original_dns, "%s", current);
	free(current);
	if (vpn_network_journal(n, runtime))
		return -1;
	return resolver_write(n->resolver, n->owned_dns);
}

/* Decline pre-existing public routes which would bypass def1, without deleting
 * them. */
static unsigned owned_route_bit(const VpnNetwork *n,
				struct in_addr destination,
				unsigned prefix,
				unsigned index,
				struct in_addr gateway)
{
	struct in_addr expected;
	unsigned tunnel = if_nametoindex(VPN_INTERFACE),
		 physical = if_nametoindex(n->physical);
	uint32_t address = ntohl(destination.s_addr);
	inet_pton(AF_INET, n->tunnel_gateway, &expected);
	if (prefix == 1 && index == tunnel &&
	    gateway.s_addr == expected.s_addr) {
		if (address == 0)
			return 8;
		if (address == 0x80000000u)
			return 16;
	}
	if (prefix == 4 && address == 0xe0000000u && index == physical &&
	    !gateway.s_addr)
		return 2;
	if (prefix == 16 && address == 0xa9fe0000u && index == physical &&
	    !gateway.s_addr)
		return 4;
	if (prefix == 32 && index == tunnel &&
	    gateway.s_addr == expected.s_addr) {
		for (int i = 0; i < n->dns_count; i++) {
			struct in_addr dns;
			inet_pton(AF_INET, n->dns[i], &dns);
			if (destination.s_addr == dns.s_addr)
				return 32u << i;
		}
	}
	if (prefix == 32 && index == if_nametoindex(endpoint_interface(n))) {
		struct in_addr endpoint, via;
		inet_pton(AF_INET, n->endpoint, &endpoint);
		inet_pton(AF_INET, endpoint_gateway(n), &via);
		if (destination.s_addr == endpoint.s_addr &&
		    gateway.s_addr == via.s_addr)
			return 1;
	}
	return 0;
}

static int tunnel_present(const VpnNetwork *n)
{
	int fd = vpn_socket(AF_INET, SOCK_DGRAM, 0, 0);
	if (fd < 0)
		return 0;
	struct ifreq request;
	memset(&request, 0, sizeof request);
	strcpy(request.ifr_name, VPN_INTERFACE);
	int ok = !ioctl(fd, SIOCGIFFLAGS, &request) &&
		 (request.ifr_flags & IFF_UP);
	if (ok) {
		ok = !ioctl(fd, SIOCGIFADDR, &request);
		struct in_addr expected;
		ok = ok &&
		     inet_pton(AF_INET, n->tunnel_address, &expected) == 1 &&
		     ((struct sockaddr_in *)&request.ifr_addr)
				     ->sin_addr.s_addr == expected.s_addr;
	}
	close(fd);
	return ok;
}

int vpn_network_conflicts(const VpnNetwork *n)
{
	int fd = vpn_socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE, 0);
	if (fd < 0)
		return -1;

	struct {
		struct nlmsghdr h;
		struct rtmsg r;
	} request;

	memset(&request, 0, sizeof request);
	request.h.nlmsg_len = NLMSG_LENGTH(sizeof request.r);
	request.h.nlmsg_type = RTM_GETROUTE;
	request.h.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
	request.h.nlmsg_seq = 1;
	request.r.rtm_family = AF_INET;
	struct sockaddr_nl kernel;
	memset(&kernel, 0, sizeof kernel);
	kernel.nl_family = AF_NETLINK;
	if (sendto(fd,
		   &request,
		   request.h.nlmsg_len,
		   0,
		   (struct sockaddr *)&kernel,
		   sizeof kernel) != (ssize_t)request.h.nlmsg_len) {
		close(fd);
		return -1;
	}
	uint64_t until = mg_millis() + 1500;
	int result = -1;
	unsigned seen = 0, expected = 31;
	for (int i = 0; i < n->dns_count; i++)
		expected |= 32u << i;
	if (n->routes && !tunnel_present(n)) {
		close(fd);
		return -1;
	}
	unsigned tunnel = if_nametoindex(VPN_INTERFACE);
	while (mg_millis() < until) {
		struct pollfd p = {fd, POLLIN, 0};
		if (poll(&p, 1, 100) <= 0)
			continue;
		char buffer[16384];
		ssize_t length = recv(fd, buffer, sizeof buffer, MSG_TRUNC);
		if (length <= 0 || length > (ssize_t)sizeof buffer)
			break;
		for (struct nlmsghdr *h = (void *)buffer;
		     NLMSG_OK(h, (unsigned)length);
		     h = NLMSG_NEXT(h, length)) {
			if (h->nlmsg_type == NLMSG_DONE) {
				result =
					!n->routes || seen == expected ? 0 : -1;
				goto done;
			}
			if (h->nlmsg_type == NLMSG_ERROR)
				goto done;
			if (h->nlmsg_type != RTM_NEWROUTE ||
			    h->nlmsg_len < NLMSG_LENGTH(sizeof(struct rtmsg)))
				continue;
			struct rtmsg *r = NLMSG_DATA(h);
			if (r->rtm_family != AF_INET ||
			    r->rtm_table != RT_TABLE_MAIN ||
			    r->rtm_type != RTN_UNICAST)
				continue;
			struct in_addr destination = {0}, gateway = {0};
			unsigned index = 0, metric = 0;
			int left = RTM_PAYLOAD(h);
			for (struct rtattr *a = RTM_RTA(r); RTA_OK(a, left);
			     a = RTA_NEXT(a, left)) {
				if (a->rta_type == RTA_DST &&
				    RTA_PAYLOAD(a) >= 4)
					memcpy(&destination, RTA_DATA(a), 4);
				if (a->rta_type == RTA_OIF &&
				    RTA_PAYLOAD(a) >= 4)
					memcpy(&index, RTA_DATA(a), 4);
				if (a->rta_type == RTA_GATEWAY &&
				    RTA_PAYLOAD(a) >= 4)
					memcpy(&gateway, RTA_DATA(a), 4);
				if (a->rta_type == RTA_PRIORITY &&
				    RTA_PAYLOAD(a) >= 4)
					memcpy(&metric, RTA_DATA(a), 4);
			}
			if (r->rtm_protocol == 186 && metric == 32760) {
				if (!n->routes)
					goto done;
				unsigned bit = owned_route_bit(n,
							       destination,
							       r->rtm_dst_len,
							       index,
							       gateway);
				if (!bit)
					goto done;
				seen |= bit;
				continue;
			}
			if (!r->rtm_dst_len || index == tunnel)
				continue;
			uint32_t address = ntohl(destination.s_addr);
			if ((address >> 24) == 127 ||
			    ((address & 0xffff0000u) == 0xa9fe0000u &&
			     r->rtm_dst_len >= 16) ||
			    ((address & 0xf0000000u) == 0xe0000000u &&
			     r->rtm_dst_len >= 4))
				continue;
			char ip[16];
			inet_ntop(AF_INET, &destination, ip, sizeof ip);
			int local = 0;
			for (int i = 0; i < n->local_count; i++) {
				const char *slash =
					strchr(n->local_subnets[i], '/');
				if (slash &&
				    r->rtm_dst_len >=
					    (unsigned)atoi(slash + 1) &&
				    subnet_contains(n->local_subnets[i], ip))
					local = 1;
			}
			if (!local)
				goto done;
		}
	}
done:
	close(fd);
	return result;
}

/* A def1 tunnel is only sufficient with the conventional IPv4 policy rules. */
int vpn_network_policy_standard(void)
{
	struct rule_header {
		unsigned char family, dst_len, src_len, tos, table, res1, res2,
			action;
		uint32_t flags;
	};

	struct {
		struct nlmsghdr h;
		struct rule_header r;
	} request;

	memset(&request, 0, sizeof request);
	request.h.nlmsg_len = NLMSG_LENGTH(sizeof request.r);
	request.h.nlmsg_type = RTM_GETRULE;
	request.h.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
	request.h.nlmsg_seq = 1;
	request.r.family = AF_INET;
	struct sockaddr_nl kernel;
	memset(&kernel, 0, sizeof kernel);
	kernel.nl_family = AF_NETLINK;
	int fd = vpn_socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE, 0);
	if (fd < 0)
		return -1;
	if (sendto(fd,
		   &request,
		   request.h.nlmsg_len,
		   0,
		   (struct sockaddr *)&kernel,
		   sizeof kernel) != (ssize_t)request.h.nlmsg_len) {
		close(fd);
		return -1;
	}
	uint64_t until = mg_millis() + 1500;
	int result = -1;
	while (mg_millis() < until) {
		struct pollfd p = {fd, POLLIN, 0};
		if (poll(&p, 1, 100) <= 0)
			continue;
		char buffer[16384];
		ssize_t length = recv(fd, buffer, sizeof buffer, MSG_TRUNC);
		if (length <= 0 || length > (ssize_t)sizeof buffer)
			break;
		for (struct nlmsghdr *h = (void *)buffer;
		     NLMSG_OK(h, (unsigned)length);
		     h = NLMSG_NEXT(h, length)) {
			if (h->nlmsg_type == NLMSG_DONE) {
				result = 0;
				goto done;
			}
			if (h->nlmsg_type == NLMSG_ERROR) {
				if (h->nlmsg_len >= NLMSG_LENGTH(sizeof(
							    struct nlmsgerr)) &&
				    ((struct nlmsgerr *)NLMSG_DATA(h))->error ==
					    -EOPNOTSUPP)
					result = 0;
				goto done;
			}
			if (h->nlmsg_type != RTM_NEWRULE ||
			    h->nlmsg_len <
				    NLMSG_LENGTH(sizeof(struct rule_header)))
				continue;
			struct rule_header *r = NLMSG_DATA(h);
			if (r->family != AF_INET)
				continue;
			if (r->dst_len || r->src_len || r->tos ||
			    r->action != 1 || (r->flags & ~1u))
				goto done;
			unsigned table = r->table, priority = 0;
			int left = (int)h->nlmsg_len -
				   (int)NLMSG_LENGTH(sizeof *r);
			for (struct rtattr *a =
				     (void *)((char *)r +
					      NLMSG_ALIGN(sizeof *r));
			     RTA_OK(a, left);
			     a = RTA_NEXT(a, left)) {
				if (a->rta_type == 6 && RTA_PAYLOAD(a) >= 4)
					memcpy(&priority, RTA_DATA(a), 4);
				else if (a->rta_type == 15 &&
					 RTA_PAYLOAD(a) >= 4)
					memcpy(&table, RTA_DATA(a), 4);
				else if (a->rta_type == 14 &&
					 RTA_PAYLOAD(a) >= 4) {
					unsigned value;
					memcpy(&value, RTA_DATA(a), 4);
					if (value != 0xffffffffu)
						goto done;
				} else if (a->rta_type != 18 &&
					   a->rta_type != 21)
					goto done;
			}
			if (!((table == RT_TABLE_LOCAL && priority == 0) ||
			      (table == RT_TABLE_MAIN && priority == 32766) ||
			      (table == RT_TABLE_DEFAULT && priority == 32767)))
				goto done;
		}
	}
done:
	close(fd);
	return result;
}
