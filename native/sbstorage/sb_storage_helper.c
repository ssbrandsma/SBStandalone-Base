/*
 * Native storage inspection, validation and preparation utilities.
 *
 * Supplies bounded storage operations and status to the Lua storage manager
 * and storage setup/boot scripts. Complements sbwebserver's storage controls
 * and the offline updater planner; it is a command-line helper, not a daemon.
 */

#define _POSIX_C_SOURCE 200809L
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/sysmacros.h>
#include <sys/utsname.h>
#include <unistd.h>

#define TARGET_BYTES (64ULL * 1024ULL * 1024ULL)
#define RESERVE_LEBS 79
#define MAGIC 0x06101831U
#define SB_NODE 6
#define BOOT_HELPER "/mnt/storage/standalonebase/sbdata-boot.sh"
#define RCS_LOCAL "/etc/init.d/rcS.local"
#define APPLETS "/usr/share/jive/applets"
#define MKFS_TEMP "/tmp/sb-mkfs-test/mkfs.ubifs"
#define MKFS_PERSISTENT "/mnt/storage/standalonebase/mkfs.ubifs"
#define MKFS_SHA256                                                            \
	"f1ec2fb29c0a9e40a300283c191092905e0d6da40fcef32184dbfb5677653af3"
#define MKFS_VERSION "1.3"
#define REQUIRED_MEMORY_KB 8192
#define REQUIRED_TMP_BYTES 3145728ULL

typedef struct {
	char model[128], arch[64], kernel[128], firmware[128], mountpoint[128],
		fs_type[32], volume[64], volume_type[32], volumes[512],
		sbdata_type[32], formatter_path[256], formatter_checksum[65],
		formatter_version[16], formatter_reason[64];
	long ubi_devices, total_pebs, avail_pebs, reserved_pebs, bad_pebs,
		flash_bytes, leb_size, min_io_size, volume_lebs, corrupted,
		read_only, sbdata_id, sbdata_lebs, sbdata_leb_size,
		sbdata_corrupted, memory_available_kb;
	unsigned long long fs_total, fs_free, sbdata_total, sbdata_free,
		tmp_available_bytes;
	int ambiguous, mounted, unexpected, forbidden, bad_id, damaged,
		sbdata_present, sbdata_mounted, bind_mounted, applet_dir,
		standard_applets, standalone_base, boot_helper, rcs_local,
		rcs_integrated, ubi_node, sbdata_node, ubifs_supported,
		ubimkvol_tool, mkfs_tool, temp_space, formatter_temporary,
		formatter_valid;
} info_t;

#include "../common/sha256.h"

/**
 * @brief Handle sha file.
 * @param n Capacity or item count associated with the operation.
 * @param out Destination buffer for the result.
 * @return Zero on success; otherwise a nonzero status or error code.
 */
static int sha_file(const char *n, char out[65])
{
	unsigned char b[4096];
	sha256_t c;
	ssize_t z;
	int fd = open(n, O_RDONLY);
	if (fd < 0)
		return -1;
	sha_init(&c);
	while ((z = read(fd, b, sizeof b)) > 0)
		sha_add(&c, b, (size_t)z);
	close(fd);
	if (z < 0)
		return -1;
	sha_done(&c, out);
	return 0;
}

/**
 * @brief Get the active configuration storage root.
 * @return Pointer to the requested object, or `NULL` when unavailable.
 */
static const char *root(void)
{
	const char *p = getenv("SB_STORAGE_ROOT");
	return p && *p ? p : "";
}

/**
 * @brief Build a path below the active configuration root.
 * @param out Destination buffer for the result.
 * @param n Capacity or item count associated with the operation.
 * @param p Input text for p.
 */
static void path(char *out, size_t n, const char *p)
{
	snprintf(out, n, "%s%s", root(), p);
}

/**
 * @brief Read text.
 * @param n Capacity or item count associated with the operation.
 * @param out Destination buffer for the result.
 * @param z Size or count for z.
 * @return Zero on success; otherwise a nonzero status or error code.
 */
static int read_text(const char *n, char *out, size_t z)
{
	char p[PATH_MAX];
	FILE *f;
	size_t q;
	path(p, sizeof p, n);
	if (!(f = fopen(p, "r")))
		return -1;
	q = fread(out, 1, z - 1, f);
	if (ferror(f) || memchr(out, 0, q) || (q == z - 1 && fgetc(f) != EOF)) {
		fclose(f);
		out[0] = 0;
		return -1;
	}
	fclose(f);
	out[q] = 0;
	while (q && strchr("\r\n \t", out[q - 1]))
		out[--q] = 0;
	return 0;
}

/* SqueezeOS appends builder and source revision lines to this file. Only
 * its bounded first line is the firmware identity used by the existing API. */
static int read_first_line(const char *name, char *out, size_t size)
{
	char p[PATH_MAX];
	FILE *f;
	size_t length = 0;
	int ch;
	if (size < 2) return -1;
	path(p, sizeof p, name);
	f = fopen(p, "r");
	if (!f) return -1;
	while ((ch = fgetc(f)) != EOF && ch != '\n') {
		if (!ch || length + 1 >= size) {
			fclose(f); out[0] = 0; return -1;
		}
		out[length++] = (char)ch;
	}
	if (ferror(f)) { fclose(f); out[0] = 0; return -1; }
	fclose(f);
	out[length] = 0;
	while (length && strchr("\r\n \t", out[length-1])) out[--length] = 0;
	return 0;
}

/**
 * @brief Read long.
 * @param n Capacity or item count associated with the operation.
 * @param d Numeric value for d.
 * @return Value produced by the operation.
 */
static long read_long(const char *n, long d)
{
	char b[64], *e;
	long v;
	if (read_text(n, b, sizeof b))
		return d;
	errno = 0;
	v = strtol(b, &e, 10);
	return errno || e == b || *e || v < 0 ? d : v;
}

