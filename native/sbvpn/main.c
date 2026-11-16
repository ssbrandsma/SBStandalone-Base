/* sbvpn: one owner of profiles, process identity, network changes and state. */
#include "vpn.h"
#include "traffic.h"
#include "verification.h"
#include "../common/process_identity.h"
#include "../common/process_spawn.h"
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/if_tun.h>
#include <linux/capability.h>
#include <sys/syscall.h>
#include <net/if.h>
#include <netdb.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
static volatile sig_atomic_t quitting;

static struct {
	int store, run, available, desired, listener, lock, management, output,
		resolver_fd, resolving, hook_ready, management_ready,
		auth_failed, restart_requested, settings_corrupt,
		recovery_invalid;
	pid_t child, resolver;
	uint64_t child_started, deadline, retry_at, stop_at, network_check,
		verify_deadline, verify_last_attempt;
	unsigned verify_generation;
	int verify_country;
	char verified_country[3], country_ip[48];
	VpnLocation location;
	char verify_state[20], verify_error[128], location_state[20],
		location_error[128];
	uint64_t public_success_ms, location_success_ms, location_due_ms,
		location_global_attempt, traffic_next;
	time_t location_checked;
	int verify_done, verify_starting, location_pending, cache_current;
	unsigned cache_cursor;

	struct {
		char ip[16];
		VpnLocation location;
		uint64_t attempt_ms, success_ms;
		time_t checked_at;
		int valid;
		char error[128];
	} location_cache[8];

	VpnTraffic traffic;
	unsigned retries, generation;
	time_t connected_at, attempt;
	char root[512], runtime[512], socket[108], executable[512], self[512],
		version[160], firmware[160], device[16];
	char state[24], error[192], category[48], active[33], endpoint[16],
		management_buffer[8192];
	size_t management_used;
	char logs[16][128];
	unsigned log_count;
	VpnSettings settings;
	VpnNetwork network;
	VpnProfile profile;
	struct mg_mgr mgr;
	struct mg_connection *verify;
	char public_ip[48];
	time_t public_checked;
} g;

static void verify_invalidate(void);
static void traffic_sample_now(void);
static void verify_location_schedule(void);

static void signal_stop(int sig)
{
	(void)sig;
	quitting = 1;
}

static void state(const char *s)
{
	if (strcmp(g.state, s)) {
		snprintf(g.logs[g.log_count++ % 16],
			 128,
			 "%ld %s",
			 (long)time(NULL),
			 s);
		snprintf(g.state, sizeof g.state, "%s", s);
	}
}

static void error(const char *code, const char *message)
{
	snprintf(g.logs[g.log_count++ % 16],
		 128,
		 "%ld %.48s",
		 (long)time(NULL),
		 code);
	snprintf(g.category, sizeof g.category, "%s", code);
	snprintf(g.error, sizeof g.error, "%s", message);
	verify_invalidate();
	state("ERROR");
}

static char *response(int status, const char *data)
{
	return mg_mprintf(
		"{\"status\":%d,\"data\":%s}", status, data ? data : "{}");
}

static char *failure(int status, const char *code, const char *message)
{
	char *data = mg_mprintf(
		"{\"error\":%m,\"code\":%m}", MG_ESC(message), MG_ESC(code));
	char *r = response(status, data);
	free(data);
	return r;
}

static int get(struct mg_str j, const char *name, char *out, size_t cap)
{
	char path[80];
	snprintf(path, sizeof path, "$.%s", name);
	char *s = mg_json_get_str(j, path);
	if (!s || strlen(s) >= cap) {
		free(s);
		return -1;
	}
	strcpy(out, s);
	vpn_wipe(s, strlen(s));
	free(s);
	return 0;
}

static int exists(const char *name)
{
	struct stat st;
	return !fstatat(g.store, name, &st, AT_SYMLINK_NOFOLLOW);
}

static char *load_profile(const char *id)
{
	char name[48];
	if (!vpn_id(id))
		return NULL;
	snprintf(name, sizeof name, "%.32s.json", id);
	char *s = vpn_store_read(g.store, name, VPN_LIMIT);
	if (s && (!vpn_json_valid(s, strlen(s)) ||
		  mg_json_get_long(mg_str(s), "$.schema", 0) != 1)) {
		free(s);
		return NULL;
	}
	return s;
}

static int save_settings(void)
{
	char *s = mg_mprintf(
		"{\"schema\":1,\"startup\":%s,\"reconnect\":%s,\"kill\":%s,"
		"\"dns\":%s,\"country_verify\":%s,\"selected\":%m,\"dns_"
		"server\":%m}",
		g.settings.startup ? "true" : "false",
		g.settings.reconnect ? "true" : "false",
		g.settings.kill ? "true" : "false",
		g.settings.dns ? "true" : "false",
		g.settings.country_verify ? "true" : "false",
		MG_ESC(g.settings.selected),
		MG_ESC(g.settings.dns_server));
	int r = s ? vpn_store_write(g.store, "settings.json", s) : -1;
	free(s);
	return r;
}

static int load_settings(void)
{
	g.settings.reconnect = g.settings.dns = 1;
	char *s = vpn_store_read(g.store, "settings.json", 4096);
	if (!s)
		return exists("settings.json") ? -1 : 0;
	struct mg_str j = mg_str(s);
	if (!vpn_json_valid(s, strlen(s)) ||
	    mg_json_get_long(j, "$.schema", 0) != 1) {
		free(s);
		return -1;
	}
	const char *bool_keys[] = {
		"startup", "reconnect", "kill", "dns", "country_verify"};
	for (unsigned i = 0; i < sizeof bool_keys / sizeof *bool_keys; i++) {
		char path[64];
		bool value;
		snprintf(path, sizeof path, "$.%s", bool_keys[i]);
		if (mg_json_get_tok(j, path).len &&
		    !mg_json_get_bool(j, path, &value)) {
			free(s);
			return -1;
		}
	}
	const char *string_keys[] = {"selected", "dns_server"};
	for (unsigned i = 0; i < 2; i++) {
		char path[64];
		snprintf(path, sizeof path, "$.%s", string_keys[i]);
		struct mg_str token = mg_json_get_tok(j, path);
		if (token.len && token.buf[0] != '"') {
			free(s);
			return -1;
		}
	}
	bool b;
	if (mg_json_get_bool(j, "$.startup", &b))
		g.settings.startup = b;
	if (mg_json_get_bool(j, "$.reconnect", &b))
		g.settings.reconnect = b;
	if (mg_json_get_bool(j, "$.kill", &b))
		g.settings.kill = b;
	if (mg_json_get_bool(j, "$.dns", &b))
		g.settings.dns = b;
	if (mg_json_get_bool(j, "$.country_verify", &b))
		g.settings.country_verify = b;
	get(j, "selected", g.settings.selected, sizeof g.settings.selected);
	get(j,
	    "dns_server",
	    g.settings.dns_server,
	    sizeof g.settings.dns_server);
	free(s);
	return (*g.settings.selected && !vpn_id(g.settings.selected)) ? -1 : 0;
}

static int session(int enabled)
{
	char *s = mg_mprintf("{\"schema\":1,\"enabled\":%s}",
			     enabled ? "true" : "false");
	int r = s ? vpn_store_write(g.run, "session.json", s) : -1;
	free(s);
	return r;
}

static void detect(void)
{
	g.available = 0;
	g.version[0] = 0;
	FILE *f = fopen("/etc/squeezeos.version", "r");
	if (f) {
		if (!fgets(g.firmware, sizeof g.firmware, f))
			g.firmware[0] = 0;
		fclose(f);
		g.firmware[strcspn(g.firmware, "\r\n")] = 0;
	}
	strcpy(g.device, "baby");
#ifdef SB_TARGET_FAB4
	strcpy(g.device, "fab4");
#endif
#if defined(__arm__)
	char cpu[8192], board[128] = "";
	if (read_file("/proc/cpuinfo", cpu, sizeof cpu)) {
		char *hardware = strstr(cpu, "Hardware");
		if (hardware && (hardware = strchr(hardware, ':'))) {
			hardware++;
			while (*hardware == ' ' || *hardware == '\t')
				hardware++;
			size_t n = strcspn(hardware, "\r\n");
			if (n < sizeof board) {
				memcpy(board, hardware, n);
				board[n] = 0;
			}
		}
	}
	const char *detected =
		!strcmp(board, "Baby") ||
				!strcmp(board, "Logitech MX25 Baby Board") ||
				!strcmp(board, "Logitech Baby Board")
			? "baby"
		: !strcmp(board, "Logitech Fab4 Board") ? "fab4"
							: "unknown";
	if (strcmp(detected, g.device)) {
		snprintf(g.device, sizeof g.device, "%s", detected);
		error("VPN_NOT_AVAILABLE",
		      "Board identity does not match this VPN binary; install "
		      "the correct Baby or Fab4 package");
		return;
	}
#else
	strcpy(g.device, "host");
#endif
	struct __user_cap_header_struct header = {_LINUX_CAPABILITY_VERSION_1,
						  0};
	struct __user_cap_data_struct data;
	if (geteuid() != 0 || syscall(SYS_capget, &header, &data) ||
	    !(data.effective & (1u << CAP_NET_ADMIN))) {
		error("VPN_NOT_AVAILABLE",
		      "VPN manager requires root with CAP_NET_ADMIN");
		return;
	}
	const char *path = vpn_program("openvpn");
	if (!path) {
		error("VPN_NOT_AVAILABLE",
		      "OpenVPN is absent. Install Community Firmware with "
		      "OpenVPN 2.5+ and TUN support.");
		return;
	}
	snprintf(g.executable, sizeof g.executable, "%s", path);
	char output[4096];
	char *args[] = {g.executable, "--version", NULL};
	if (vpn_exec(g.executable, args, output, sizeof output, 2000) ||
	    strncmp(output, "OpenVPN ", 8)) {
		error("VPN_NOT_AVAILABLE",
		      "Cannot determine the installed OpenVPN version");
		return;
	}
	unsigned major = 0, minor = 0;
	if (sscanf(output, "OpenVPN %u.%u", &major, &minor) != 2 ||
	    major != 2 || minor < 5) {
		error("VPN_NOT_AVAILABLE",
		      "OpenVPN 2.5 or later in the 2.x series is required");
		return;
	}
	output[strcspn(output, "\r\n")] = 0;
	snprintf(g.version, sizeof g.version, "%.159s", output);
	struct stat st;
	if (stat("/dev/net/tun", &st) || !S_ISCHR(st.st_mode) ||
	    major(st.st_rdev) != 10 || minor(st.st_rdev) != 200) {
		error("VPN_TUN_ERROR",
		      "TUN device is absent. Enable the firmware's TUN "
		      "support; no module is loaded automatically.");
		return;
	}
	int fd = open("/dev/net/tun", O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		error("VPN_TUN_ERROR",
		      "TUN cannot be opened by the VPN service");
		return;
	}
	close(fd);
	if (vpn_ipv6() && !vpn_firewall_capability(1)) {
		error("VPN_IPV6_PROTECTION_UNAVAILABLE",
		      "IPv6 is available but its public traffic cannot be "
		      "safely blocked: ip6tables is missing");
		return;
	}
	if (g.settings.dns && !vpn_dns_capability()) {
		error("VPN_DNS_ERROR",
		      "The firmware resolver file cannot be safely updated "
		      "atomically; VPN DNS is unavailable");
		return;
	}
	FILE *forward = fopen("/proc/sys/net/ipv4/ip_forward", "r");
	int forwarding = 0;
	if (forward) {
		if (fscanf(forward, "%d", &forwarding) != 1)
			forwarding = 1;
		fclose(forward);
	}
	if (forwarding) {
		error("VPN_CONFIGURATION_UNSUPPORTED",
		      "IP forwarding is enabled; this client does not provide "
		      "VPN routing for other devices");
		return;
	}
	g.available = 1;
	g.error[0] = g.category[0] = 0;
	state("DISCONNECTED");
}

