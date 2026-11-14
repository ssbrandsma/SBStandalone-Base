/* Read-only Baby layout qualification. Included after the rooted I/O helpers.
 * Kernel accounting and size policy are documented in the dynamic-layout report.
 * All numeric inputs are bounded before arithmetic, including on 32-bit ARM.
 */
typedef struct {
	unsigned long lebs[6], good, bad, available, reserve, sum;
	char names[6][32];
	int existing;
	const char *reason;
} layout_t;

static int layout_number(const char *path_name, unsigned long *result)
{
	char b[64];
	unsigned long v = 0;
	if (read_text(path_name, b, sizeof b) || !b[0]) return 0;
	for (const char *p = b; *p; ++p) {
		if (*p < '0' || *p > '9' || v > (UINT32_MAX - (unsigned)(*p - '0')) / 10)
			return 0;
		v = v * 10 + (unsigned)(*p - '0');
	}
	*result = v;
	return 1;
}

static int layout_text(const char *p, const char *expected)
{
	char b[256];
	return !read_text(p, b, sizeof b) && !strcmp(b, expected);
}

static int layout_cpu(char *text)
{
	char *save, *line;
	int hardware = 0, cpu = 0;
	for (line = strtok_r(text, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
		char *colon = strchr(line, ':'), *end, *value;
		if (!colon) continue;
		end = colon;
		while (end > line && strchr(" \t", end[-1])) --end;
		*end = 0;
		value = colon + 1;
		while (*value && strchr(" \t", *value)) ++value;
		if (!strcmp(line, "Hardware")) {
			if (hardware || (strcmp(value, "Baby") && strcmp(value, "Logitech MX25 Baby Board"))) return 0;
			hardware = 1;
		}
		if (!strcmp(line, "Processor") && !strncmp(value, "ARM926EJ-S", 10)) cpu = 1;
	}
	return hardware && (cpu || layout_text("/proc/sys/kernel/architecture", "armv5tejl"));
}

/* Image contains 14 LEBs and has max_leb_cnt=521. Existing allocations in
 * that interval can be read without enlarging or rewriting the filesystem.
 * Creation and destructive reinitialization retain the 521-LEB policy. */
static int layout_target(void)
{
	unsigned long lebs, bytes;
	return layout_text("/sys/class/ubi/ubi0_5/name", "sbdata") &&
	       layout_text("/sys/class/ubi/ubi0_5/type", "dynamic") &&
	       layout_text("/sys/class/ubi/ubi0_5/alignment", "1") &&
	       layout_text("/sys/class/ubi/ubi0_5/usable_eb_size", "129024") &&
	       layout_text("/sys/class/ubi/ubi0/min_io_size", "2048") &&
	       layout_text("/sys/class/ubi/ubi0_5/corrupted", "0") &&
	       layout_text("/sys/class/ubi/ubi0_5/upd_marker", "0") &&
	       layout_number("/sys/class/ubi/ubi0_5/reserved_ebs", &lebs) &&
	       lebs >= 14 && lebs <= 521 &&
	       layout_number("/sys/class/ubi/ubi0_5/data_bytes", &bytes) &&
	       bytes == lebs * 129024UL;
}

/* A correctly named but stale character node must never redirect the writer
 * to a stock volume. Sysfs dev_t, rather than the pathname, is authoritative. */
static int layout_device_node(int target)
{
	char p[PATH_MAX], dev[32], canonical[32], tail;
	struct stat st;
	unsigned major_number, minor_number;
	if (read_text(target ? "/sys/class/ubi/ubi0_5/dev" : "/sys/class/ubi/ubi0/dev", dev, sizeof dev) ||
	    sscanf(dev, "%4u:%7u%c", &major_number, &minor_number, &tail) != 2 ||
	    major_number > 4095 || minor_number > 1048575) return 0;
	snprintf(canonical, sizeof canonical, "%u:%u", major_number, minor_number);
	if (strcmp(dev, canonical)) return 0;
	path(p, sizeof p, target ? "/dev/ubi0_5" : "/dev/ubi0");
	if (stat(p, &st)) return 0;
	if (root()[0] && S_ISREG(st.st_mode)) return 1;
	return S_ISCHR(st.st_mode) && major(st.st_rdev) == major_number &&
	       minor(st.st_rdev) == minor_number;
}

/* CramFS stores its own image length; UBI data_bytes is only capacity.
 * A variable backup allocation must contain a recognizable, bounded CramFS
 * superblock. This is a read, never a mount or an image update. */
static int layout_backup_image(unsigned id, unsigned long capacity)
{
	unsigned char b[76];
	char p[PATH_MAX], relative[32];
	unsigned long length, magic, flags;
	FILE *f = NULL;
	struct stat st;
	/* SqueezeOS uses /dev/mtd/6ro, while other supported userspaces expose
	 * /dev/mtd6ro. Prefer read-only character nodes; never create a node. */
	static const char *formats[] = {"/dev/mtd/%uro", "/dev/mtd%uro", "/dev/mtd/%u", "/dev/mtd%u"};
	for (unsigned i = 0; i < sizeof formats / sizeof formats[0]; ++i) {
		snprintf(relative, sizeof relative, formats[i], id + 2);
		path(p, sizeof p, relative);
		f = fopen(p, "rb");
		if (!f) continue;
		if (fstat(fileno(f), &st) ||
		    (!(root()[0] && S_ISREG(st.st_mode)) &&
		     (!S_ISCHR(st.st_mode) || major(st.st_rdev) != 90 ||
		      minor(st.st_rdev) != 2 * (id + 2) + (i < 2)))) {
			fclose(f); return 0;
		}
		break;
	}
	if (!f) return 0;
	if (fread(b, 1, sizeof b, f) != sizeof b) { fclose(f); return 0; }
	fclose(f);
	magic = (unsigned long)b[0] | (unsigned long)b[1] << 8 |
		(unsigned long)b[2] << 16 | (unsigned long)b[3] << 24;
	length = (unsigned long)b[4] | (unsigned long)b[5] << 8 |
		(unsigned long)b[6] << 16 | (unsigned long)b[7] << 24;
	flags = (unsigned long)b[8] | (unsigned long)b[9] << 8 |
		(unsigned long)b[10] << 16 | (unsigned long)b[11] << 24;
	return magic == 0x28cd3d45UL && length >= sizeof b && length <= capacity &&
	       !(flags & ~0x7ffUL) &&
	       !memcmp(b + 16, "Compressed ROMFS", 16) &&
	       ((b[65] & 0xf0) == 0x40); /* root inode is a directory */
}

static int layout_validate(layout_t *l)
{
	char firmware[64], p[PATH_MAX], b[4096], name[64], tail;
	const char *names[6] = {"kernel", "cramfs", "ubifs", "kernel_bak", "cramfs_bak", "sbdata"};
	unsigned long fixed[5] = {23, 106, 82, 23, 0}, n, bytes, flags;
	unsigned seen = 0, mtd_seen = 0, id, size, erase;
	DIR *d;
	struct dirent *e;
	FILE *f;
#define LFAIL(r) do { l->reason = r; return 0; } while (0)
#define LNUM(field, dest) if (!layout_number("/sys/class/ubi/ubi0/" field, &(dest))) LFAIL("invalid_controller_metadata")
	memset(l, 0, sizeof *l);
	if (read_text("/proc/cpuinfo", b, sizeof b) || !layout_cpu(b)) LFAIL("unsupported_platform");
	if (!layout_text("/proc/sys/kernel/osrelease", "2.6.26.8-rt16")) LFAIL("unsupported_kernel");
	if (read_first_line("/etc/squeezeos.version", firmware, sizeof firmware)) LFAIL("unsupported_firmware");
	if (!strcmp(firmware, "9.0.1 r17084")) fixed[1] = 113;
	else if (strcmp(firmware, "7.7.2 r9663") && strcmp(firmware, "7.7.3 r16676"))
		LFAIL("unsupported_firmware");
	/* Qualified A/B firmware identifies the active bank by role, not by
	 * physical volume order. A complete pair swap preserves all size, image,
	 * inventory and accounting checks, including 9.0.1's 113-LEB active role. */
	if (!strcmp(firmware, "7.7.2 r9663") ||
	    layout_text("/sys/class/ubi/ubi0_0/name", "kernel_bak")) {
		names[0] = "kernel_bak"; names[1] = "cramfs_bak";
		names[3] = "kernel"; names[4] = "cramfs";
		fixed[4] = fixed[1]; fixed[1] = 0;
	}
	LNUM("total_eraseblocks", l->good);
	LNUM("bad_peb_count", l->bad);
	LNUM("avail_eraseblocks", l->available);
	LNUM("reserved_for_bad", l->reserve);
	if (l->good > 1014 || l->bad > 1014 || l->available > 1014 ||
	    l->good + l->bad != 1014 || l->reserve != 10 ||
	    !layout_text("/sys/class/ubi/ubi0/eraseblock_size", "129024") ||
	    !layout_text("/sys/class/ubi/ubi0/min_io_size", "2048")) LFAIL("unsupported_ubi_geometry");
	if (!layout_text("/sys/class/ubi/ubi0/mtd_num", "1")) LFAIL("unexpected_ubi_mtd_relationship");
	path(p, sizeof p, "/sys/class/ubi");
	d = opendir(p);
	if (!d) LFAIL("required_volume_missing");
	while ((e = readdir(d))) {
		if (!strncmp(e->d_name, "ubi", 3) && strcmp(e->d_name, "ubi0") &&
		    strncmp(e->d_name, "ubi0_", 5)) { closedir(d); LFAIL("unexpected_ubi_device"); }
		if (strncmp(e->d_name, "ubi0_", 5)) continue;
		if (strlen(e->d_name) != 6 || e->d_name[5] < '0' || e->d_name[5] > '5') {
			closedir(d); LFAIL("unexpected_volume_present");
		}
		id = (unsigned)(e->d_name[5] - '0');
		snprintf(name, sizeof name, "ubi0_%u", id);
		if (strcmp(name, e->d_name) || (seen & (1U << id))) {
			closedir(d); LFAIL("volume_identity_mismatch");
		}
		seen |= 1U << id;
	}
	closedir(d);
	if ((seen & 31) != 31) LFAIL("required_volume_missing");
	l->existing = !!(seen & 32);
	for (id = 0; id < (l->existing ? 6U : 5U); ++id) {
#define VPATH(field) snprintf(p, sizeof p, "/sys/class/ubi/ubi0_%u/" field, id)
#define VTEXT(field, expected, reason) do { VPATH(field); if (!layout_text(p, expected)) LFAIL(reason); } while (0)
#define VNUM(field, dest) do { VPATH(field); if (!layout_number(p, &(dest))) LFAIL("invalid_volume_metadata"); } while (0)
		VTEXT("name", names[id], "volume_identity_mismatch");
		snprintf(l->names[id], sizeof l->names[id], "%s", names[id]);
		VTEXT("type", "dynamic", "volume_type_mismatch");
		VNUM("alignment", n); if (n != 1) LFAIL("volume_alignment_mismatch");
		VNUM("usable_eb_size", n); if (n != 129024) LFAIL("unsupported_ubi_geometry");
		VNUM("reserved_ebs", l->lebs[id]);
		if (!l->lebs[id] || l->lebs[id] > 1014) LFAIL("volume_size_unsupported");
		VNUM("data_bytes", bytes);
		if (bytes != l->lebs[id] * 129024UL) LFAIL("volume_capacity_inconsistent");
		VNUM("corrupted", flags); if (flags) LFAIL("volume_corrupted");
		VNUM("upd_marker", flags); if (flags) LFAIL("volume_update_in_progress");
		if (id < 5 && fixed[id] && l->lebs[id] != fixed[id]) LFAIL("volume_size_unsupported");
		/* Retain the qualified image maximum and existing allocation policy. */
		if (id == 5 && !layout_target()) LFAIL("filesystem_image_capacity_unsupported");
		l->sum += l->lebs[id];
	}
	/* Backup CramFS may be smaller than the running firmware, but not larger.
	 * The free-space policy below retains enough room to replace it at the
	 * running firmware size without touching sbdata or another stock volume. */
	id = fixed[1] ? 4 : 1;
	if (l->lebs[id] > (fixed[1] ? fixed[1] : fixed[4])) LFAIL("volume_size_unsupported");
	if (l->sum + l->available + l->reserve + 2 + 1 + 1 != l->good)
		LFAIL("ubi_accounting_inconsistent");
	/* /proc/mtd contains one physical UBI partition and logical gluebi maps.
	 * Never add the gluebi sizes to the physical partition capacity. */
	path(p, sizeof p, "/proc/mtd"); f = fopen(p, "r");
	if (!f) LFAIL("mtd_geometry_inconsistent");
	while (fgets(b, sizeof b, f)) {
		if (!strncmp(b, "dev:", 4)) continue;
		if (sscanf(b, "mtd%1u: %8x %8x \"%63[^\"]\" %c", &id, &size, &erase, name, &tail) != 4 ||
		    id > 7 || (mtd_seen & (1U << id))) { fclose(f); LFAIL("mtd_geometry_inconsistent"); }
		mtd_seen |= 1U << id;
		if ((id == 0 && (size != 0x80000 || erase != 0x20000 || strcmp(name, "redboot"))) ||
		    (id == 1 && (size != 1014U * 131072U || erase != 131072 || strcmp(name, "ubi"))) ||
		    (id >= 2 && (id > (l->existing ? 7U : 6U) ||
		        size != l->lebs[id-2] * 129024UL || erase != 129024 || strcmp(name, names[id-2])))) {
			fclose(f); LFAIL("mtd_geometry_inconsistent");
		}
	}
	n = ferror(f); fclose(f);
	/* sbdata's gluebi map is optional until a consumer opens it. */
	if (n || (mtd_seen & 127) != 127) LFAIL("mtd_geometry_inconsistent");
	id = fixed[1] ? 4 : 1;
	if (!layout_backup_image(id, l->lebs[id] * 129024UL))
		LFAIL("backup_cramfs_image_unverified");
	if (!l->existing && (l->available < 600 || l->available < 521 + RESERVE_LEBS))
		LFAIL("insufficient_available_lebs");
	n = fixed[1] ? fixed[1] : fixed[4];
	if ((l->existing ? l->available : l->available - 521) < n - l->lebs[fixed[1] ? 4 : 1])
		LFAIL("insufficient_firmware_replacement_capacity");
	l->reason = "validated_dynamic_layout";
	return 1;
#undef LFAIL
#undef LNUM
#undef VPATH
#undef VTEXT
#undef VNUM
}

static int layout_report(void)
{
	layout_t l;
	sha256_t digest;
	char canonical[256], fingerprint[65];
	int ok = layout_validate(&l);
	sha_init(&digest);
	snprintf(canonical, sizeof canonical, "%s:%lu:%lu:%lu:%lu", l.reason, l.good, l.bad, l.available, l.reserve);
	sha_add(&digest, (const unsigned char *)canonical, strlen(canonical));
	for (unsigned id = 0; id < 6; ++id) {
		snprintf(canonical, sizeof canonical, ":%u:%s:%lu", id, l.names[id], l.lebs[id]);
		sha_add(&digest, (const unsigned char *)canonical, strlen(canonical));
	}
	sha_done(&digest, fingerprint);
	printf("LAYOUT_FINGERPRINT=%s\n", fingerprint);
	printf("DYNAMIC_LAYOUT_COMPATIBLE=%d\nLAYOUT_REASON=%s\n", ok, l.reason);
	if (!ok) {
		/* Do not present unread or rejected metadata as measured zeroes. The
		 * shell analysis also lists each raw sysfs value, including absences. */
		puts("ALLOCATION_PLAN_VALID=0");
		return 2;
	}
	printf("EXISTING_RESERVED_LEBS=%lu\nTOTAL_GOOD_PEBS=%lu\nBAD_PEBS=%lu\nAVAILABLE_LEBS=%lu\n",
	       l.sum, l.good, l.bad, l.available);
	printf("BAD_BLOCK_RESERVE=%lu\nLAYOUT_RESERVE=2\nWL_RESERVE=1\nATOMIC_CHANGE_RESERVE=1\n", l.reserve);
	printf("MINIMUM_AVAILABLE_LEBS=600\nPLANNED_TARGET_LEBS=521\nPLANNED_TARGET_BYTES=67221504\nMINIMUM_RETAINED_LEBS=79\n");
	printf("ALLOCATION_PLAN_VALID=%d\n", ok && !l.existing);
	printf("PLANNED_REMAINING_LEBS=%lu\n", l.available >= 521 ? l.available - 521 : 0);
	printf("RAW_AVAILABLE_BYTES=%llu\nRAW_AVAILABLE_MIB=%.2f\n",
	       (unsigned long long)l.available * 129024, (double)l.available * 129024 / 1048576);
	for (unsigned id = 0; id < 5; ++id) {
		printf("STOCK_%u_NAME=%s\nSTOCK_%u_LEBS=%lu\nSTOCK_%u_EXPECTED_DATA_BYTES=%lu\n",
		       id, l.names[id], id, l.lebs[id], id, l.lebs[id] * 129024UL);
	}
	printf("BACKUP_CRAMFS_POLICY=bounded_cramfs_superblock_capacity_not_larger_than_running_cramfs_with_replacement_headroom\n");
	return ok ? 0 : 2;
}