#include "dynamic_layout.h"

/**
 * @brief Handle exists.
 * @param n Capacity or item count associated with the operation.
 * @return Zero on success; otherwise a nonzero status or error code.
 */
static int exists(const char *n)
{
	char p[PATH_MAX];
	struct stat s;
	path(p, sizeof p, n);
	return !stat(p, &s);
}

/**
 * @brief Handle directory.
 * @param n Capacity or item count associated with the operation.
 * @return Zero on success; otherwise a nonzero status or error code.
 */
static int directory(const char *n)
{
	char p[PATH_MAX];
	struct stat s;
	path(p, sizeof p, n);
	return !stat(p, &s) && S_ISDIR(s.st_mode);
}

/**
 * @brief Handle contains.
 * @param n Capacity or item count associated with the operation.
 * @param x Input text for x.
 * @return Zero on success; otherwise a nonzero status or error code.
 */
static int contains(const char *n, const char *x)
{
	char b[8192];
	return !read_text(n, b, sizeof b) && strstr(b, x);
}

static uint32_t le32(const unsigned char *p);

/**
 * @brief Handle mem value.
 * @param name Name of the item to process.
 * @return Value produced by the operation.
 */
static long mem_value(const char *name)
{
	char b[4096], *p;
	if (read_text("/proc/meminfo", b, sizeof b) ||
	    (p = strstr(b, name)) == NULL)
		return -1;
	p += strlen(name);
	while (*p == ' ' || *p == '\t' || *p == ':')
		p++;
	return strtol(p, NULL, 10);
}

/**
 * @brief Handle resources.
 * @param i Object referenced by i.
 */
static void resources(info_t *i)
{
	struct statvfs s;
	char p[PATH_MAX];
	long free = mem_value("MemFree"), buffers = mem_value("Buffers"),
	     cached = mem_value("Cached"), reclaim = mem_value("SReclaimable");
	if (*root()) {
		i->memory_available_kb = read_long(
			"/sys/class/ubi/ubi0/test_memory_available_kb", -1);
		i->tmp_available_bytes = (unsigned long long)read_long(
			"/sys/class/ubi/ubi0/test_tmp_available_bytes", -1);
		return;
	}
	i->memory_available_kb =
		(free < 0 || buffers < 0 || cached < 0)
			? -1
			: free + buffers + cached + (reclaim > 0 ? reclaim : 0);
	path(p, sizeof p, "/tmp");
	if (!statvfs(p, &s) && s.f_blocks)
		i->tmp_available_bytes =
			(unsigned long long)s.f_bavail * s.f_frsize;
	else if (i->memory_available_kb > 0)
		i->tmp_available_bytes =
			(unsigned long long)i->memory_available_kb * 1024;
}

/**
 * @brief Handle elf arm static.
 * @param n Capacity or item count associated with the operation.
 * @return Zero on success; otherwise a nonzero status or error code.
 */
static int elf_arm_static(const char *n)
{
	unsigned char h[52], ph[32];
	int fd = open(n, O_RDONLY), i, num, off;
	if (fd < 0)
		return 0;
	if (read(fd, h, sizeof h) != (ssize_t)sizeof h ||
	    memcmp(h, "\177ELF", 4) || h[4] != 1 || h[5] != 1 || h[18] != 40 ||
	    h[19] != 0) {
		close(fd);
		return 0;
	}
	off = (int)le32(h + 28);
	num = h[44] | h[45] << 8;
	for (i = 0; i < num; i++) {
		if (lseek(fd, off + i * 32, SEEK_SET) < 0 ||
		    read(fd, ph, sizeof ph) != (ssize_t)sizeof ph) {
			close(fd);
			return 0;
		}
		if (le32(ph) == 3 || le32(ph) == 2) {
			close(fd);
			return 0;
		}
	}
	close(fd);
	return 1;
}

/**
 * @brief Handle formatter.
 * @param i Object referenced by i.
 */
static void formatter(info_t *i)
{
	const char *candidates[] = {
		MKFS_PERSISTENT, "/usr/sbin/mkfs.ubifs", MKFS_TEMP};
	char p[PATH_MAX], reason[64];
	struct stat s;
	int j;
	if (*root()) {
		read_text("/sys/class/ubi/ubi0/test_formatter_reason",
			  reason,
			  sizeof reason);
		read_text("/sys/class/ubi/ubi0/test_formatter_checksum",
			  i->formatter_checksum,
			  sizeof i->formatter_checksum);
		read_text("/sys/class/ubi/ubi0/test_formatter_version",
			  i->formatter_version,
			  sizeof i->formatter_version);
		read_text("/sys/class/ubi/ubi0/test_formatter_path",
			  i->formatter_path,
			  sizeof i->formatter_path);
		snprintf(i->formatter_reason,
			 sizeof i->formatter_reason,
			 "%s",
			 reason[0] ? reason : "missing_executable");
		i->formatter_valid = !strcmp(i->formatter_reason, "ok");
		i->formatter_temporary =
			strstr(i->formatter_path, "/tmp/") != NULL;
		i->mkfs_tool = i->formatter_path[0] != 0;
		return;
	}
	for (j = 0; j < 3; j++) {
		path(p, sizeof p, candidates[j]);
		if (!stat(p, &s) && S_ISREG(s.st_mode) &&
		    access(p, X_OK) == 0) {
			snprintf(i->formatter_path,
				 sizeof i->formatter_path,
				 "%s",
				 candidates[j]);
			i->formatter_temporary = j == 2;
			i->mkfs_tool = 1;
			break;
		}
	}
	if (!i->mkfs_tool) {
		strcpy(i->formatter_reason, "missing_executable");
		return;
	}
	path(p, sizeof p, i->formatter_path);
	if (!elf_arm_static(p)) {
		strcpy(i->formatter_reason, "wrong_architecture_or_linkage");
		return;
	}
	if (sha_file(p, i->formatter_checksum)) {
		strcpy(i->formatter_reason, "checksum_failed");
		return;
	}
	if (strcmp(i->formatter_checksum, MKFS_SHA256)) {
		strcpy(i->formatter_reason, "wrong_checksum");
		return;
	}
	strcpy(i->formatter_version, MKFS_VERSION);
	strcpy(i->formatter_reason, "ok");
	i->formatter_valid = 1;
}