static char *capabilities(void)
{
	char *s = mg_mprintf(
		"{\"available\":%s,\"device\":%m,\"firmware\":%m,\"openvpn_"
		"path\":%m,\"openvpn_version\":%m,\"reason\":%m,\"credential_"
		"transport\":\"paired HTTPS portal; SSH loopback recovery "
		"optional\",\"kill_switch_available\":%s,\"boot_time_leak_"
		"protection\":false,\"ipv6\":%m,\"profile_limit\":16,\"config_"
		"limit\":49152}",
		g.available ? "true" : "false",
		MG_ESC(g.device),
		MG_ESC(g.firmware),
		MG_ESC(g.executable),
		MG_ESC(g.version),
		MG_ESC(g.available ? "" : g.error),
		g.available && vpn_firewall_capability(0) ? "true" : "false",
		MG_ESC(vpn_ipv6()
			       ? "Requires managed public IPv6 firewall block"
			       : "Not available"));
	return s;
}

static char *settings_json(void)
{
	return mg_mprintf("{\"startup\":%s,\"reconnect\":%s,\"kill\":%s,"
			  "\"dns\":%s,\"country_verify\":%s,\"selected\":%m,"
			  "\"dns_server\":%m}",
			  g.settings.startup ? "true" : "false",
			  g.settings.reconnect ? "true" : "false",
			  g.settings.kill ? "true" : "false",
			  g.settings.dns ? "true" : "false",
			  g.settings.country_verify ? "true" : "false",
			  MG_ESC(g.settings.selected),
			  MG_ESC(g.settings.dns_server));
}

static char *verification_json(void);

static char *status_json(void)
{
	char profile_name[129] = "", country[80] = "";
	char *stored = load_profile(g.settings.selected);
	if (stored) {
		get(mg_str(stored), "name", profile_name, sizeof profile_name);
		get(mg_str(stored), "country", country, sizeof country);
		vpn_wipe(stored, strlen(stored));
		free(stored);
	}
	char *traffic = vpn_traffic_json(&g.traffic, mg_millis()),
	     *verification = verification_json();
	char *out = mg_mprintf(
		"{\"profile_name\":%m,\"configured_country\":%m,\"available\":%"
		"s,\"enabled\":%s,\"state\":%m,\"selected_"
		"profile_id\":%m,\"active_profile_id\":%m,\"public_ip\":%m,"
		"\"public_ip_verified\":%s,\"public_ip_checked_at\":%ld,"
		"\"configured_country_verified\":false,\"verified_country\":"
		"%m,\"tunnel_interface\":%m,\"tunnel_address\":%m,"
		"\"endpoint\":%m,\"protocol\":%m,\"connection_duration_"
		"seconds\":%ld,\"last_attempt\":%ld,\"reconnect_attempts\":%u,"
		"\"kill_switch_active\":%s,\"vpn_dns_active\":%s,\"ipv6_"
		"protection\":%m,\"local_network_access\":%s,\"last_error\":%m,"
		"\"error_category\":%m,\"network_ready_generation\":%u,"
		"\"verified_country_name\":%m,\"verified_region\":%m,"
		"\"verified_city\":%m,\"verification\":%s,\"traffic\":%s}",
		MG_ESC(profile_name),
		MG_ESC(country),
		g.available ? "true" : "false",
		g.desired ? "true" : "false",
		MG_ESC(g.state),
		MG_ESC(g.settings.selected),
		MG_ESC(!strcmp(g.state, "CONNECTED") ? g.active : ""),
		MG_ESC(g.public_ip),
		*g.public_ip ? "true" : "false",
		(long)g.public_checked,
		MG_ESC(g.verified_country),
		MG_ESC(g.network.routes ? VPN_INTERFACE : ""),
		MG_ESC(g.network.routes?g.network.tunnel_address:""),
		MG_ESC(g.endpoint),
		MG_ESC(g.network.protocol),
		g.connected_at ? (long)(time(NULL) - g.connected_at) : 0L,
		(long)g.attempt,
		g.retries,
		g.settings.kill && g.network.guard4 > 0 &&
				(!vpn_ipv6() || g.network.guard6 > 0)
			? "true"
			: "false",
		g.network.dns_active ? "true" : "false",
		MG_ESC(g.network.guard6 > 0 ? "Blocked while VPN requested"
		       : vpn_ipv6()	    ? "Unprotected"
					    : "Not available"),
		(!g.desired || g.network.local_count > 0) ? "true" : "false",
		MG_ESC(g.error),
		MG_ESC(g.category),
		g.generation,
		MG_ESC(g.location.country),
		MG_ESC(g.location.region),
		MG_ESC(g.location.city),
		verification ? verification : "{}",
		traffic ? traffic : "{}");
	free(traffic);
	free(verification);
	return out;
}

static void stop_child(void)
{
	traffic_sample_now();
	vpn_traffic_pause(&g.traffic, !g.desired);
	if (g.child > 0 &&
	    !g.stop_at) { /* Only an unreaped direct child can be signalled; PID
			     reuse is impossible. */
		kill(g.child, SIGTERM);
		g.stop_at = mg_millis() + 8000;
	}
	verify_invalidate();
	g.connected_at = 0;

	if (g.resolver > 0) {
		kill(g.resolver, SIGKILL);
		waitpid(g.resolver, NULL, 0);
		g.resolver = 0;
	}
	if (g.resolver_fd >= 0) {
		close(g.resolver_fd);
		g.resolver_fd = -1;
	}
	g.resolving = 0;
}

static int private_runtime_file(const char *name, const char *text)
{
	return vpn_store_write(g.run, name, text);
}

static int launch_profile(void)
{
	if (if_nametoindex(VPN_INTERFACE)) {
		error("VPN_TUN_ERROR",
		      "The reserved VPN interface already exists; unrelated "
		      "interfaces were retained");
		return -1;
	}
	char *stored = load_profile(g.settings.selected);
	if (!stored) {
		error("VPN_PROFILE_INVALID",
		      "Selected profile is missing or invalid");
		return -1;
	}
	struct mg_str j = mg_str(stored);
	char *config = mg_json_get_str(j, "$.configuration"),
	     *user = mg_json_get_str(j, "$.username"),
	     *pass = mg_json_get_str(j, "$.password");
	char message[192];
	if (!config ||
	    vpn_profile_parse(config, &g.profile, message, sizeof message)) {
		error("VPN_PROFILE_INVALID",
		      "Stored profile failed validation");
		goto bad;
	}
	if (g.profile.needs_auth && (!user || !*user || !pass || !*pass)) {
		error("VPN_CREDENTIALS_MISSING",
		      "Provision OpenVPN service username and password through "
		      "the secure profile form");
		goto bad;
	}
	char auth[1200] = "";
	if (g.profile.needs_auth) {
		snprintf(auth, sizeof auth, "%s\n%s\n", user, pass);
		if (private_runtime_file("credentials", auth)) {
			error("VPN_CONFIGURATION_UNSUPPORTED",
			      "Cannot securely stage credentials");
			goto bad;
		}
	}
	vpn_wipe(auth, sizeof auth);
	char *runtime = mg_mprintf(
		"%s\nremote %s %u %s\ndev %s\ndev-type "
		"tun\nnobind\nifconfig-noexec\nroute-noexec\nroute 0.0.0.0 "
		"128.0.0.0\npull-filter ignore \"route "
		"\"\npull-filter ignore redirect-gateway\npull-filter ignore "
		"ifconfig-ipv6\npull-filter ignore route-ipv6\npull-filter "
		"reject block-outside-dns\npull-filter reject "
		"compress\npull-filter reject comp-lzo\nallow-compression "
		"no\nauth-retry none\npull-filter ignore \"ping "
		"\"\npull-filter ignore \"ping-restart \"\npull-filter ignore "
		"\"ping-exit \"\ntls-version-min %s\nscript-security 2\nup "
		"\"%s --hook "
		"%s\"\ndown \"%s --hook %s\"\ndown-pre\nup-restart\nremap-usr1 "
		"SIGTERM\nconnect-retry-max 1\nconnect-timeout "
		"15\nresolv-retry 0\nping 10\nping-exit 30\nverb 3\nmanagement "
		"%s/management.sock unix\nmanagement-hold\n%s",
		g.profile.config,
		g.endpoint,
		g.network.port,
		g.network.protocol,
		VPN_INTERFACE,
		g.profile.tls_min,
		g.self,
		g.socket,
		g.self,
		g.socket,
		g.runtime,
		g.profile.needs_auth ? "" : "");
	if (!runtime)
		goto bad;
	if (g.profile.needs_auth) {
		char *new_config = mg_mprintf(
			"%sauth-user-pass %s/credentials\nauth-nocache\n",
			runtime,
			g.runtime);
		free(runtime);
		runtime = new_config;
	}
	if (!runtime || private_runtime_file("client.ovpn", runtime)) {
		free(runtime);
		error("VPN_CONFIGURATION_UNSUPPORTED",
		      "Cannot stage sanitized runtime configuration");
		goto bad;
	}
	vpn_wipe(runtime, strlen(runtime));
	free(runtime);
	char management[640];
	snprintf(
		management, sizeof management, "%s/management.sock", g.runtime);
	unlink(management);
	unsigned fd_limit;
	if (!sb_process_fd_limit(&fd_limit))
		goto bad;
	int diagnostics[2];
	if (pipe(diagnostics))
		goto bad;
	g.child = fork();
	if (g.child < 0) {
		close(diagnostics[0]);
		close(diagnostics[1]);
		g.child = 0;
		error("VPN_PROCESS_EXITED", "Cannot start OpenVPN");
		goto bad;
	}
	if (!g.child) {
		if (prctl(PR_SET_PDEATHSIG, SIGTERM))
			_exit(127);
		if (getppid() == 1)
			_exit(127);
		char path[640];
		snprintf(path, sizeof path, "%s/client.ovpn", g.runtime);
		int null = open("/dev/null", O_RDWR);
		dup2(null, 0);
		dup2(diagnostics[1], 1);
		dup2(diagnostics[1], 2);
		close(diagnostics[0]);
		close(diagnostics[1]);
		setpriority(PRIO_PROCESS, 0, 10);
		sb_process_close_fds(fd_limit, -1);
		execl(g.executable,
		      g.executable,
		      "--config",
		      path,
		      (char *)NULL);
		_exit(127);
	}
	close(diagnostics[1]);
	g.output = diagnostics[0];
	fcntl(g.output, F_SETFL, O_NONBLOCK);
	fcntl(g.output, F_SETFD, FD_CLOEXEC);
	if (vpn_process_record(g.run, g.child, g.executable)) {
		kill(g.child, SIGTERM);
		g.stop_at = mg_millis() + 8000;
		error("VPN_PROCESS_EXITED",
		      "Cannot persist managed process identity");
		goto bad;
	}
	strcpy(g.active, g.settings.selected);
	g.child_started = mg_millis();
	g.deadline = g.child_started + 60000;
	g.hook_ready = g.management_ready = 0;
	g.management_used = 0;
	/* Retain the previous error until a verified connection succeeds. */
	if (config) {
		vpn_wipe(config, strlen(config));
		free(config);
	}
	if (pass) {
		vpn_wipe(pass, strlen(pass));
		free(pass);
	}
	free(user);
	vpn_wipe(stored, strlen(stored));
	free(stored);
	return 0;
bad:
	if (g.child <= 0) {
		vpn_store_delete(g.run, "credentials");
		vpn_store_delete(g.run, "client.ovpn");
	}
	vpn_wipe(g.profile.config, sizeof g.profile.config);
	if (config) {
		vpn_wipe(config, strlen(config));
		free(config);
	}
	if (pass) {
		vpn_wipe(pass, strlen(pass));
		free(pass);
	}
	free(user);
	vpn_wipe(stored, strlen(stored));
	free(stored);
	return -1;
}

