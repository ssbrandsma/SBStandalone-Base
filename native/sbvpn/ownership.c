/* Crash recovery never signals a PID unless start ticks and executable match.
 */
#include "vpn.h"
#include "../common/process_identity.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static unsigned long long start_ticks(pid_t pid)
{
	identity process;
	if (!process_stat(pid, &process) || process.state == 'Z' ||
	    process.state == 'X')
		return 0;
	return process.start;
}

int vpn_process_record(int dir, pid_t pid, const char *executable)
{
	struct stat st;
	unsigned long long ticks = start_ticks(pid);
	if (!ticks || stat(executable, &st))
		return -1;
	char *json = mg_mprintf("{\"schema\":1,\"pid\":%ld,\"start\":%llu,"
				"\"device\":%llu,\"inode\":%llu}",
				(long)pid,
				ticks,
				(unsigned long long)st.st_dev,
				(unsigned long long)st.st_ino);
	int r = json ? vpn_store_write(dir, "process.json", json) : -1;
	free(json);
	return r;
}

int vpn_process_recover(int dir)
{
	char *json = vpn_store_read(dir, "process.json", 1024);
	if (!json)
		return 0;
	struct mg_str j = mg_str(json);
	if (!vpn_json_valid(json, strlen(json)) ||
	    mg_json_get_long(j, "$.schema", 0) != 1) {
		free(json);
		return -1;
	}
	long value = mg_json_get_long(j, "$.pid", 0);
	unsigned long long ticks = (unsigned long long)mg_json_get_num(
				   j, "$.start", 0),
			   device = (unsigned long long)mg_json_get_num(
				   j, "$.device", 0),
			   inode = (unsigned long long)mg_json_get_num(
				   j, "$.inode", 0);
	free(json);
	if (value < 2 || value > 2147483647 || !ticks)
		return -1;
	pid_t pid = (pid_t)value;
	if (start_ticks(pid) != ticks)
		return vpn_store_delete(dir, "process.json");
	char path[64];
	snprintf(path, sizeof path, "/proc/%ld/exe", value);
	struct stat st;
	if (stat(path, &st) || device != (unsigned long long)st.st_dev ||
	    inode != (unsigned long long)st.st_ino)
		return -1;
	if (kill(pid, SIGTERM))
		return -1;
	uint64_t end = mg_millis() + 8000;
	while (mg_millis() < end) {
		if (start_ticks(pid) != ticks)
			return vpn_store_delete(dir, "process.json");
		usleep(20000);
	}
	if (start_ticks(pid) == ticks) {
		if (stat(path, &st) ||
		    device != (unsigned long long)st.st_dev ||
		    inode != (unsigned long long)st.st_ino)
			return -1;
		kill(pid, SIGKILL);
	}
	return vpn_store_delete(dir, "process.json");
}