/**
 * @brief Handle ubi count.
 * @return Value produced by the operation.
 */
static long ubi_count(void)
{
	long n = 0;
	char p[PATH_MAX], q[PATH_MAX];
	DIR *d;
	struct dirent *e;
	path(p, sizeof p, "/sys/class/ubi");
	if (!(d = opendir(p)))
		return 0;
	while ((e = readdir(d))) {
		int id;
		char tail;
		if (sscanf(e->d_name, "ubi%d%c", &id, &tail) == 1) {
			snprintf(q,
				 sizeof q,
				 "/sys/class/ubi/%.255s",
				 e->d_name);
			if (directory(q))
				n++;
		}
	}
	closedir(d);
	return n;
}

/**
 * @brief Handle flash size.
 * @return Value produced by the operation.
 */
static long flash_size(void)
{
	char p[PATH_MAX], l[256];
	FILE *f;
	unsigned long z;
	int id;
	path(p, sizeof p, "/proc/mtd");
	if (!(f = fopen(p, "r")))
		return -1;
	while (fgets(l, sizeof l, f))
		if (sscanf(l, "mtd%d: %lx", &id, &z) == 2 && id == 1) {
			fclose(f);
			return (long)z;
		}
	fclose(f);
	return -1;
}

/**
 * @brief Handle cpu.
 * @param i Object referenced by i.
 */
static void cpu(info_t *i)
{
	char b[4096], *p, *e;
	if (read_text("/proc/cpuinfo", b, sizeof b) ||
	    (p = strstr(b, "Hardware")) == NULL || (p = strchr(p, ':')) == NULL)
		return;
	for (p++; *p == ' ' || *p == '\t'; p++)
		;
	if ((e = strchr(p, '\n')))
		*e = 0;
	snprintf(i->model, sizeof i->model, "%s", p);
}

/**
 * @brief Handle expected id.
 * @param n Capacity or item count associated with the operation.
 * @param id Identifier of the requested item.
 * @return Zero on success; otherwise a nonzero status or error code.
 */
static int expected_id(const char *n, int id)
{
	if (!strcmp(n, "kernel_bak"))
		return id == 0 || id == 3;
	if (!strcmp(n, "cramfs_bak"))
		return id == 1 || id == 4;
	if (!strcmp(n, "ubifs"))
		return id == 2;
	if (!strcmp(n, "kernel"))
		return id == 0 || id == 3;
	if (!strcmp(n, "cramfs"))
		return id == 1 || id == 4;
	if (!strcmp(n, "sbdata"))
		return id >= 5;
	return 0;
}

/**
 * @brief Handle mounts.
 * @param i Object referenced by i.
 */
static void mounts(info_t *i)
{
	char p[PATH_MAX], dev[128], mnt[128], type[32], opts[256], l[640];
	FILE *f;
	path(p, sizeof p, "/proc/mounts");
	if (!(f = fopen(p, "r")))
		return;
	while (fgets(l, sizeof l, f)) {
		if (sscanf(l, "%127s %127s %31s %255s", dev, mnt, type, opts) <
		    3)
			continue;
		if (!strcmp(type, "ubifs") && !strcmp(mnt, "/mnt/storage")) {
			snprintf(
				i->mountpoint, sizeof i->mountpoint, "%s", mnt);
			snprintf(i->fs_type, sizeof i->fs_type, "%s", type);
			if (!strncmp(dev, "ubi", 3)) {
				char *q = strchr(dev, ':');
				if (q)
					snprintf(i->volume,
						 sizeof i->volume,
						 "%s",
						 q + 1);
			}
			i->mounted = 1;
		}
		int extended =
			!strcmp(type, "ubifs") || !strcmp(type, "sbubifs");
		int sbdata = !strcmp(dev, "ubi0:sbdata") ||
			     !strcmp(dev, "/dev/ubi0_5");
		if (extended && !strcmp(mnt, "/mnt/sbdata") && sbdata)
			i->sbdata_mounted = 1;
		if (!strcmp(mnt, APPLETS) &&
		    ((extended && sbdata) ||
		     (!strcmp(dev, "/mnt/sbdata/applets") &&
		      (extended || !strcmp(type, "none")))))
			i->bind_mounted = 1;
	}
	fclose(f);
}

/**
 * @brief Handle volumes.
 * @param i Object referenced by i.
 */