static void stamp_success(void)
{
	char *stored = load_profile(g.active);
	if (!stored)
		return;
	struct mg_str j = mg_str(stored),
		      token = mg_json_get_tok(j, "$.last_success");
	char *old = mg_json_get_str(j, "$.last_success");
	long last = old ? strtol(old, NULL, 10) : 0;
	free(old);
	if (token.len && time(NULL) - last >= 3600) {
		char value[32], file[48];
		snprintf(value, sizeof value, "%ld", (long)time(NULL));
		snprintf(file, sizeof file, "%.32s.json", g.active);
		size_t prefix = (size_t)(token.buf - stored);
		char *updated = mg_mprintf("%.*s%m%s",
					   (int)prefix,
					   stored,
					   MG_ESC(value),
					   token.buf + token.len);
		if (updated) {
			vpn_store_write(g.store, file, updated);
			vpn_wipe(updated, strlen(updated));
			free(updated);
		}
	}
	vpn_wipe(stored, strlen(stored));
	free(stored);
}

static void location_clear(void)
{
	memset(&g.location, 0, sizeof g.location);
	g.verified_country[0] = 0;
	g.location_checked = 0;
	g.location_success_ms = 0;
	g.location_error[0] = 0;
	strcpy(g.location_state,
	       g.settings.country_verify ? "unverified" : "disabled");
	g.location_pending = 0;
}

static void verify_invalidate(void)
{
	if (g.verify) {
		if (g.verify_country && !g.verify_done &&
		    g.cache_current >= 0 && g.cache_current < 8 &&
		    !g.location_cache[g.cache_current].valid)
			g.location_cache[g.cache_current].attempt_ms = 0;
		g.verify->is_closing = 1;
		g.verify = NULL;
	}
	g.public_ip[0] = 0;
	g.public_checked = 0;
	g.public_success_ms = 0;
	g.verify_error[0] = 0;
	strcpy(g.verify_state, "unverified");
	location_clear();
}

static void verify_failure(int country, const char *message)
{
	if (country) {
		snprintf(g.location_error,
			 sizeof g.location_error,
			 "%s",
			 message);
		strcpy(g.location_state, "unavailable");
		g.location_pending = 0;
		if (g.cache_current >= 0 && g.cache_current < 8)
			snprintf(g.location_cache[g.cache_current].error,
				 sizeof g.location_cache[0].error,
				 "%s",
				 message);
	} else {
		g.public_ip[0] = 0;
		location_clear();
		strcpy(g.verify_state, "unavailable");
		snprintf(g.verify_error, sizeof g.verify_error, "%s", message);
	}
}

static void location_restore(unsigned i)
{
	g.location = g.location_cache[i].location;
	strcpy(g.verified_country, g.location.code);
	g.location_checked = g.location_cache[i].checked_at;
	g.location_success_ms = g.location_cache[i].success_ms;
	g.location_error[0] = 0;
	strcpy(g.location_state, "verified");
	g.location_pending = 0;
}

static void verify_event(struct mg_connection *, int, void *);

static void verify_start(int country)
{
	g.verify_country = country;
	g.verify_done = 0;
	g.verify_deadline = mg_millis() + 8000;
	if (country) {
		strcpy(g.location_state, "checking");
		g.location_error[0] = 0;
	} else {
		strcpy(g.verify_state, "checking");
		g.verify_error[0] = 0;
	}
	/* Mongoose can emit OPEN/RESOLVE/CONNECT before connect returns. */
 g.verify_starting=1;
	g.verify = mg_http_connect(
		&g.mgr, "http://127.0.0.1:8765", verify_event, NULL);
 g.verify_starting=0;
	if (!g.verify)
		verify_failure(country,
			       "HTTPS proxy connection could not be started");
}

static void verify_location_schedule(void)
{
	if (!g.settings.country_verify || !*g.public_ip ||
	    strcmp(g.state, "CONNECTED"))
		return;
	if (g.verify) {
		g.location_pending = 1;
		return;
	}
	uint64_t now = mg_millis();
	for (unsigned i = 0; i < 8; i++)
		if (!strcmp(g.location_cache[i].ip, g.public_ip) &&
		    g.location_cache[i].attempt_ms &&
		    now - g.location_cache[i].attempt_ms < 3600000) {
			if (g.location_cache[i].valid)
				location_restore(i);
			else {
				strcpy(g.location_state, "unavailable");
				snprintf(g.location_error,
					 sizeof g.location_error,
					 "%s",
					 g.location_cache[i].error);
				g.location_pending = 0;
			}
			return;
		}
	if (g.location_global_attempt &&
	    now - g.location_global_attempt < 60000) {
		g.location_pending = 1;
		g.location_due_ms = g.location_global_attempt + 60000;
		strcpy(g.location_state, "pending");
		return;
	}
	unsigned slot = g.cache_cursor++ % 8;
	memset(&g.location_cache[slot], 0, sizeof g.location_cache[slot]);
	strcpy(g.location_cache[slot].ip, g.public_ip);
	g.location_cache[slot].attempt_ms = now;
	strcpy(g.location_cache[slot].error, "Location lookup unavailable");
	g.cache_current = (int)slot;
	strcpy(g.country_ip, g.public_ip);
	g.location_global_attempt = now;
	g.location_pending = 0;
	verify_start(1);
}

static void verify_event(struct mg_connection *c, int event, void *data)
{
	if(g.verify_starting&&!g.verify)g.verify=c;
	if (event == MG_EV_OPEN)
		return;
	if (g.verify != c) {
		c->is_closing = 1;
		return;
	}
	if (strcmp(g.state, "CONNECTED") ||
	    g.verify_generation != g.generation) {
		c->is_closing = 1;
		return;
	}
	if (event == MG_EV_CONNECT) {
		if (g.verify_country)
			mg_printf(c,
				  "GET "
				  "/https/ipwho.is/"
				  "%s?fields=success,ip,country_code,country,"
				  "region,city HTTP/1.0\r\nHost: "
				  "127.0.0.1\r\nX-SB-Public-Fetch: "
				  "1\r\nConnection: close\r\n\r\n",
				  g.country_ip);
		else
			mg_printf(c,
				  "GET /https/api.ipify.org/ HTTP/1.0\r\nHost: "
				  "127.0.0.1\r\nX-SB-Public-Fetch: "
				  "1\r\nConnection: close\r\n\r\n");
	} else if (event == MG_EV_HTTP_MSG) {
		struct mg_http_message *m = data;
		g.verify_done = 1;
		if (mg_http_status(m) != 200) {
			char reason[96];
			snprintf(reason,
				 sizeof reason,
				 "HTTPS verification service returned HTTP %d",
				 mg_http_status(m));
			verify_failure(g.verify_country, reason);
		} else if (g.verify_country) {
			VpnLocation result;
			if (!g.settings.country_verify ||
			    strcmp(g.country_ip, g.public_ip) ||
			    vpn_verify_location(m->body.buf,
						m->body.len,
						g.country_ip,
						&result))
				verify_failure(1,
					       "Location response was invalid "
					       "or referred to another IP");
			else {
				unsigned slot = (unsigned)g.cache_current;
				g.location_cache[slot].location = result;
				g.location_cache[slot].valid = 1;
				g.location_cache[slot].checked_at = time(NULL);
				g.location_cache[slot].success_ms = mg_millis();
				g.location_cache[slot].error[0] = 0;
				location_restore(slot);
			}
		} else {
			char ip[16];
			if (vpn_verify_ip(m->body.buf, m->body.len, ip))
				verify_failure(
					0,
					"Verification service did not return a "
					"valid public IPv4 address");
			else {
				location_clear();
				strcpy(g.public_ip, ip);
				g.public_checked = time(NULL);
				g.public_success_ms = mg_millis();
				g.verify_error[0] = 0;
				strcpy(g.verify_state, "verified");
				g.location_pending = g.settings.country_verify;
				g.location_due_ms = 0;
			}
		}
		c->is_closing = 1;
	} else if (event == MG_EV_ERROR) {
		g.verify_done = 1;
		verify_failure(g.verify_country,
			       "HTTPS proxy connection failed");
		c->is_closing = 1;
	} else if (event == MG_EV_POLL && mg_millis() > g.verify_deadline) {
		g.verify_done = 1;
		verify_failure(g.verify_country,
			       "HTTPS verification timed out");
		c->is_closing = 1;
	} else if (event == MG_EV_CLOSE) {
		int country = g.verify_country;
		if (!g.verify_done)
			verify_failure(
				country,
				"HTTPS verification response was incomplete");
		g.verify = NULL;
		if (!country && g.location_pending)
			verify_location_schedule();
	}
	if (c->recv.len > 8192 && !c->is_closing) {
		g.verify_done = 1;
		verify_failure(
			g.verify_country,
			"HTTPS verification response exceeded its size limit");
		c->is_closing = 1;
	}
}

static void verify_public(void)
{
	uint64_t now = mg_millis();
	if (g.verify || strcmp(g.state, "CONNECTED") ||
	    (g.verify_generation == g.generation &&
	     now - g.verify_last_attempt < 60000))
		return;
	g.verify_generation = g.generation;
	g.verify_last_attempt = now;
	verify_start(0);
}

static char *verification_json(void)
{
	uint64_t now = mg_millis();
	uint64_t age = g.public_success_ms && now >= g.public_success_ms
			       ? (now - g.public_success_ms) / 1000
			       : 0,
		 location_age =
			 g.location_success_ms && now >= g.location_success_ms
				 ? (now - g.location_success_ms) / 1000
				 : 0;
	unsigned retry =
		g.verify_generation == g.generation &&
				now - g.verify_last_attempt < 60000
			? (unsigned)((60000 - (now - g.verify_last_attempt) +
				      999) /
				     1000)
			: 0;
	return mg_mprintf(
		"{\"state\":%m,\"error\":%m,\"last_success_at\":%ld,\"age_"
		"seconds\":%llu,\"refresh_after_seconds\":%u,\"location_"
		"state\":%m,\"location_error\":%m,\"location_checked_at\":%ld,"
		"\"location_age_seconds\":%llu,\"approximate\":true}",
		MG_ESC(*g.verify_state ? g.verify_state : "unverified"),
		MG_ESC(g.verify_error),
		(long)g.public_checked,
		(unsigned long long)age,
		retry,
		MG_ESC(*g.location_state	   ? g.location_state
		       : g.settings.country_verify ? "unverified"
						   : "disabled"),
		MG_ESC(g.location_error),
		(long)g.location_checked,
		(unsigned long long)location_age);
}

static void traffic_sample_now(void)
{
	if (!g.traffic.running || !g.network.routes ||
	    strcmp(g.traffic.profile, g.active))
		return;
	VpnTrafficSample sample;
	if (vpn_traffic_read(g.traffic.interface, g.traffic.source, &sample))
		vpn_traffic_missing(&g.traffic);
	else
		vpn_traffic_update(
			&g.traffic, &sample, mg_millis(), (int64_t)time(NULL));
}

static void management_line(char *line)
{
	if ((strstr(line, "AUTH_FAILED") &&
	     strstr(line, "cipher negotiation failed")) ||
	    strstr(line, "failed to negotiate cipher") ||
	    strstr(line, "no shared cipher")) {
		g.auth_failed = 1;
		error("VPN_CONFIGURATION_UNSUPPORTED",
		      "VPN server and requested data ciphers are incompatible; "
		      "cryptography was not downgraded");
		stop_child();
	} else if (strstr(line, "AUTH_FAILED") ||
		   strstr(line, "Verification Failed")) {
		g.auth_failed = 1;
		error("VPN_AUTHENTICATION_FAILED",
		      "VPN authentication failed; verify service credentials");
		stop_child();
	} else if (strstr(line, "TLS Error") || strstr(line, "VERIFY ERROR")) {
		error("VPN_TLS_ERROR",
		      "VPN certificate verification or TLS negotiation failed");
		stop_child();
	} else if (!strncmp(line, ">PASSWORD:", 10)) {
		g.auth_failed = 1;
		error("VPN_CREDENTIALS_MISSING",
		      "OpenVPN requested interactive credentials or a key "
		      "passphrase; secure non-interactive credentials are "
		      "required");
		stop_child();
	} else if (!strncmp(line, ">STATE:", 7)) {
		if (strstr(line, ",CONNECTED,SUCCESS,"))
			g.management_ready = 1;
		else if (strstr(line, ",RECONNECTING,")) {
			state("RECONNECTING");
			stop_child();
		}
	}
}

static void read_diagnostics(void)
{
	if (g.output < 0)
		return;
	char text[4097];
	for (int i = 0; i < 8; i++) {
		ssize_t n = read(g.output, text, sizeof text - 1);
		if (n <= 0)
			break;
		text[n] = 0;
		management_line(text);
		if (strstr(text, "Options error:") ||
		    strstr(text, "Unrecognized option")) {
			g.auth_failed = 1;
			error("VPN_CONFIGURATION_UNSUPPORTED",
			      "Installed OpenVPN rejected a managed option or "
			      "requested cipher; cryptography was not "
			      "downgraded");
			stop_child();
		} else if (strstr(text, "Cannot ioctl TUNSETIFF") ||
			   strstr(text, "Cannot open TUN/TAP")) {
			g.auth_failed = 1;
			error("VPN_TUN_ERROR",
			      "OpenVPN could not create the dedicated TUN "
			      "interface");
			stop_child();
		} else if (strstr(text, "Cannot load") ||
			   strstr(text, "Private key does not match")) {
			g.auth_failed = 1;
			error("VPN_TLS_ERROR",
			      "OpenVPN could not load or match the inline "
			      "certificates and unencrypted private key");
			stop_child();
		}
		memset(text, 0, sizeof text);
	}
}

static void read_management(void)
{
	if (g.child <= 0)
		return;
	if (g.management < 0) {
		char p[640];
		snprintf(p, sizeof p, "%s/management.sock", g.runtime);
		struct sockaddr_un sa;
		memset(&sa, 0, sizeof sa);
		sa.sun_family = AF_UNIX;
		if (strlen(p) >= sizeof sa.sun_path)
			return;
		strcpy(sa.sun_path, p);
		int fd = vpn_socket(AF_UNIX, SOCK_STREAM, 0, 1);
		if (fd < 0)
			return;
		if (connect(fd, (struct sockaddr *)&sa, sizeof sa)) {
			close(fd);
			return;
		}
		struct ucred peer;
		socklen_t length = sizeof peer;
		if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &length) ||
		    peer.uid != geteuid() || peer.pid != g.child) {
			close(fd);
			error("VPN_PROCESS_EXITED",
			      "OpenVPN management peer identity did not match "
			      "the owned child");
			stop_child();
			return;
		}
		chmod(p, 0600);
		g.management = fd;
		const char *commands = "state on\nlog on\nhold release\n";
		if (send(fd, commands, strlen(commands), MSG_NOSIGNAL) !=
		    (ssize_t)strlen(commands)) {
			close(fd);
			g.management = -1;
		}
		return;
	}
	char buffer[1024];
	ssize_t n = read(g.management, buffer, sizeof buffer);
	if (n == 0) {
		close(g.management);
		g.management = -1;
		return;
	}
	if (n < 0)
		return;
	if ((size_t)n > sizeof g.management_buffer - 1 - g.management_used) {
		error("VPN_CONFIGURATION_UNSUPPORTED",
		      "OpenVPN management message exceeded its bound");
		stop_child();
		return;
	}
	memcpy(g.management_buffer + g.management_used, buffer, (size_t)n);
	g.management_used += (size_t)n;
	g.management_buffer[g.management_used] = 0;
	char *line = g.management_buffer, *end;
	while ((end = strchr(line, '\n'))) {
		*end = 0;
		management_line(line);
		line = end + 1;
	}
	size_t left = g.management_used - (size_t)(line - g.management_buffer);
	memmove(g.management_buffer, line, left);
	g.management_used = left;
}

static int begin_resolve(void)
{
	g.restart_requested = 0;
	char *s = load_profile(g.settings.selected);
	if (!s) {
		error("VPN_PROFILE_INVALID", "Select a valid profile");
		return -1;
	}
	char *configuration = mg_json_get_str(mg_str(s), "$.configuration");
	char message[192];
	int invalid = !configuration || vpn_profile_parse(configuration,
							  &g.profile,
							  message,
							  sizeof message);
	if (configuration) {
		vpn_wipe(configuration, strlen(configuration));
		free(configuration);
	}
	vpn_wipe(s, strlen(s));
	free(s);
	if (invalid) {
		error("VPN_PROFILE_INVALID",
		      "Selected profile failed validation");
		return -1;
	}
	VpnNetwork old = g.network;
	memset(&g.network, 0, sizeof g.network);
	g.network.guard4 = old.guard4;
	g.network.guard6 = old.guard6;
	if (vpn_network_discover(&g.network)) {
		g.network = old;
		error("VPN_SERVER_UNREACHABLE",
		      "No usable physical default route and LAN subnet");
		return -1;
	}
	if (vpn_network_policy_standard() ||
	    vpn_network_conflicts(&g.network)) {
		error("VPN_LOCAL_ROUTE_CONFLICT",
		      "Existing public-specific routes or the reserved VPN "
		      "route namespace conflict with safe full-tunnel routing; "
		      "unrelated routes were retained");
		g.auth_failed = 1;
		g.retry_at = UINT64_MAX;
		return -1;
	}
	int i = (int)(g.retries % (unsigned)g.profile.remotes);
	g.network.port = g.profile.port[i];
	strcpy(g.network.protocol, g.profile.proto[i]);
	struct in_addr literal;
	if (g.settings.kill &&
	    inet_pton(AF_INET, g.profile.host[i], &literal) != 1) {
		error("VPN_CONFIGURATION_UNSUPPORTED",
		      "Kill switch currently requires literal IPv4 remote "
		      "endpoints; unrestricted endpoint DNS is not permitted");
		return -1;
	}
	int pipes[2];
	if (pipe(pipes)) {
		error("VPN_SERVER_UNREACHABLE",
		      "Cannot start bounded DNS resolution");
		return -1;
	}
	vpn_wipe(g.profile.config, sizeof g.profile.config);
	g.resolver = fork();
	if (g.resolver < 0) {
		close(pipes[0]);
		close(pipes[1]);
		return -1;
	}
	if (!g.resolver) {
		close(pipes[0]);
		if (prctl(PR_SET_PDEATHSIG, SIGKILL) || getppid() == 1)
			_exit(127);
		struct addrinfo hints, *list = NULL;
		memset(&hints, 0, sizeof hints);
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_DGRAM; /* Resolver child never changes
						   network state. */
		if (!getaddrinfo(g.profile.host[i], NULL, &hints, &list) &&
		    list) {
			struct addrinfo *selected = list;
			unsigned count = 0;
			for (struct addrinfo *p = list; p && count < 8;
			     p = p->ai_next)
				count++;
			unsigned chosen =
				count ? (g.retries /
					 (unsigned)g.profile.remotes) %
						count
				      : 0;
			while (chosen-- && selected->ai_next)
				selected = selected->ai_next;
			struct in_addr ip =
				((struct sockaddr_in *)selected->ai_addr)
					->sin_addr;
			ssize_t written = write(pipes[1], &ip, sizeof ip);
			if (written != (ssize_t)sizeof ip) {
				freeaddrinfo(list);
				close(pipes[1]);
				_exit(1);
			}
			freeaddrinfo(list);
		}
		close(pipes[1]);
		_exit(0);
	}
	close(pipes[1]);
	g.resolver_fd = pipes[0];
	fcntl(g.resolver_fd, F_SETFL, O_NONBLOCK);
	g.resolving = 1;
	g.deadline = mg_millis() + 8000;
	g.attempt = time(NULL);
	state(g.retries ? "RECONNECTING" : "CONNECTING");
	return 0;
}