static void volumes(info_t *i)
{
	char full[PATH_MAX], p[PATH_MAX], name[64], item[96];
	DIR *d;
	struct dirent *e;
	int matches = 0;
	path(full, sizeof full, "/sys/class/ubi/ubi0");
	if (!(d = opendir(full)))
		return;
	while ((e = readdir(d))) {
		int id;
		if (sscanf(e->d_name, "ubi0_%d", &id) != 1)
			continue;
		snprintf(p,
			 sizeof p,
			 "/sys/class/ubi/ubi0/%.255s/name",
			 e->d_name);
		if (read_text(p, name, sizeof name))
			continue;
		snprintf(item, sizeof item, "%d:%.63s", id, name);
		if (i->volumes[0])
			strncat(i->volumes,
				",",
				sizeof i->volumes - strlen(i->volumes) - 1);
		strncat(i->volumes,
			item,
			sizeof i->volumes - strlen(i->volumes) - 1);
		if (strstr(name, "ubifs") && strcmp(name, "ubifs"))
			i->forbidden = 1;
		if (!expected_id(name, id)) {
			i->unexpected++;
			if (!strcmp(name, "kernel") ||
			    !strcmp(name, "kernel_bak") ||
			    !strcmp(name, "cramfs") ||
			    !strcmp(name, "cramfs_bak") ||
			    !strcmp(name, "ubifs") || !strcmp(name, "sbdata"))
				i->bad_id = 1;
		}
		snprintf(p,
			 sizeof p,
			 "/sys/class/ubi/ubi0/%.255s/corrupted",
			 e->d_name);
		if (read_long(p, -1) != 0)
			i->damaged = 1;
		if (!strcmp(name, i->volume[0] ? i->volume : "ubifs")) {
			matches++;
			snprintf(p,
				 sizeof p,
				 "/sys/class/ubi/ubi0/%.255s/type",
				 e->d_name);
			read_text(p, i->volume_type, sizeof i->volume_type);
			snprintf(p,
				 sizeof p,
				 "/sys/class/ubi/ubi0/%.255s/reserved_ebs",
				 e->d_name);
			i->volume_lebs = read_long(p, -1);
			snprintf(p,
				 sizeof p,
				 "/sys/class/ubi/ubi0/%.255s/usable_eb_size",
				 e->d_name);
			i->leb_size = read_long(p, -1);
			snprintf(p,
				 sizeof p,
				 "/sys/class/ubi/ubi0/%.255s/corrupted",
				 e->d_name);
			i->corrupted = read_long(p, -1);
		}
		if (!strcmp(name, "sbdata")) {
			i->sbdata_present++;
			i->sbdata_id = id;
			snprintf(p,
				 sizeof p,
				 "/sys/class/ubi/ubi0/%.255s/type",
				 e->d_name);
			read_text(p, i->sbdata_type, sizeof i->sbdata_type);
			snprintf(p,
				 sizeof p,
				 "/sys/class/ubi/ubi0/%.255s/reserved_ebs",
				 e->d_name);
			i->sbdata_lebs = read_long(p, -1);
			snprintf(p,
				 sizeof p,
				 "/sys/class/ubi/ubi0/%.255s/usable_eb_size",
				 e->d_name);
			i->sbdata_leb_size = read_long(p, -1);
			snprintf(p,
				 sizeof p,
				 "/sys/class/ubi/ubi0/%.255s/corrupted",
				 e->d_name);
			i->sbdata_corrupted = read_long(p, -1);
		}
	}
	closedir(d);
	i->ambiguous = matches != 1;
	if (i->sbdata_present == 1) {
		snprintf(p, sizeof p, "/dev/ubi0_%ld", i->sbdata_id);
		i->sbdata_node = exists(p);
	}
}

/**
 * @brief Handle filesystems.
 * @param i Object referenced by i.
 */
static void filesystems(info_t *i)
{
	struct statvfs s;
	char p[PATH_MAX];
	if (*root()) {
		i->fs_total = read_long(
			"/sys/class/ubi/ubi0/test_fs_total_bytes", -1);
		i->fs_free =
			read_long("/sys/class/ubi/ubi0/test_fs_free_bytes", -1);
		i->sbdata_total = read_long(
			"/sys/class/ubi/ubi0/test_sbdata_total_bytes", 0);
		i->sbdata_free = read_long(
			"/sys/class/ubi/ubi0/test_sbdata_free_bytes", 0);
		return;
	}
	path(p, sizeof p, "/mnt/storage");
	if (!statvfs(p, &s)) {
		i->fs_total = (unsigned long long)s.f_blocks * s.f_frsize;
		i->fs_free = (unsigned long long)s.f_bavail * s.f_frsize;
	}
	path(p, sizeof p, "/mnt/sbdata");
	if (i->sbdata_mounted && !statvfs(p, &s)) {
		i->sbdata_total = (unsigned long long)s.f_blocks * s.f_frsize;
		i->sbdata_free = (unsigned long long)s.f_bavail * s.f_frsize;
	}
}

/**
 * @brief Handle collect.
 * @param i Object referenced by i.
 */
static void collect(info_t *i)
{
	struct utsname u;
	char b[256], *nl;
	memset(i, 0, sizeof *i);
	i->ubi_devices = i->total_pebs = i->avail_pebs = i->reserved_pebs =
		i->bad_pebs = i->flash_bytes = i->leb_size = i->min_io_size =
			i->volume_lebs = i->corrupted = i->read_only =
				i->sbdata_id = i->sbdata_lebs =
					i->sbdata_leb_size =
						i->sbdata_corrupted =
							i->memory_available_kb =
								-1;
	cpu(i);
	if (!uname(&u)) {
		snprintf(i->arch, sizeof i->arch, "%.63s", u.machine);
		snprintf(i->kernel, sizeof i->kernel, "%.127s", u.release);
	}
	if (!read_text("/proc/sys/kernel/architecture", b, sizeof b))
		snprintf(i->arch, sizeof i->arch, "%.63s", b);
	if (!read_text("/proc/sys/kernel/osrelease", b, sizeof b))
		snprintf(i->kernel, sizeof i->kernel, "%.127s", b);
	read_first_line("/etc/squeezeos.version", i->firmware, sizeof i->firmware);
	if ((nl = strpbrk(i->firmware, "\r\n")))
		*nl = 0;
	mounts(i);
	i->ubi_devices = ubi_count();
	i->flash_bytes = flash_size();
	i->total_pebs = read_long("/sys/class/ubi/ubi0/total_eraseblocks", -1);
	i->avail_pebs = read_long("/sys/class/ubi/ubi0/avail_eraseblocks", -1);
	i->reserved_pebs =
		read_long("/sys/class/ubi/ubi0/reserved_for_bad", -1);
	i->bad_pebs = read_long("/sys/class/ubi/ubi0/bad_peb_count", -1);
	i->read_only = read_long("/sys/class/ubi/ubi0/read_only", -1);
	i->min_io_size = read_long("/sys/class/ubi/ubi0/min_io_size", -1);
	i->ubi_node = exists("/dev/ubi0");
	volumes(i);
	filesystems(i);
	i->applet_dir = directory(APPLETS);
	i->standard_applets = directory(APPLETS "/SetupAppletInstaller");
	i->standalone_base = directory(APPLETS "/StandaloneBase");
	i->boot_helper = exists(BOOT_HELPER) ||
			 exists("/mnt/storage/standalonebase/storage-boot.sh");
	i->rcs_local = exists(RCS_LOCAL);
	i->rcs_integrated =
		contains(RCS_LOCAL, "STANDALONEBASE-SBDATA") ||
		contains(RCS_LOCAL, "STANDALONEBASE-EXTENDED-STORAGE");
	i->ubifs_supported = contains("/proc/filesystems", "ubifs");
	i->ubimkvol_tool = exists("/usr/sbin/ubimkvol") ||
			   exists("/sbin/ubimkvol") ||
			   exists("/usr/bin/ubimkvol");
	i->temp_space = directory("/tmp");
	resources(i);
	formatter(i);
}