static void tick(void)
{
	uint64_t now = mg_millis();
	if (g.resolving) {
		struct in_addr ip;
		ssize_t n = read(g.resolver_fd, &ip, sizeof ip);
		if (n == (ssize_t)sizeof ip) {
			waitpid(g.resolver, NULL, 0);
			g.resolver = 0;
			close(g.resolver_fd);
			g.resolver_fd = -1;
			g.resolving = 0;
			inet_ntop(AF_INET, &ip, g.endpoint, sizeof g.endpoint);
			strcpy(g.network.endpoint, g.endpoint);
			vpn_network_endpoint(&g.network);
			if (vpn_network_guard(
				    &g.network, &g.settings, g.runtime)) {
				error("VPN_FIREWALL_ERROR",
				      "Cannot install required network "
				      "protection; connection was not started");
				g.auth_failed = 1;
				g.retry_at = UINT64_MAX;
			} else if (launch_profile()) {
				g.auth_failed = 1;
				g.retry_at = UINT64_MAX;
				if (!g.settings.kill)
					vpn_network_cleanup(
						&g.network, 1, g.runtime);
			}
		} else if (now >= g.deadline) {
			stop_child();
			error("VPN_SERVER_UNREACHABLE",
			      "VPN endpoint DNS resolution timed out");
			g.retry_at = now + 30000;
			g.retries++;
		}
	}
	if (g.child > 0) {
		read_diagnostics();
		read_management();
		int status;
		pid_t p = waitpid(g.child, &status, WNOHANG);
		if (p == g.child) {
			g.child = 0;
			if (g.output >= 0) {
				close(g.output);
				g.output = -1;
			}
			vpn_store_delete(g.run, "process.json");
			g.stop_at = 0;
			if (g.management >= 0) {
				close(g.management);
				g.management = -1;
			}
			g.management_ready = g.hook_ready = 0;
			g.connected_at = 0;
			vpn_traffic_pause(&g.traffic, !g.desired);
			verify_invalidate();
			vpn_store_delete(g.run, "credentials");
			vpn_store_delete(g.run, "client.ovpn");
			int cleanup_failed = vpn_network_cleanup(
				&g.network, !g.desired, g.runtime);
			if (cleanup_failed) {
				error("VPN_ROUTING_ERROR",
				      "Owned network cleanup failed; use local "
				      "recovery");
				g.auth_failed = 1;
				g.retry_at = UINT64_MAX;
			} else if (g.desired && g.restart_requested) {
				g.retry_at = 0;
				state("SWITCHING");
			} else if (g.desired && !g.auth_failed &&
				   g.settings.reconnect) {
				g.retries++;
				unsigned shift = g.retries > 6 ? 6 : g.retries;
				g.retry_at = now + (1000u << shift);
				state("RECONNECTING");
			} else if (!g.desired)
				state("DISCONNECTED");
			else {
				g.retry_at = UINT64_MAX;
				if (!*g.error)
					error("VPN_PROCESS_EXITED",
					      "OpenVPN exited before a "
					      "verified tunnel was available");
			}
		} else if (g.stop_at && now >= g.stop_at)
			kill(g.child, SIGKILL);
		else if (!g.stop_at && g.management_ready && g.hook_ready &&
			 strcmp(g.state, "CONNECTED")) {
			int network_result = vpn_network_apply(
				&g.network, &g.settings, g.runtime);
			if (network_result) {
				error(network_result == -2
					      ? "VPN_DNS_ERROR"
					      : "VPN_ROUTING_ERROR",
				      "Tunnel routes, address overlap or VPN "
				      "DNS could not be safely applied");
				stop_child();
			} else {
				g.error[0] = g.category[0] = 0;
				state("CONNECTED");
				g.connected_at = time(NULL);
				g.retries = 0;
				g.generation++;
				vpn_traffic_attach(&g.traffic,
						   VPN_INTERFACE,
						   vpn_traffic_bits());
				traffic_sample_now();
				g.traffic_next = now + 2000;
				stamp_success();
				verify_public();
			}
		} else if (!g.stop_at && strcmp(g.state, "CONNECTED") &&
			   now >= g.deadline) {
			error("VPN_CONNECTION_TIMEOUT",
			      "VPN connection exceeded 60 seconds");
			stop_child();
		}
	}
	if (g.desired && g.available && !g.child && !g.resolving &&
	    !g.auth_failed && now >= g.retry_at) {
		if (begin_resolve()) {
			g.retries++;
			g.retry_at = now + 30000;
			if (!g.settings.reconnect)
				g.retry_at = UINT64_MAX;
		}
	}
	if (!strcmp(g.state, "CONNECTED") && now >= g.traffic_next) {
		g.traffic_next = now + 2000;
		traffic_sample_now();
	}
	if (g.location_pending && !g.verify && now >= g.location_due_ms)
		verify_location_schedule();
	if (!strcmp(g.state, "CONNECTED") && now >= g.network_check) {
		g.network_check = now + 3000;
		if (vpn_network_refresh_dns(&g.network, g.runtime)) {
			error("VPN_DNS_ERROR",
			      "VPN DNS ownership could not be maintained");
			stop_child();
			return;
		}
		VpnNetwork current;
		memset(&current, 0, sizeof current);
		if (vpn_network_discover(&current) ||
		    vpn_network_policy_standard() ||
		    vpn_network_conflicts(&g.network) ||
		    strcmp(current.gateway, g.network.gateway) ||
		    strcmp(current.physical, g.network.physical) ||
		    strcmp(current.subnet, g.network.subnet) ||
		    current.local_count != g.network.local_count ||
		    memcmp(current.local_subnets,
			   g.network.local_subnets,
			   sizeof current.local_subnets) ||
		    current.address_signature != g.network.address_signature ||
		    current.local_ipv6_count != g.network.local_ipv6_count ||
		    memcmp(current.local_ipv6,
			   g.network.local_ipv6,
			   sizeof current.local_ipv6)) {
			state("RECONNECTING");
			stop_child();
		}
	}
}