/* Compare whole inventory tokens: "kernel" must not match "kernel_bak". */
static int volume_named(const info_t *i, int id, const char *name)
{
	char token[96];
	snprintf(token, sizeof token, "%d:%s", id, name);
	for (const char *p = i->volumes; *p;) {
		const char *end = strchr(p, ',');
		size_t length = end ? (size_t)(end - p) : strlen(p);
		if (length == strlen(token) && !memcmp(p, token, length))
			return 1;
		if (!end) break;
		p = end + 1;
	}
	return 0;
}

/* Read-only compatibility inspection; the shell initialization frontend
 * independently rechecks exact controller capacity and every write gate. */
static int compatible(const info_t *i, char *r, size_t n)
{
	long target =
		i->leb_size > 0
			? (long)((TARGET_BYTES + i->leb_size - 1) / i->leb_size)
			: -1;
	int layout77x = volume_named(i, 0, "kernel_bak") &&
			volume_named(i, 1, "cramfs_bak") &&
			volume_named(i, 2, "ubifs") &&
			volume_named(i, 3, "kernel") &&
			volume_named(i, 4, "cramfs");
	int layout901 = volume_named(i, 0, "kernel") &&
			volume_named(i, 1, "cramfs") &&
			volume_named(i, 2, "ubifs") &&
			volume_named(i, 3, "kernel_bak") &&
			volume_named(i, 4, "cramfs_bak");
	int primary773 = !strcmp(i->firmware, "7.7.3 r16676") && layout901;
	if (!strstr(i->model, "Baby") &&
	    !strstr(i->model, "Squeezebox Radio")) {
		snprintf(r, n, "unsupported_model");
		return 0;
	}
	if (strncmp(i->arch, "arm", 3)) {
		snprintf(r, n, "unsupported_architecture");
		return 0;
	}
	if (strncmp(i->kernel, "2.6.26", 6)) {
		snprintf(r, n, "unsupported_kernel");
		return 0;
	}
	if (strcmp(i->firmware, "7.7.2 r9663") &&
	    strcmp(i->firmware, "7.7.3 r16676") &&
	    strcmp(i->firmware, "9.0.1 r17084")) {
		snprintf(r, n, "unsupported_firmware");
		return 0;
	}
	if (i->ubi_devices != 1) {
		snprintf(r, n, "unexpected_ubi_device_count");
		return 0;
	}
	if (i->forbidden) {
		snprintf(r, n, "forbidden_ubifs_like_volume");
		return 0;
	}
	if (i->sbdata_present > 1) {
		snprintf(r, n, "ambiguous_sbdata_volume");
		return 0;
	}
	if (i->bad_id) {
		snprintf(r, n, "unexpected_volume_id");
		return 0;
	}
	if (i->unexpected) {
		snprintf(r, n, "unexpected_volume_layout");
		return 0;
	}
	if (((!strcmp(i->firmware, "7.7.2 r9663") ||
	      !strcmp(i->firmware, "7.7.3 r16676")) &&
	     !layout77x && !primary773) ||
	    (!strcmp(i->firmware, "9.0.1 r17084") && !layout901 && !layout77x)) {
		snprintf(r, n, "missing_production_volume");
		return 0;
	}
	if (!i->mounted || strcmp(i->fs_type, "ubifs") ||
	    strcmp(i->volume, "ubifs")) {
		snprintf(r, n, "writable_mount_not_verified");
		return 0;
	}
	if (i->ambiguous || strcmp(i->volume_type, "dynamic")) {
		snprintf(r, n, "production_volume_invalid");
		return 0;
	}
	if (i->damaged || i->corrupted != 0) {
		snprintf(r, n, "volume_corrupted_or_unknown");
		return 0;
	}
	if (i->read_only == 1) {
		snprintf(r, n, "ubi_read_only");
		return 0;
	}
	{
        layout_t layout;
        if (!layout_validate(&layout)) {
            snprintf(r, n, "%s", layout.reason);
            return 0;
        }
    }
	if (target < 0 ||
	    (!i->sbdata_present && i->avail_pebs < target + RESERVE_LEBS)) {
		snprintf(r, n, "insufficient_capacity_with_reserve");
		return 0;
	}
	snprintf(r, n, "supported_read_only");
	return 1;
}

/**
 * @brief Handle prepare ok.
 * @param i Object referenced by i.
 * @param r Mutable text buffer for r.
 * @param n Capacity or item count associated with the operation.
 * @return Zero on success; otherwise a nonzero status or error code.
 */
static int prepare_ok(const info_t *i, char *r, size_t n)
{
	if (!compatible(i, r, n))
		return 0;
	if (i->leb_size != 129024) {
		snprintf(r, n, "invalid_leb_size");
		return 0;
	}
	if (i->min_io_size != 2048) {
		snprintf(r, n, "invalid_min_io_size");
		return 0;
	}
	if (!i->ubifs_supported) {
		snprintf(r, n, "ubifs_not_supported");
		return 0;
	}
	if (!i->ubi_node) {
		snprintf(r, n, "missing_ubi_device_node");
		return 0;
	}
	if (i->sbdata_present == 1 && !i->sbdata_node) {
		snprintf(r, n, "missing_sbdata_device_node");
		return 0;
	}
	if (!i->ubimkvol_tool) {
		snprintf(r, n, "missing_ubimkvol");
		return 0;
	}
	if (!i->mkfs_tool) {
		snprintf(r, n, "missing_compatible_mkfs_ubifs");
		return 0;
	}
	if (!i->formatter_valid) {
		snprintf(r, n, "formatter_%s", i->formatter_reason);
		return 0;
	}
	if (i->memory_available_kb < REQUIRED_MEMORY_KB) {
		snprintf(r, n, "insufficient_free_ram");
		return 0;
	}
	if (i->tmp_available_bytes < REQUIRED_TMP_BYTES) {
		snprintf(r, n, "insufficient_temporary_capacity");
		return 0;
	}
	if (!i->temp_space) {
		snprintf(r, n, "missing_temporary_workspace");
		return 0;
	}
	if (!i->applet_dir || !i->standard_applets) {
		snprintf(r, n, "required_applets_missing");
		return 0;
	}
	if (i->rcs_local && !i->rcs_integrated) {
		snprintf(r, n, "existing_rcs_local_requires_review");
		return 0;
	}
	if (i->sbdata_present == 1 && strcmp(i->sbdata_type, "dynamic")) {
		snprintf(r, n, "sbdata_not_dynamic");
		return 0;
	}
	snprintf(r, n, "migration_and_mount_safety_pending");
	return 0;
}

/**
 * @brief Handle state.
 * @param i Object referenced by i.
 * @return Pointer to the requested object, or `NULL` when unavailable.
 */
static const char *state(const info_t *i)
{
	if (i->sbdata_present == 1 && i->sbdata_mounted && i->bind_mounted &&
	    i->boot_helper && i->rcs_integrated && i->standard_applets &&
	    i->standalone_base)
		return "complete";
	if (i->sbdata_present || i->sbdata_mounted || i->bind_mounted ||
	    i->boot_helper || i->rcs_integrated)
		return "partial";
	return "absent";
}

/**
 * @brief Handle output.
 * @param i Object referenced by i.
 */
static void output(const info_t *i)
{
	char r[96];
	long target =
		i->leb_size > 0
			? (long)((TARGET_BYTES + i->leb_size - 1) / i->leb_size)
			: -1;
	int ok = compatible(i, r, sizeof r);
	printf("schema=2\nmodel=%s\narchitecture=%s\nkernel=%s\nfirmware=%"
	       "s\nmountpoint=%s\nfilesystem=%s\nvolume=%s\nvolume_type=%"
	       "s\nvolumes=%s\n",
	       i->model,
	       i->arch,
	       i->kernel,
	       i->firmware,
	       i->mountpoint,
	       i->fs_type,
	       i->volume,
	       i->volume_type,
	       i->volumes);
	printf("flash_bytes=%ld\nubi_devices=%ld\ntotal_pebs=%ld\navailable_"
	       "pebs=%ld\nreserved_pebs=%ld\nbad_pebs=%ld\nleb_size=%"
	       "ld\nvolume_lebs=%ld\nfilesystem_total_bytes=%llu\nfilesystem_"
	       "free_bytes=%llu\ntarget_bytes=%llu\ntarget_lebs=%ld\nreserve_"
	       "lebs=%d\ncompatible=%s\nreason=%s\nexpansion_enabled=false\n",
	       i->flash_bytes,
	       i->ubi_devices,
	       i->total_pebs,
	       i->avail_pebs,
	       i->reserved_pebs,
	       i->bad_pebs,
	       i->leb_size,
	       i->volume_lebs,
	       i->fs_total,
	       i->fs_free,
	       TARGET_BYTES,
	       target,
	       RESERVE_LEBS,
	       ok ? "true" : "false",
	       r);
}

/**
 * @brief Handle status.
 * @param i Object referenced by i.
 */