static char *profile_list(void)
{
	char *out = calloc(1, 65536);
	if (!out)
		return NULL;
	size_t used = 0;
	out[used++] = '[';
	DIR *d = fdopendir(
		openat(g.store, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
	if (!d) {
		free(out);
		return NULL;
	}
	struct dirent *e;
	unsigned scanned = 0, count = 0;
	while ((e = readdir(d)) && scanned++ < 128) {
		if (strlen(e->d_name) != 37 || strcmp(e->d_name + 32, ".json"))
			continue;
		char id[33];
		memcpy(id, e->d_name, 32);
		id[32] = 0;
		if (!vpn_id(id))
			continue;
		char *s = load_profile(id);
		if (!s)
			continue;
		char *public = vpn_public_profile(mg_str(s), id);
		vpn_wipe(s, strlen(s));
		free(s);
		if (!public)
			continue;
		size_t n = strlen(public);
		if (used + n + 3 >= 65536 || count == VPN_PROFILES) {
			free(public);
			closedir(d);
			free(out);
			return NULL;
		}
		if (count++)
			out[used++] = ',';
		memcpy(out + used, public, n);
		used += n;
		free(public);
	}
	closedir(d);
	out[used++] = ']';
	out[used] = 0;
	return out;
}

static int metadata(struct mg_str body,
		    struct mg_str old,
		    const char *key,
		    char *out,
		    size_t cap,
		    int required)
{
	char path[80];
	snprintf(path, sizeof path, "$.%s", key);
	char *v = mg_json_get_str(body, path);
	if (!v)
		v = mg_json_get_str(old, path);
	if (!v) {
		*out = 0;
		return required ? -1 : 0;
	}
	size_t n = strlen(v);
	int invalid = n >= cap || (required && !n);
	for (size_t i = 0; i < n; i++)
		if ((unsigned char)v[i] < 32)
			invalid = 1;
	if (!invalid)
		strcpy(out, v);
	memset(v, 0, n);
	free(v);
	return invalid ? -1 : 0;
}

static char *profile_save(const char *id, struct mg_str body, int create)
{
	char *previous = create ? NULL : load_profile(id);
	if (!create && !previous)
		return failure(404, "VPN_PROFILE_INVALID", "Profile not found");
	if (!create && ((g.child > 0 && !strcmp(id, g.active)) ||
			(g.desired && !strcmp(id, g.settings.selected)))) {
		free(previous);
		return failure(409,
			       "VPN_PROFILE_ACTIVE",
			       "Disconnect before changing an active profile");
	}
	struct mg_str old = previous ? mg_str(previous) : mg_str("{}");
	char name[129], country[80], code[3], provider[80], city[80],
		description[257], username[257], password[513],
		operation[16] = "keep";
	if (metadata(body, old, "name", name, sizeof name, 1) ||
	    metadata(body, old, "country", country, sizeof country, 1) ||
	    metadata(body, old, "country_code", code, sizeof code, 1) ||
	    metadata(body, old, "provider", provider, sizeof provider, 0) ||
	    metadata(body, old, "city", city, sizeof city, 0) ||
	    metadata(body,
		     old,
		     "description",
		     description,
		     sizeof description,
		     0) ||
	    metadata(body, old, "username", username, sizeof username, 0) ||
	    strlen(code) != 2 || code[0] < 'A' || code[0] > 'Z' ||
	    code[1] < 'A' || code[1] > 'Z') {
		free(previous);
		return failure(400,
			       "VPN_PROFILE_INVALID",
			       "Invalid profile metadata; use a two-letter "
			       "uppercase country code");
	}
	char *configuration = mg_json_get_str(body, "$.configuration");
	if (!configuration)
		configuration = mg_json_get_str(old, "$.configuration");
	VpnProfile *parsed = calloc(1, sizeof *parsed);
	char why[192] = "Profile configuration is missing";
	if (!parsed || !configuration ||
	    vpn_profile_parse(configuration, parsed, why, sizeof why)) {
		free(previous);
		free(configuration);
		free(parsed);
		return failure(400,
			       "VPN_PROFILE_INVALID",
			       parsed ? why
				      : "Profile configuration is missing");
	}
	get(body, "password_operation", operation, sizeof operation);
	if (strcmp(operation, "keep") && strcmp(operation, "replace") &&
	    strcmp(operation, "clear")) {
		free(previous);
		free(configuration);
		free(parsed);
		return failure(
			400,
			"VPN_PROFILE_INVALID",
			"Password operation must be keep, replace or clear");
	}
	password[0] = 0;
	if (!strcmp(operation, "replace")) {
		if (metadata(body,
			     mg_str("{}"),
			     "password",
			     password,
			     sizeof password,
			     1))
			goto invalid;
	} else if (strcmp(operation, "clear")) {
		if (metadata(old,
			     mg_str("{}"),
			     "password",
			     password,
			     sizeof password,
			     0))
			goto invalid;
	}
	char generated[33];
	if (create) {
		unsigned char random[16];
		mg_random(random, sizeof random);
		for (int i = 0; i < 16; i++)
			snprintf(generated + i * 2, 3, "%02x", random[i]);
		id = generated;
		char *list = profile_list();
		if (!list) {
			free(previous);
			free(configuration);
			free(parsed);
			return failure(
				507,
				"VPN_PROFILE_INVALID",
				"Profile quota or storage validation failed");
		}
		struct mg_str a = mg_str(list), k, v;
		size_t pos = 0;
		unsigned count = 0;
		while ((pos = mg_json_next(a, pos, &k, &v)))
			count++;
		free(list);
		if (count >= VPN_PROFILES) {
			free(previous);
			free(configuration);
			free(parsed);
			return failure(409,
				       "VPN_PROFILE_INVALID",
				       "Sixteen profile limit reached");
		}
	}
	char created[32], updated[32], last_success[32] = "";
	snprintf(updated, sizeof updated, "%ld", (long)time(NULL));
	if (metadata(old,
		     mg_str("{}"),
		     "created_at",
		     created,
		     sizeof created,
		     0) ||
	    !*created)
		strcpy(created, updated);
	metadata(old,
		 mg_str("{}"),
		 "last_success",
		 last_success,
		 sizeof last_success,
		 0);
	char *json = mg_mprintf(
		"{\"schema\":1,\"name\":%m,\"country\":%m,\"country_code\":%m,"
		"\"provider\":%m,\"city\":%m,\"description\":%m,\"server\":%m,"
		"\"authentication\":%m,\"username\":%m,\"password\":%m,"
		"\"configuration\":%m,\"created_at\":%m,\"updated_at\":%m,"
		"\"last_success\":%m}",
		MG_ESC(name),
		MG_ESC(country),
		MG_ESC(code),
		MG_ESC(provider),
		MG_ESC(city),
		MG_ESC(description),
		MG_ESC(parsed->host[0]),
		MG_ESC(parsed->needs_auth
			       ? (parsed->has_cert ? "password+certificate"
						   : "password")
			       : (parsed->has_cert ? "certificate"
						   : "embedded/server-only")),
		MG_ESC(username),
		MG_ESC(password),
		MG_ESC(configuration),
		MG_ESC(created),
		MG_ESC(updated),
		MG_ESC(last_success));
	vpn_wipe(password, sizeof password);
	vpn_wipe(configuration, strlen(configuration));
	free(configuration);
	memset(parsed, 0, sizeof *parsed);
	free(parsed);
	if (previous) {
		vpn_wipe(previous, strlen(previous));
		free(previous);
	}
	char filename[48];
	snprintf(filename, sizeof filename, "%.32s.json", id);
	if (!json || strlen(json) > VPN_LIMIT ||
	    vpn_store_write(g.store, filename, json)) {
		if (json) {
			memset(json, 0, strlen(json));
			free(json);
		}
		return failure(507,
			       "VPN_CONFIGURATION_UNSUPPORTED",
			       "Atomic profile update failed");
	}
	char *public = vpn_public_profile(mg_str(json), id);
	memset(json, 0, strlen(json));
	free(json);
	char *reply = response(create ? 201 : 200, public);
	free(public);
	return reply;
invalid:
	free(previous);
	vpn_wipe(configuration, strlen(configuration));
	free(configuration);
	free(parsed);
	vpn_wipe(password, sizeof password);
	return failure(400,
		       "VPN_PROFILE_INVALID",
		       "Invalid credentials; line breaks are forbidden");
}

static int fields(struct mg_str body, const char *allowed, const char *booleans)
{
	if (body.len < 2 || body.buf[0] != '{')
		return 0;
	struct mg_str key, value;
	size_t pos = 0;
	while ((pos = mg_json_next(body, pos, &key, &value))) {
		char name[80], needle[84];
		if (key.len < 2 || key.len - 2 >= sizeof name)
			return 0;
		memcpy(name, key.buf + 1, key.len - 2);
		name[key.len - 2] = 0;
		snprintf(needle, sizeof needle, "|%s|", name);
		if (!strstr(allowed, needle))
			return 0;
		if (booleans && strstr(booleans, needle)) {
			char path[96];
			snprintf(path, sizeof path, "$.%s", name);
			bool b;
			if (!mg_json_get_bool(body, path, &b))
				return 0;
		} else if (value.len < 2 || value.buf[0] != '"')
			return 0;
	}
	return 1;
}

static char *dispatch(struct mg_str request, pid_t peer)
{
	char method[12], path[160];
	if (get(request, "method", method, sizeof method) ||
	    get(request, "path", path, sizeof path))
		return failure(400, "VPN_REQUEST_INVALID", "Invalid request");
	struct mg_str body = mg_json_get_tok(request, "$.body");
	if (!body.len)
		body = mg_str("{}");
	if (!strcmp(path, "/event")) {
		/* Only OpenVPN's direct hook child may submit network
		 * parameters. */
		char proc[64], line[1024];
		snprintf(proc, sizeof proc, "/proc/%ld/stat", (long)peer);
		FILE *f = fopen(proc, "r");
		pid_t parent = 0;
		if (f) {
			if (fgets(line, sizeof line, f)) {
				char *end = strrchr(line, ')');
				char state_char;
				if (end)
					sscanf(end + 1,
					       " %c %d",
					       &state_char,
					       &parent);
			}
			fclose(f);
		}
		if (!g.child || parent != g.child)
			return failure(403,
				       "VPN_REQUEST_INVALID",
				       "Hook process identity rejected");
		char type[16];
		if (get(body, "type", type, sizeof type))
			return failure(400,
				       "VPN_REQUEST_INVALID",
				       "Missing hook type");
		if (!strcmp(type, "up")) {
			get(body,
			    "gateway",
			    g.network.tunnel_gateway,
			    sizeof g.network.tunnel_gateway);
			get(body,
			    "address",
			    g.network.tunnel_address,
			    sizeof g.network.tunnel_address);
			g.network.dns_count = 0;
			for (int i = 0; i < 3; i++) {
				char key[16];
				snprintf(key, sizeof key, "dns%d", i);
				if (!get(body,
					 key,
					 g.network.dns[g.network.dns_count],
					 16) &&
				    *g.network.dns[g.network.dns_count])
					g.network.dns_count++;
			}
			get(body,
			    "netmask",
			    g.network.tunnel_netmask,
			    sizeof g.network.tunnel_netmask);
			get(body,
			    "peer",
			    g.network.tunnel_peer,
			    sizeof g.network.tunnel_peer);
			g.network.tunnel_mtu =
				(int)mg_json_get_long(body, "$.mtu", 1500);
			g.hook_ready = 1;
		} else if (!strcmp(type, "down")) {
			traffic_sample_now();
			vpn_traffic_pause(&g.traffic, !g.desired);
			if (!strcmp(g.state, "CONNECTED"))
				state(g.desired ? "RECONNECTING"
						: "DISCONNECTING");
			g.connected_at = 0;
			g.hook_ready = g.management_ready = 0;
			verify_invalidate();
			if (vpn_network_cleanup(&g.network, 0, g.runtime))
				error("VPN_ROUTING_ERROR",
				      "VPN down cleanup requires local "
				      "recovery");
		} else
			return failure(400,
				       "VPN_REQUEST_INVALID",
				       "Invalid hook type");
		return response(200, "{}");
	}
	if (strcmp(method, "GET")) {
		const char *allowed = "", *booleans = NULL;
		if (!strcmp(path, "/profiles") ||
		    (!strncmp(path, "/profiles/", 10) &&
		     (!strcmp(method, "PUT"))))
			allowed = "|name||country||country_code||provider||"
				  "city||description||configuration||username||"
				  "password||password_operation|";
		else if (!strcmp(path, "/settings")) {
			allowed = "|startup||reconnect||kill||dns||country_"
				  "verify||selected||"
				  "dns_server|";
			booleans = "|startup||reconnect||kill||dns||country_"
				   "verify|";
		} else if (!strcmp(path, "/connect") ||
			   !strcmp(path, "/reconnect"))
			allowed = "|profile_id|";
		if (!fields(body, allowed, booleans))
			return failure(400,
				       "VPN_REQUEST_INVALID",
				       "Unknown fields or invalid request "
				       "field types");
	}
	if (!strcmp(method, "GET")) {
		char *data = NULL;
		if (!strcmp(path, "/capabilities"))
			data = capabilities();
		else if (!strcmp(path, "/status"))
			data = status_json();
		else if (!strcmp(path, "/settings"))
			data = settings_json();
		else if (!strcmp(path, "/profiles"))
			data = profile_list();
		else if (!strcmp(path, "/logs")) {
			data = calloc(1, 4096);
			if (data) {
				size_t used = 0;
				data[used++] = '[';
				unsigned count =
					g.log_count > 16 ? 16 : g.log_count;
				for (unsigned i = 0; i < count; i++)
					used += mg_snprintf(
						data + used,
						4096 - used,
						"%s%m",
						i ? "," : "",
						MG_ESC(g.logs[(g.log_count -
							       count + i) %
							      16]));
				data[used++] = ']';
				data[used] = 0;
			}
		} else if (!strncmp(path, "/profiles/", 10) &&
			   vpn_id(path + 10)) {
			char *s = load_profile(path + 10);
			if (s) {
				data = vpn_public_profile(mg_str(s), path + 10);
				vpn_wipe(s, strlen(s));
				free(s);
			}
		}
		if (!data)
			return failure(404,
				       "VPN_REQUEST_INVALID",
				       "Resource unavailable or corrupted");
		char *r = response(200, data);
		free(data);
		return r;
	}
	if (!strcmp(path, "/profiles") && !strcmp(method, "POST"))
		return profile_save(NULL, body, 1);
	if (!strncmp(path, "/profiles/", 10)) {
		if (strlen(path) == 10 + 32 + 9 &&
		    !strcmp(path + 42, "/validate") &&
		    !strcmp(method, "POST")) {
			char id[33];
			memcpy(id, path + 10, 32);
			id[32] = 0;
			char *stored = load_profile(id);
			if (!stored)
				return failure(404,
					       "VPN_PROFILE_INVALID",
					       "Profile not found");
			char *config = mg_json_get_str(mg_str(stored),
						       "$.configuration");
			VpnProfile *parsed = calloc(1, sizeof *parsed);
			char why[192] = "Missing profile";
			int invalid = !config || !parsed ||
				      vpn_profile_parse(
					      config, parsed, why, sizeof why);
			if (config) {
				vpn_wipe(config, strlen(config));
				free(config);
			}
			if (parsed) {
				memset(parsed, 0, sizeof *parsed);
				free(parsed);
			}
			vpn_wipe(stored, strlen(stored));
			free(stored);
			return invalid ? failure(400,
						 "VPN_PROFILE_INVALID",
						 why)
				       : response(200,
						  "{\"valid\":true,\"message\":"
						  "\"Profile is structurally "
						  "compatible; OpenVPN "
						  "verifies certificates and "
						  "cryptography at connection "
						  "time.\"}");
		}
		const char *id = path + 10;
		if (!vpn_id(id))
			return failure(400,
				       "VPN_PROFILE_INVALID",
				       "Invalid profile identifier");
		if (!strcmp(method, "PUT"))
			return profile_save(id, body, 0);
		if (!strcmp(method, "DELETE")) {
			if ((g.child > 0 && !strcmp(id, g.active)) ||
			    (g.desired && !strcmp(id, g.settings.selected)))
				return failure(409,
					       "VPN_PROFILE_ACTIVE",
					       "Disconnect before deleting the "
					       "selected active profile");
			char filename[48];
			snprintf(filename, sizeof filename, "%.32s.json", id);
			if (vpn_store_delete(g.store, filename))
				return failure(507,
					       "VPN_CONFIGURATION_UNSUPPORTED",
					       "Cannot delete profile");
			if (!strcmp(id, g.settings.selected)) {
				g.settings.selected[0] = 0;
				g.settings.startup = 0;
				save_settings();
			}
			return response(200, "{}");
		}
	}
	if (!strcmp(path, "/settings") && !strcmp(method, "PUT")) {
		if (g.settings_corrupt)
			return failure(409,
				       "VPN_CONFIGURATION_UNSUPPORTED",
				       "Stored settings schema is invalid; the "
				       "original was retained");
		VpnSettings old = g.settings, next = old;
		bool b;
		if (mg_json_get_bool(body, "$.startup", &b))
			next.startup = b;
		if (mg_json_get_bool(body, "$.reconnect", &b))
			next.reconnect = b;
		if (mg_json_get_bool(body, "$.kill", &b))
			next.kill = b;
		if (mg_json_get_bool(body, "$.dns", &b))
			next.dns = b;
		if (mg_json_get_bool(body, "$.country_verify", &b))
			next.country_verify = b;
		if (mg_json_get_tok(body, "$.selected").len &&
		    get(body, "selected", next.selected, sizeof next.selected))
			return failure(400,
				       "VPN_PROFILE_INVALID",
				       "Invalid selected profile");
		if (mg_json_get_tok(body, "$.dns_server").len &&
		    get(body,
			"dns_server",
			next.dns_server,
			sizeof next.dns_server))
			return failure(
				400, "VPN_DNS_ERROR", "Invalid DNS server");
		if (*next.dns_server) {
			struct in_addr ip;
			if (inet_pton(AF_INET, next.dns_server, &ip) != 1)
				return failure(
					400,
					"VPN_DNS_ERROR",
					"DNS must be a literal IPv4 address");
		}
		if (*next.selected) {
			char *s = load_profile(next.selected);
			if (!s)
				return failure(400,
					       "VPN_PROFILE_INVALID",
					       "Selected profile is missing");
			vpn_wipe(s, strlen(s));
			free(s);
		}
		if (next.startup && !*next.selected)
			return failure(400,
				       "VPN_PROFILE_INVALID",
				       "Choose a startup profile first");
		if (next.kill &&
		    (!next.dns || !g.available || !vpn_firewall_capability(0)))
			return failure(409,
				       "VPN_FIREWALL_ERROR",
				       "Kill switch requires usable IPv4/IPv6 "
				       "firewall protection and VPN DNS");
		if (g.desired &&
		    (next.kill != old.kill || next.dns != old.dns ||
		     strcmp(next.dns_server, old.dns_server)))
			return failure(409,
				       "VPN_PROFILE_ACTIVE",
				       "Disconnect before changing protection "
				       "or DNS policy");
		g.settings = next;
		if (save_settings()) {
			g.settings = old;
			return failure(507,
				       "VPN_CONFIGURATION_UNSUPPORTED",
				       "Cannot commit settings");
		}
		if (!next.country_verify) {
			if (g.verify && g.verify_country) {
				g.verify->is_closing = 1;
				g.verify = NULL;
			}
			location_clear();
		} else if (!old.country_verify)
			verify_location_schedule();
		if (g.desired && strcmp(old.selected, next.selected)) {
			vpn_traffic_begin(&g.traffic,
					  next.selected,
					  "profile_switch",
					  mg_millis(),
					  (int64_t)time(NULL));
			g.restart_requested = 1;
			state("SWITCHING");
			stop_child();
			g.retry_at = 0;
			g.auth_failed = 0;
		}
		char *data = settings_json(), *r = response(200, data);
		free(data);
		return r;
	}
	if (!strcmp(method, "POST") && !strcmp(path, "/disconnect")) {
		if (g.recovery_invalid)
			return failure(
				500,
				"VPN_ROUTING_ERROR",
				"The network journal is invalid; use the "
				"documented local recovery procedure");
		g.desired = 0;
		g.restart_requested = 0;
		g.auth_failed = 0;
		g.retry_at = 0;
		session(0);
		state("DISCONNECTING");
		stop_child();
		if (!g.child) {
			if (vpn_network_cleanup(&g.network, 1, g.runtime)) {
				error("VPN_ROUTING_ERROR",
				      "Local recovery is required to finish "
				      "network cleanup");
				return failure(
					500, "VPN_ROUTING_ERROR", g.error);
			}
			state(g.available ? "DISCONNECTED" : "UNAVAILABLE");
		}
		return response(202, "{}");
	}
	if (!strcmp(method, "POST") &&
	    (!strcmp(path, "/connect") || !strcmp(path, "/reconnect"))) {
		if (!g.available)
			return failure(503, "VPN_NOT_AVAILABLE", g.error);
		char selected[33];
		if (mg_json_get_tok(body, "$.profile_id").len) {
			if (get(body,
				"profile_id",
				selected,
				sizeof selected) ||
			    !vpn_id(selected))
				return failure(400,
					       "VPN_PROFILE_INVALID",
					       "Invalid selected profile");
		} else
			strcpy(selected, g.settings.selected);
		char *s = load_profile(selected);
		if (!s)
			return failure(400,
				       "VPN_PROFILE_INVALID",
				       "Select a configured profile");
		char *configuration =
			     mg_json_get_str(mg_str(s), "$.configuration"),
		     *username = mg_json_get_str(mg_str(s), "$.username"),
		     *password = mg_json_get_str(mg_str(s), "$.password");
		VpnProfile *preview = calloc(1, sizeof *preview);
		char why[192];
		int invalid = !configuration || !preview ||
			      vpn_profile_parse(
				      configuration, preview, why, sizeof why);
		int missing =
			!invalid && preview->needs_auth &&
			(!username || !*username || !password || !*password);
		if (configuration) {
			vpn_wipe(configuration, strlen(configuration));
			free(configuration);
		}
		if (password) {
			vpn_wipe(password, strlen(password));
			free(password);
		}
		free(username);
		if (preview) {
			memset(preview, 0, sizeof *preview);
			free(preview);
		}
		vpn_wipe(s, strlen(s));
		free(s);
		if (invalid)
			return failure(400,
				       "VPN_PROFILE_INVALID",
				       "Stored profile is invalid");
		if (missing)
			return failure(400,
				       "VPN_CREDENTIALS_MISSING",
				       "Provision OpenVPN service credentials "
				       "before connecting");
		int same = g.desired && !strcmp(g.settings.selected, selected);
		strcpy(g.settings.selected, selected);
		if (save_settings() || session(1))
			return failure(507,
				       "VPN_CONFIGURATION_UNSUPPORTED",
				       "Cannot persist requested state");
		g.desired = 1;
		g.auth_failed = 0;
		g.retry_at = 0;
		if (!same || !strcmp(path, "/reconnect")) {
			vpn_traffic_begin(&g.traffic,
					  selected,
					  !strcmp(path, "/reconnect")
						  ? "explicit_reconnect"
					  : same	? "explicit_connect"
					  : g.child > 0 ? "profile_switch"
							: "explicit_connect",
					  mg_millis(),
					  (int64_t)time(NULL));
			g.restart_requested = 1;
			state(g.child > 0 ? "SWITCHING" : "CONNECTING");
			stop_child();
			g.retry_at = 0;
			g.retries = 0;
		}
		return response(202, "{}");
	}
	if (!strcmp(path, "/capabilities/refresh") && !strcmp(method, "POST")) {
		if (g.settings_corrupt || g.recovery_invalid)
			return failure(
				409,
				"VPN_CONFIGURATION_UNSUPPORTED",
				"Repair the retained invalid configuration or "
				"recovery journal first");
		if (g.desired || g.child || g.resolving)
			return failure(
				409,
				"VPN_PROFILE_ACTIVE",
				"Disconnect before probing capabilities");
		detect();
		if (!g.available)
			state("UNAVAILABLE");
		char *data = capabilities(), *r = response(200, data);
		free(data);
		return r;
	}
	if (!strcmp(path, "/test") && !strcmp(method, "POST")) {
		verify_public();
		return response(202, "{}");
	}
	return failure(405, "VPN_REQUEST_INVALID", "Unsupported VPN operation");
}

static int hook(const char *socket)
{
	char *type = getenv("script_type"), *address = getenv("ifconfig_local"),
	     *gateway = getenv("route_vpn_gateway");
	if (!type || !getenv("dev") || strcmp(getenv("dev"), VPN_INTERFACE) ||
	    (!strcmp(type, "up") && (!address || !gateway)))
		return 1;
	char dns[3][16] = {{0}};
	int count = 0;
	for (int i = 1; i <= 64 && count < 3; i++) {
		char key[32];
		snprintf(key, sizeof key, "foreign_option_%d", i);
		const char *option = getenv(key);
		if (!option)
			continue;
		char ip[16], extra;
		struct in_addr a;
		if (sscanf(option, "dhcp-option DNS %15s %c", ip, &extra) ==
			    1 &&
		    inet_pton(AF_INET, ip, &a) == 1) {
			int duplicate = 0;
			for (int j = 0; j < count; j++)
				if (!strcmp(dns[j], ip))
					duplicate = 1;
			if (!duplicate)
				strcpy(dns[count++], ip);
		}
	}
	char *request = mg_mprintf(
		"{\"method\":\"POST\",\"path\":\"/"
		"event\",\"body\":{\"type\":%m,\"address\":%m,\"gateway\":%m,"
		"\"netmask\":%m,\"peer\":%m,\"mtu\":%d,\"dns0\":%m,\"dns1\":%m,"
		"\"dns2\":%m}}",
		MG_ESC(type),
		MG_ESC(address ? address : ""),
		MG_ESC(gateway ? gateway : ""),
		MG_ESC(getenv("ifconfig_netmask") ? getenv("ifconfig_netmask")
						  : ""),
		MG_ESC(getenv("ifconfig_remote") ? getenv("ifconfig_remote")
						 : ""),
		atoi(getenv("tun_mtu") ? getenv("tun_mtu") : "1500"),
		MG_ESC(dns[0]),
		MG_ESC(dns[1]),
		MG_ESC(dns[2]));
	char *reply = request ? vpn_rpc(socket, request) : NULL;
	free(request);
	int ok = reply &&
		 mg_json_get_long(mg_str(reply), "$.status", 500) == 200;
	free(reply);
	return ok ? 0 : 1;
}

int main(int argc, char **argv)
{
	if (argc >= 3 && !strcmp(argv[1], "--hook"))
		return hook(argv[2]);
	int recover = 0, cap_only = 0;
	const char *control = NULL;
	const char *config = "/mnt/storage/standalonebase/vpn",
		   *runtime = "/tmp/standalonebase/vpn";
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--config") && i + 1 < argc)
			config = argv[++i];
		else if (!strcmp(argv[i], "--runtime") && i + 1 < argc)
			runtime = argv[++i];
		else if (!strcmp(argv[i], "--version")) {
			puts("Standalone VPN 1.0");
			return 0;
		} else if (!strcmp(argv[i], "--ctl") && i + 1 < argc) {
			control = argv[++i];
		} else if (!strcmp(argv[i], "--recover")) {
			recover = 1;
		} else if (!strcmp(argv[i], "--capabilities")) {
			cap_only = 1;
		} else {
			fprintf(stderr, "Unknown sbvpn argument\n");
			return 2;
		}
	}
	if (strlen(config) >= sizeof g.root || strlen(runtime) >= 80 ||
	    strchr(runtime, ' ') || strchr(config, ' '))
		return 2;
	umask(0077);
	struct rlimit core = {0, 0};
	if (setrlimit(RLIMIT_CORE, &core))
		return 1;
	strcpy(g.root, config);
	strcpy(g.runtime, runtime);
	snprintf(g.socket, sizeof g.socket, "%s/control.sock", runtime);
	if (control) {
		const char *path = !strcmp(control, "status") ? "/status"
				   : !strcmp(control, "disconnect")
					   ? "/disconnect"
					   : NULL;
		if (!path)
			return 2;
		char *request = mg_mprintf(
			"{\"method\":%m,\"path\":%m}",
			MG_ESC(!strcmp(control, "status") ? "GET" : "POST"),
			MG_ESC(path));
		char *reply = vpn_rpc(g.socket, request);
		free(request);
		if (!reply)
			return 1;
		puts(reply);
		int code =
			(int)mg_json_get_long(mg_str(reply), "$.status", 500);
		free(reply);
		return code < 400 ? 0 : 1;
	}
	if (cap_only) {
		g.settings.dns = 1;
		detect();
		char *data = capabilities();
		if (!data)
			return 1;
		puts(data);
		free(data);
		return g.available ? 0 : 1;
	}
	if (recover) {
		char *reply = vpn_rpc(
			g.socket,
			"{\"method\":\"POST\",\"path\":\"/disconnect\"}");
		if (reply) {
			int code = (int)mg_json_get_long(
				mg_str(reply), "$.status", 500);
			free(reply);
			if (code >= 400)
				return 1;
			uint64_t until = mg_millis() + 12000;
			while (mg_millis() < until) {
				reply = vpn_rpc(g.socket,
						"{\"method\":\"GET\",\"path\":"
						"\"/status\"}");
				if (reply) {
					struct mg_str data = mg_json_get_tok(
						mg_str(reply), "$.data");
					bool enabled = true, guard = true,
					     dns = true;
					mg_json_get_bool(
						data, "$.enabled", &enabled);
					mg_json_get_bool(data,
							 "$.kill_switch_active",
							 &guard);
					mg_json_get_bool(
						data, "$.vpn_dns_active", &dns);
					char status[32] = "";
					get(data,
					    "state",
					    status,
					    sizeof status);
					int done =
						!enabled && !guard && !dns &&
						strcmp(status, "DISCONNECTING");
					free(reply);
					if (done) {
						reply = vpn_rpc(
							g.socket,
							"{\"method\":\"PUT\","
							"\"path\":\"/"
							"settings\",\"body\":{"
							"\"startup\":false}}");
						if (!reply)
							return 1;
						int status =
							(int)mg_json_get_long(
								mg_str(reply),
								"$.status",
								500);
						free(reply);
						return status < 400 ? 0 : 1;
					}
				}
				usleep(50000);
			}
			return 1;
		}
	}
	ssize_t self = readlink("/proc/self/exe", g.self, sizeof g.self - 1);
	if (self <= 0)
		return 1;
	g.self[self] = 0;
	g.store = vpn_store_open(config);
	g.run = vpn_store_open(runtime);
	if (g.store < 0 || g.run < 0) {
		fprintf(stderr,
			"VPN private configuration/runtime directories "
			"unavailable\n");
		return 1;
	}
	g.lock = openat(g.run,
			"manager.lock",
			O_WRONLY | O_CREAT | O_CLOEXEC | O_NOFOLLOW,
			0600);
	if (g.lock < 0 || flock(g.lock, LOCK_EX | LOCK_NB))
		return 1;
	g.management = g.resolver_fd = g.output = -1;
	if (vpn_process_recover(g.run)) {
		fprintf(stderr,
			"VPN process identity recovery failed; original "
			"retained\n");
		return 1;
	}
	vpn_store_delete(g.run, "credentials");
	vpn_store_delete(g.run, "client.ovpn");
	if (recover) {
		VpnNetwork n;
		memset(&n, 0, sizeof n);
		int failed = vpn_network_recover(&n, runtime) ||
			     vpn_network_cleanup(&n, 1, runtime);
		if (!failed) {
			session(0);
			if (!load_settings()) {
				g.settings.startup = 0;
				if (save_settings())
					failed = 1;
			}
		}
		return failed ? 1 : 0;
	}
	mg_mgr_init(&g.mgr);
	signal(SIGTERM, signal_stop);
	signal(SIGINT, signal_stop);
	signal(SIGPIPE, SIG_IGN);
	state("DISABLED");
	int corrupt = load_settings();
	g.settings_corrupt = corrupt != 0;
	detect();
	if (corrupt) {
		g.available = 0;
		error("VPN_CONFIGURATION_UNSUPPORTED",
		      "Stored settings are corrupt or from an unsupported "
		      "schema; original retained");
	}
	if (vpn_network_recover(&g.network, runtime)) {
		g.recovery_invalid = 1;
		g.available = 0;
		error("VPN_ROUTING_ERROR",
		      "Owned network recovery journal is invalid; original "
		      "retained");
	} else if (vpn_network_cleanup(&g.network, 0, runtime)) {
		g.available = 0;
		error("VPN_ROUTING_ERROR",
		      "Cannot recover owned network state");
	}
	char *previous = vpn_store_read(g.run, "session.json", 1024);
	bool enabled = false;
	struct stat session_stat;
	int has_session = !fstatat(
		g.run, "session.json", &session_stat, AT_SYMLINK_NOFOLLOW);
	int session_valid =
		previous && vpn_json_valid(previous, strlen(previous)) &&
		mg_json_get_long(mg_str(previous), "$.schema", 0) == 1 &&
		mg_json_get_bool(mg_str(previous), "$.enabled", &enabled);
	int session_corrupt = has_session && !session_valid;
	g.desired = session_valid ? enabled : g.settings.startup;
	if (g.desired)
		vpn_traffic_begin(&g.traffic,
				  g.settings.selected,
				  "manager_restart",
				  mg_millis(),
				  (int64_t)time(NULL));
	if (session_corrupt) {
		g.available = 0;
		g.desired = g.network.guard4 != 0 || g.network.guard6 != 0;
		error("VPN_CONFIGURATION_UNSUPPORTED",
		      "Requested-state journal is invalid; existing protection "
		      "was retained. Explicit Disconnect is required.");
	}
	free(previous);
	if (!g.desired && !session_corrupt && !g.recovery_invalid)
		vpn_network_cleanup(&g.network, 1, runtime);
	if (!g.available)
		state("UNAVAILABLE");
	if (!session_corrupt)
		session(g.desired);

	g.listener = vpn_socket(AF_UNIX, SOCK_SEQPACKET, 0, 1);
	if (g.listener < 0)
		return 1;
	struct sockaddr_un sa;
	memset(&sa, 0, sizeof sa);
	sa.sun_family = AF_UNIX;
	if (strlen(g.socket) >= sizeof sa.sun_path)
		return 1;
	strcpy(sa.sun_path, g.socket);
	unlink(g.socket);
	if (bind(g.listener, (struct sockaddr *)&sa, sizeof sa) ||
	    chmod(g.socket, 0600) || listen(g.listener, 8))
		return 1;
	char *buffer = malloc(VPN_LIMIT + 1);
	if (!buffer)
		return 1;

	struct {
		int fd;
		uint64_t deadline;
	} clients[8];

	for (int i = 0; i < 8; i++)
		clients[i].fd = -1;
	while (!quitting) {
		struct pollfd ready[12];
		unsigned ready_count = 0;
		ready[ready_count++] = (struct pollfd){g.listener, POLLIN, 0};
		if (g.management >= 0)
			ready[ready_count++] =
				(struct pollfd){g.management, POLLIN, 0};
		if (g.resolver_fd >= 0)
			ready[ready_count++] =
				(struct pollfd){g.resolver_fd, POLLIN, 0};
		for (int j = 0; j < 8; j++)
			if (clients[j].fd >= 0)
				ready[ready_count++] = (struct pollfd){
					clients[j].fd, POLLIN, 0};
		poll(ready,
		     ready_count,
		     g.verify				   ? 20
		     : g.desired || g.child || g.resolving ? 100
							   : 500);
		mg_mgr_poll(&g.mgr, 0);
		int fd = accept(g.listener, NULL, NULL);
		if (fd >= 0) {
			fcntl(fd, F_SETFL, O_NONBLOCK);
			fcntl(fd, F_SETFD, FD_CLOEXEC);
			struct ucred cred;
			socklen_t length = sizeof cred;
			if (getsockopt(fd,
				       SOL_SOCKET,
				       SO_PEERCRED,
				       &cred,
				       &length) ||
			    cred.uid != geteuid()) {
				close(fd);
			} else {
				int slot = 0;
				while (slot < 8 && clients[slot].fd >= 0)
					slot++;
				if (slot == 8)
					close(fd);
				else {
					clients[slot].fd = fd;
					clients[slot].deadline =
						mg_millis() + 3000;
				}
			}
		}
		for (int i = 0; i < 8; i++)
			if (clients[i].fd >= 0) {
				fd = clients[i].fd;
				ssize_t n =
					recv(fd, buffer, VPN_LIMIT, MSG_TRUNC);
				if (n > 0) {
					char *reply = NULL;
					struct ucred peer;
					socklen_t size = sizeof peer;
					if (n > (ssize_t)VPN_LIMIT ||
					    !vpn_json_valid(buffer, (size_t)n))
						reply = failure(
							400,
							"VPN_REQUEST_INVALID",
							"Malformed or "
							"oversized request");
					else if (getsockopt(fd,
							    SOL_SOCKET,
							    SO_PEERCRED,
							    &peer,
							    &size))
						reply = failure(
							403,
							"VPN_REQUEST_INVALID",
							"Peer unavailable");
					else {
						buffer[n] = 0;
						reply = dispatch(
							mg_str_n(buffer,
								 (size_t)n),
							peer.pid);
					}
					if (reply) {
						send(fd,
						     reply,
						     strlen(reply),
						     MSG_NOSIGNAL);
						memset(reply, 0, strlen(reply));
						free(reply);
					}
					memset(buffer, 0, VPN_LIMIT + 1);
					close(fd);
					clients[i].fd = -1;
				} else if (!n ||
					   mg_millis() > clients[i].deadline) {
					close(fd);
					clients[i].fd = -1;
				}
			}
		tick();
	}
	stop_child();
	uint64_t until = mg_millis() + 9000;
	while (g.child > 0 && mg_millis() < until) {
		int status;
		if (waitpid(g.child, &status, WNOHANG) == g.child) {
			g.child = 0;
			break;
		}
		if (mg_millis() + 1000 > until)
			kill(g.child, SIGKILL);
		usleep(10000);
	}
	if (g.child > 0) {
		kill(g.child, SIGKILL);
		waitpid(g.child, NULL, 0);
	}
	if (!g.recovery_invalid)
		vpn_network_cleanup(
			&g.network, !(g.desired && g.settings.kill), runtime);
	vpn_store_delete(g.run, "credentials");
	vpn_store_delete(g.run, "client.ovpn");
	vpn_store_delete(g.run, "process.json");
	unlink(g.socket);
	free(buffer);
	mg_mgr_free(&g.mgr);
	close(g.listener);
	close(g.run);
	close(g.store);
	return 0;
}