static void status(const info_t *i)
{
	printf("schema=3\nmodel=%s\nfirmware=%s\nkernel=%s\nubi_device="
	       "ubi0\navailable_lebs=%ld\nleb_size=%ld\nmin_io_size=%"
	       "ld\nvolumes=%s\nsbdata_present=%s\nsbdata_id=%ld\nsbdata_type=%"
	       "s\nsbdata_lebs=%ld\nsbdata_leb_size=%ld\nsbdata_corrupted=%"
	       "ld\nsbdata_mounted=%s\nbind_mounted=%s\napplet_directory=%"
	       "s\nstandard_applets=%s\nstandalone_base=%s\noriginal_total_"
	       "bytes=%llu\noriginal_free_bytes=%llu\nsbdata_total_bytes=%"
	       "llu\nsbdata_free_bytes=%llu\nformatter_path=%s\nformatter_"
	       "location=%s\nformatter_version=%s\nformatter_checksum=%"
	       "s\nformatter_valid=%s\nformatter_reason=%s\nmemory_available_"
	       "kb=%ld\ntmp_available_bytes=%llu\nboot_helper=%s\nrcs_local=%"
	       "s\nrcs_integrated=%s\ninstallation_state=%s\n",
	       i->model,
	       i->firmware,
	       i->kernel,
	       i->avail_pebs,
	       i->leb_size,
	       i->min_io_size,
	       i->volumes,
	       i->sbdata_present == 1 ? "true" : "false",
	       i->sbdata_id,
	       i->sbdata_type,
	       i->sbdata_lebs,
	       i->sbdata_leb_size,
	       i->sbdata_corrupted,
	       i->sbdata_mounted ? "true" : "false",
	       i->bind_mounted ? "true" : "false",
	       i->applet_dir ? "present" : "missing",
	       i->standard_applets ? "present" : "missing",
	       i->standalone_base ? "present" : "missing",
	       i->fs_total,
	       i->fs_free,
	       i->sbdata_total,
	       i->sbdata_free,
	       i->formatter_path,
	       i->formatter_temporary ? "temporary" : "persistent",
	       i->formatter_version,
	       i->formatter_checksum,
	       i->formatter_valid ? "true" : "false",
	       i->formatter_reason,
	       i->memory_available_kb,
	       i->tmp_available_bytes,
	       i->boot_helper ? "present" : "missing",
	       i->rcs_local ? "present" : "missing",
	       i->rcs_integrated ? "true" : "false",
	       state(i));
}

/**
 * @brief Handle prepare.
 * @param i Object referenced by i.
 * @return Zero on success; otherwise a nonzero status or error code.
 */
static int prepare(const info_t *i)
{
	char r[128];
	int ok = prepare_ok(i, r, sizeof r);
	status(i);
	printf("prepare_ready=%s\nprepare_reason=%s\nplanned_volume="
	       "sbdata\nplanned_bytes=%llu\nplanned_lebs=%ld\nminimum_free_"
	       "reserve_lebs=%d\ninstall_enabled=false\n"
	       "install_scope=legacy_native_command\n"
	       "initialization_frontend=storage-setup.sh\n",
	       ok ? "true" : "false",
	       r,
	       TARGET_BYTES,
	       i->leb_size > 0
		       ? (long)((TARGET_BYTES + i->leb_size - 1) / i->leb_size)
		       : -1,
	       RESERVE_LEBS);
	return ok ? 0 : 2;
}

/**
 * @brief Handle verify.
 * @param i Object referenced by i.
 * @return Zero on success; otherwise a nonzero status or error code.
 */
static int verify(const info_t *i)
{
	const char *r = "verified";
	int ok = 1;
	if (i->sbdata_present != 1) {
		r = "sbdata_missing";
		ok = 0;
	} else if (strcmp(i->sbdata_type, "dynamic") ||
		   i->sbdata_corrupted != 0) {
		r = "sbdata_invalid";
		ok = 0;
	} else if (!i->sbdata_mounted) {
		r = "sbdata_not_mounted";
		ok = 0;
	} else if (!i->bind_mounted) {
		r = "applet_bind_missing";
		ok = 0;
	} else if (!i->standard_applets) {
		r = "standard_applets_missing";
		ok = 0;
	} else if (!i->standalone_base) {
		r = "standalonebase_missing";
		ok = 0;
	} else if (!i->boot_helper || !i->rcs_integrated) {
		r = "boot_integration_incomplete";
		ok = 0;
	}
	status(i);
	printf("verified=%s\nverify_reason=%s\nrecovery_advice=%s\n",
	       ok ? "true" : "false",
	       r,
	       ok ? "none"
		  : "leave_original_ubifs_untouched_and_remove_only_the_applet_"
		    "bind_mount");
	return ok ? 0 : 3;
}

/**
 * @brief Handle le32.
 * @param p Input text for p.
 * @return Value produced by the operation.
 */
static uint32_t le32(const unsigned char *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
	       (uint32_t)p[3] << 24;
}

/**
 * @brief Handle le64.
 * @param p Input text for p.
 * @return Value produced by the operation.
 */
static uint64_t le64(const unsigned char *p)
{
	return (uint64_t)le32(p) | (uint64_t)le32(p + 4) << 32;
}

/**
 * @brief Handle inspect.
 * @param n Capacity or item count associated with the operation.
 * @return Zero on success; otherwise a nonzero status or error code.
 */
static int inspect(const char *n)
{
	unsigned char b[128];
	int fd = open(n, O_RDONLY);
	ssize_t z;
	if (fd < 0) {
		perror(n);
		return 66;
	}
	z = read(fd, b, sizeof b);
	close(fd);
	if (z < (ssize_t)sizeof b) {
		fprintf(stderr, "short UBIFS superblock read\n");
		return 65;
	}
	printf("schema=1\nkind=ubifs_superblock\nmagic=0x%08x\n", le32(b));
	if (le32(b) != MAGIC || b[20] != SB_NODE) {
		printf("valid=false\nreason=not_ubifs_superblock\n");
		return 2;
	}
	printf("valid=true\nnode_length=%u\nsequence=%llu\nflags=%u\nmin_io_"
	       "size=%u\nleb_size=%u\nleb_cnt=%u\nmax_leb_cnt=%u\nmax_bud_"
	       "bytes=%llu\nlog_lebs=%u\nlpt_lebs=%u\norph_lebs=%u\njhead_cnt=%"
	       "u\nfanout=%u\nlsave_cnt=%u\nformat_version=%u\nread_version="
	       "0\nformat=w%u/r0\ndefault_compressor=%u\n",
	       le32(b + 16),
	       (unsigned long long)le64(b + 8),
	       le32(b + 28),
	       le32(b + 32),
	       le32(b + 36),
	       le32(b + 40),
	       le32(b + 44),
	       (unsigned long long)le64(b + 48),
	       le32(b + 56),
	       le32(b + 60),
	       le32(b + 64),
	       le32(b + 68),
	       le32(b + 72),
	       le32(b + 76),
	       le32(b + 80),
	       le32(b + 80),
	       b[84] | (unsigned)b[85] << 8);
	return 0;
}

/**
 * @brief Verify module.
 * @param n Capacity or item count associated with the operation.
 * @return Zero on success; otherwise a nonzero status or error code.
 */
static int verify_module(const char *n)
{
	char digest[65] = "";
	struct stat s;
	long size = -1;
	int valid = 0;
	if (!stat(n, &s)) {
		size = (long)s.st_size;
		if (S_ISREG(s.st_mode) && !sha_file(n, digest) &&
		    size == 199119 &&
		    !strcmp(digest,
			    "63652ce67df06a78abb84a4986253bdab02fbd7b7c000779c6"
			    "0b3d393ba9566b"))
			valid = 1;
	}
	printf("module_path=%s\nmodule_size=%ld\nmodule_sha256=%s\nmodule_"
	       "valid=%s\n",
	       n,
	       size,
	       digest,
	       valid ? "true" : "false");
	return valid ? 0 : 2;
}

/**
 * @brief Verify sbdata image.
 * @param n Capacity or item count associated with the operation.
 * @return Zero on success; otherwise a nonzero status or error code.
 */
static int verify_sbdata_image(const char *n)
{
	char digest[65] = "";
	struct stat s;
	long size = -1;
	int valid = 0;
	if (!stat(n, &s)) {
		size = (long)s.st_size;
		if (S_ISREG(s.st_mode) && !sha_file(n, digest) &&
		    size == 1806336 &&
		    !strcmp(digest,
			    "34f26e33d80393c1f1f497e85dff6b78dbc6fc0e2df98ed115"
			    "0ea45c6631065d"))
			valid = 1;
	}
	printf("image_path=%s\nimage_size=%ld\nimage_sha256=%s\nimage_valid=%"
	       "s\n",
	       n,
	       size,
	       digest,
	       valid ? "true" : "false");
	return valid ? 0 : 2;
}

/**
 * @brief Compare two regular files without depending on a firmware cmp tool.
 * @param first Original file pathname.
 * @param second Copied file pathname.
 * @return Zero for identical readable files, two for differences or errors.
 */
static int verify_copy(const char *first, const char *second)
{
	unsigned char a[4096], b[4096];
	FILE *left = fopen(first, "rb"), *right = fopen(second, "rb");
	int result = 2;
	size_t na, nb;

	if (!left || !right)
		goto done;
	do {
		na = fread(a, 1, sizeof a, left);
		nb = fread(b, 1, sizeof b, right);
		if (na != nb || memcmp(a, b, na) || ferror(left) ||
		    ferror(right))
			goto done;
	} while (na);
	result = 0;
done:
	if (left)
		fclose(left);
	if (right)
		fclose(right);
	return result;
}

/**
 * @brief Run the program entry point.
 * @param argc Number of command-line arguments.
 * @param argv Command-line argument vector.
 * @return Process exit status.
 */
int main(int argc, char **argv)
{
	info_t i;
	if (argc == 2 && !strcmp(argv[1], "--version")) {
		puts("sb-storage-helper 0.7.3 dynamic-layout-v1");
		return 0;
	}
	if (argc == 2 && !strcmp(argv[1], "validate-layout"))
		return layout_report();
	if (argc == 2 && !strcmp(argv[1], "validate-target"))
		return layout_target() ? 0 : 2;
	if (argc == 2 && !strcmp(argv[1], "validate-controller-node"))
		return layout_device_node(0) ? 0 : 2;
	if (argc == 2 && !strcmp(argv[1], "validate-target-node"))
		return layout_device_node(1) ? 0 : 2;
	if (argc == 4 && !strcmp(argv[1], "verify-copy"))
		return verify_copy(argv[2], argv[3]);
	if (argc == 3 && !strcmp(argv[1], "inspect-superblock"))
		return inspect(argv[2]);
	if (argc == 3 && !strcmp(argv[1], "verify-module"))
		return verify_module(argv[2]);
	if (argc == 3 && !strcmp(argv[1], "verify-sbdata-image"))
		return verify_sbdata_image(argv[2]);
	if (argc != 2) {
		fprintf(stderr,
			"usage: sb-storage-helper "
			"--version|validate-layout|validate-target|validate-controller-node|validate-target-node|info|check|storage-status|prepare|install|verify|"
			"verify-module FILE|verify-sbdata-image "
			"FILE|inspect-superblock FILE|expand\n");
		return 64;
	}
	if (!strcmp(argv[1], "install")) {
		fprintf(stderr,
			"legacy native install command is disabled; use the "
			"explicit Extended Storage initialization workflow in "
			"storage-setup.sh; no storage mutation performed\n");
		return 78;
	}
	if (!strcmp(argv[1], "expand")) {
		fprintf(stderr,
			"production expansion is permanently disabled\n");
		return 78;
	}
	if (strcmp(argv[1], "info") && strcmp(argv[1], "check") &&
	    strcmp(argv[1], "storage-status") && strcmp(argv[1], "prepare") &&
	    strcmp(argv[1], "verify")) {
		fprintf(stderr, "unknown command\n");
		return 64;
	}
	collect(&i);
	if (!strcmp(argv[1], "storage-status")) {
		status(&i);
		return 0;
	}
	if (!strcmp(argv[1], "prepare"))
		return prepare(&i);
	if (!strcmp(argv[1], "verify"))
		return verify(&i);
	output(&i);
	return !strcmp(argv[1], "check") && !compatible(&i, (char[96]){0}, 96)
		       ? 2
		       : 0;
}
