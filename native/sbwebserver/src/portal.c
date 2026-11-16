/* One process, persistent per-device identity. Never log private material. */
#include "portal.h"
#include "auth.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/random.h>
#include <arpa/inet.h>
#include <limits.h>
#include <time.h>
#include <ifaddrs.h>

static struct {
	int available, port, rng_ready;
	char error[160], fingerprint[96], hostname[128];
	char cert[8192], key[4096];
	uint64_t started[24];
	unsigned long ids[24];
} P;

static struct {
	struct mg_mgr *mgr;
	mg_event_handler_t fn;
	void *data;
} G;
#if MG_TLS != MG_TLS_NONE
#include <openssl/ssl.h>
#include <openssl/pem.h>
#include <openssl/ec.h>
#include <openssl/x509v3.h>
#include <openssl/rand.h>

static int read_private(int dir, const char *name, char *out, size_t cap)
{
	int fd = openat(dir, name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
	struct stat st;
	int ok = fd >= 0 && !fstat(fd, &st) && S_ISREG(st.st_mode) &&
		 st.st_uid == geteuid() && !(st.st_mode & 0077) &&
		 st.st_nlink == 1 && st.st_size > 0 && (size_t)st.st_size < cap;
	size_t got = 0;
	while (ok && got < (size_t)st.st_size) {
		ssize_t n = read(fd, out + got, (size_t)st.st_size - got);
		if (n <= 0) {
			ok = 0;
			break;
		}
		got += n;
	}
	if (fd >= 0)
		close(fd);
	if (ok)
		out[got] = 0;
	return ok;
}

static int
save_private(int dir, const char *name, const char *data, size_t size)
{
	int fd = openat(dir,
			name,
			O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
			0600);
	if (fd < 0)
		return 0;
	size_t at = 0;
	int ok = 1;
	while (at < size) {
		ssize_t n = write(fd, data + at, size - at);
		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0) {
			ok = 0;
			break;
		}
		at += n;
	}
	if (ok)
		ok = !fsync(fd);
	if (close(fd))
		ok = 0;
	return ok;
}

static int identity_valid(void)
{
	BIO *cb = BIO_new_mem_buf(P.cert, -1), *kb = BIO_new_mem_buf(P.key, -1);
	X509 *cert = cb ? PEM_read_bio_X509(cb, NULL, NULL, NULL) : NULL;
	EVP_PKEY *key =
		kb ? PEM_read_bio_PrivateKey(kb, NULL, NULL, NULL) : NULL;
	int ok = cert && key && X509_check_private_key(cert, key) == 1;
	if (ok) {
		EVP_PKEY *pub = X509_get_pubkey(cert);
		ok = pub && X509_verify(cert, pub) == 1;
		EVP_PKEY_free(pub);
	}
	if (ok)
		ok = X509_cmp_current_time(X509_get_notBefore(cert)) < 0 &&
		     X509_cmp_current_time(X509_get_notAfter(cert)) > 0;
	if (ok) {
		unsigned char digest[32];
		unsigned int len = 0;
		ok = X509_digest(cert, EVP_sha256(), digest, &len) == 1 &&
		     len == 32;
		if (ok)
			for (unsigned i = 0; i < 32; i++)
				snprintf(P.fingerprint + i * 3,
					 4,
					 i == 31 ? "%02X" : "%02X:",
					 digest[i]);
	}
	EVP_PKEY_free(key);
	X509_free(cert);
	BIO_free(cb);
	BIO_free(kb);
	return ok;
}

static int os_seed(void)
{
	/* P-256 provides 128-bit security. Obtain 128 credited entropy bits on
	 * legacy kernels, then mix them into the kernel CSPRNG used by wolfSSL.
	 * wolfSSL RAND_seed alone does not incorporate caller bytes. No entropy
	 * count is fabricated. A protected boot-ID witness avoids draining the
	 * pool again on webserver restarts within the same boot. */
	char boot[80] = {0}, seen[80] = {0};
	int b = open("/proc/sys/kernel/random/boot_id", O_RDONLY | O_CLOEXEC);
	ssize_t bn = b >= 0 ? read(b, boot, sizeof boot - 1) : -1;
	if (b >= 0)
		close(b);
	if (bn <= 0)
		return 0;
	if (mkdir("/tmp/sb-web-entropy", 0700) && errno != EEXIST)
		return 0;
	int dir = open("/tmp/sb-web-entropy",
		       O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	struct stat st;
	if (dir < 0)
		return 0;
	if (fstat(dir, &st) || st.st_uid != geteuid() || (st.st_mode & 0077)) {
		close(dir);
		return 0;
	}
	int marker = openat(dir, "ready", O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
	if (marker >= 0) {
		int valid = !fstat(marker, &st) && S_ISREG(st.st_mode) &&
			    st.st_uid == geteuid() && !(st.st_mode & 0077) &&
			    st.st_nlink == 1;
		ssize_t n = valid ? read(marker, seen, sizeof seen - 1) : -1;
		close(marker);
		if (n == bn && !memcmp(boot, seen, (size_t)bn)) {
			close(dir);
			return RAND_status() == 1;
		}
		close(dir);
		return 0; /* Stale/unsafe witness: fail closed, never replace
			     blindly. */
	}
	if (errno != ENOENT) {
		close(dir);
		return 0;
	}
	unsigned char seed[16];
	size_t got = 0;
	int fd = open("/dev/random", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd >= 0) {
		/* Do not consume and discard a short pool on every boot retry. */
		int credited = 0;
		if (ioctl(fd, RNDGETENTCNT, &credited) || credited < 128) {
			close(fd);
			close(dir);
			return 0;
		}
		while (got < sizeof seed) {
			ssize_t n = read(fd, seed + got, sizeof seed - got);
			if (n < 0 && errno == EINTR)
				continue;
			if (n <= 0)
				break;
			got += (size_t)n;
		}
		close(fd);
	}
	int ok = got == sizeof seed;
	/* Linux 2.6.26 random_write mixes both the blocking and nonblocking
	 * pools. */
	if (ok) {
		fd = open("/dev/urandom", O_WRONLY | O_CLOEXEC);
		ok = fd >= 0 && write(fd, seed, sizeof seed) == sizeof seed;
		if (fd >= 0)
			close(fd);
	}
	if (ok) {
		RAND_seed(seed, sizeof seed);
		ok = RAND_status() == 1;
	}
	memset(seed, 0, sizeof seed);
	if (ok)
		ok = save_private(dir, "ready", boot, (size_t)bn);
	close(dir);
	return ok;
}

#if MG_TLS == MG_TLS_WOLFSSL
#include <wolfssl/wolfcrypt/asn.h>
#include <wolfssl/wolfcrypt/asn_public.h>
#include <wolfssl/wolfcrypt/ecc.h>
#include <wolfssl/wolfcrypt/random.h>

static int make_identity(const unsigned char serial[16])
{
	Cert cert;
	ecc_key key;
	WC_RNG rng;
	unsigned char der[4096], keyder[512], names[700];
	size_t used = 0;
	if (wc_InitCert(&cert) || wc_ecc_init(&key))
		return 0;
	if (wc_InitRng(&rng)) {
		wc_ecc_free(&key);
		return 0;
	}
	cert.sigType = CTC_SHA256wECDSA;
	cert.daysValid = 5 * 365;
	cert.isCA = 0;
	cert.basicConstSet = 1;
	cert.keyUsage = KEYUSE_DIGITAL_SIG;
	cert.extKeyUsage = EXTKEYUSE_SERVER_AUTH;
	memcpy(cert.serial, serial, 16);
	cert.serialSz = 16;
	snprintf(cert.subject.commonName,
		 sizeof cert.subject.commonName,
		 "%s",
		 P.hostname);
	/* SubjectAltName DER: a sequence of bounded DNS names and IPv4 octets.
	 */
	const char *dns[] = {P.hostname, "localhost"};
	for (unsigned i = 0; i < 2; i++) {
		size_t n = strlen(dns[i]);
		if (n > 127) {
			wc_FreeRng(&rng);
			wc_ecc_free(&key);
			return 0;
		}
		names[used++] = 0x82;
		names[used++] = (unsigned char)n;
		memcpy(names + used, dns[i], n);
		used += n;
	}
	struct ifaddrs *addresses = NULL;
	if (!getifaddrs(&addresses)) {
		for (struct ifaddrs *a = addresses; a; a = a->ifa_next)
			if (a->ifa_addr && a->ifa_addr->sa_family == AF_INET &&
			    used + 6 < sizeof names) {
				names[used++] = 0x87;
				names[used++] = 4;
				memcpy(names + used,
				       &((struct sockaddr_in *)a->ifa_addr)
						->sin_addr,
				       4);
				used += 4;
			}
		freeifaddrs(addresses);
	}
	size_t header = used < 128 ? 2 : used < 256 ? 3 : 4;
	cert.altNames[0] = 0x30;
	if (header == 2)
		cert.altNames[1] = (unsigned char)used;
	else if (header == 3) {
		cert.altNames[1] = 0x81;
		cert.altNames[2] = (unsigned char)used;
	} else {
		cert.altNames[1] = 0x82;
		cert.altNames[2] = (unsigned char)(used >> 8);
		cert.altNames[3] = (unsigned char)used;
	}
	memcpy(cert.altNames + header, names, used);
	cert.altNamesSz = (int)(header + used);
	int ok = wc_ecc_make_key_ex(&rng, 32, &key, ECC_SECP256R1) == 0;
	int size =
		ok ? wc_MakeCert(&cert, der, sizeof der, NULL, &key, &rng) : -1;
	if (size > 0)
		size = wc_SignCert(
			size, cert.sigType, der, sizeof der, NULL, &key, &rng);
	int kn = size > 0 ? wc_EccKeyToDer(&key, keyder, sizeof keyder) : -1;
	int cn = size > 0 ? wc_DerToPem(der,
					size,
					(unsigned char *)P.cert,
					sizeof P.cert - 1,
					CERT_TYPE)
			  : -1;
	int pn = kn > 0 ? wc_DerToPem(keyder,
				      kn,
				      (unsigned char *)P.key,
				      sizeof P.key - 1,
				      ECC_PRIVATEKEY_TYPE)
			: -1;
	ok = cn > 0 && pn > 0;
	if (ok) {
		P.cert[cn] = 0;
		P.key[pn] = 0;
		ok = identity_valid();
	}
	memset(keyder, 0, sizeof keyder);
	wc_ecc_free(&key);
	wc_FreeRng(&rng);
	return ok;
}
#else
static int make_identity(const unsigned char serial[16])
{
	EC_KEY *ec = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
	EVP_PKEY *key = EVP_PKEY_new();
	X509 *cert = X509_new();
	BIO *cb = NULL, *kb = NULL;
	int ok = ec && key && cert && EC_KEY_generate_key(ec) == 1 &&
		 EVP_PKEY_set1_EC_KEY(key, ec) == 1;
	if (ok) {
		BIGNUM *bn = BN_bin2bn(serial, 16, NULL);
		ASN1_INTEGER *number = bn ? BN_to_ASN1_INTEGER(bn, NULL) : NULL;
		ok = number && X509_set_serialNumber(cert, number) == 1;
		ASN1_INTEGER_free(number);
		BN_free(bn);
	}
	if (ok)
		ok = X509_set_version(cert, 2) == 1 &&
		     X509_gmtime_adj(X509_get_notBefore(cert), -86400) !=
			     NULL &&
		     X509_gmtime_adj(X509_get_notAfter(cert),
				     5L * 365 * 86400) != NULL &&
		     X509_set_pubkey(cert, key) == 1;
	if (ok) {
		X509_NAME *name = X509_get_subject_name(cert);
		ok = X509_NAME_add_entry_by_txt(name,
						"CN",
						MBSTRING_ASC,
						(unsigned char *)P.hostname,
						-1,
						-1,
						0) == 1 &&
		     X509_set_issuer_name(cert, name) == 1;
	}
	if (ok) {
		char san[800];
		snprintf(san,
			 sizeof san,
			 "DNS:%s,DNS:localhost,IP:127.0.0.1",
			 P.hostname);
		struct ifaddrs *addresses = NULL;
		if (!getifaddrs(&addresses)) {
			for (struct ifaddrs *a = addresses; a; a = a->ifa_next)
				if (a->ifa_addr &&
				    a->ifa_addr->sa_family == AF_INET) {
					char ip[INET_ADDRSTRLEN];
					struct sockaddr_in *in =
						(struct sockaddr_in *)
							a->ifa_addr;
					if (inet_ntop(AF_INET,
						      &in->sin_addr,
						      ip,
						      sizeof ip) &&
					    strcmp(ip, "127.0.0.1") &&
					    strlen(san) + strlen(ip) + 5 <
						    sizeof san) {
						strcat(san, ",IP:");
						strcat(san, ip);
					}
				}
			freeifaddrs(addresses);
		}
		X509_EXTENSION *ext = X509V3_EXT_conf_nid(
			NULL, NULL, NID_subject_alt_name, san);
		ok = ext && X509_add_ext(cert, ext, -1) == 1;
		X509_EXTENSION_free(ext);
	}
	if (ok) {
		const int ids[] = {NID_basic_constraints,
				   NID_key_usage,
				   NID_ext_key_usage};
		const char *values[] = {"critical,CA:FALSE",
					"critical,digitalSignature",
					"serverAuth"};
		for (unsigned i = 0; i < 3 && ok; i++) {
			X509_EXTENSION *ext = X509V3_EXT_conf_nid(
				NULL, NULL, ids[i], (char *)values[i]);
			ok = ext && X509_add_ext(cert, ext, -1) == 1;
			X509_EXTENSION_free(ext);
		}
	}
	if (ok)
		ok = X509_sign(cert, key, EVP_sha256()) > 0;
	if (ok) {
		cb = BIO_new(BIO_s_mem());
		kb = BIO_new(BIO_s_mem());
		ok = cb && kb && PEM_write_bio_X509(cb, cert) == 1 &&
		     PEM_write_bio_PrivateKey(
			     kb, key, NULL, NULL, 0, NULL, NULL) == 1;
	}
	if (ok) {
		int cn = BIO_read(cb, P.cert, sizeof P.cert - 1),
		    kn = BIO_read(kb, P.key, sizeof P.key - 1);
		ok = cn > 0 && kn > 0;
		if (ok) {
			P.cert[cn] = 0;
			P.key[kn] = 0;
			ok = identity_valid();
		}
	}
	BIO_free(cb);
	BIO_free(kb);
	EVP_PKEY_free(key);
	EC_KEY_free(ec);
	X509_free(cert);
	return ok;
}
#endif
static int generate(int parent)
{
	/* No identity generation with an unset clock or unseeded OS RNG. */
	if (time(NULL) < 1704067200 || !P.rng_ready || RAND_status() != 1)
		return 0;
	unsigned char serial[16];
	if (RAND_bytes(serial, sizeof serial) != 1)
		return 0;
	serial[0] &= 0x7f;
	int ok = make_identity(serial);
	if (ok) { /* Pin a single transaction directory; a partial identity is
		     never activated. */
		ok = !mkdirat(parent, ".tls-new", 0700);
		int dir = ok ? openat(parent,
				      ".tls-new",
				      O_RDONLY | O_DIRECTORY | O_NOFOLLOW |
					      O_CLOEXEC)
			     : -1;
		if (dir < 0)
			ok = 0;
		if (ok)
			ok = save_private(dir,
					  "device.crt",
					  P.cert,
					  strlen(P.cert)) &&
			     save_private(
				     dir, "device.key", P.key, strlen(P.key)) &&
			     !fsync(dir);
		if (dir >= 0)
			close(dir);
		if (ok)
			ok = !renameat(parent, ".tls-new", parent, "tls") &&
			     !fsync(parent);
	}
	return ok;
}

static int identity(void)
{
	const char *path = getenv("SBWEBSERVER_CONFIG_DIR");
	if (!path)
		path = "/mnt/storage/sbwebserver";
	char real[PATH_MAX];
	if (!realpath(path, real))
		return 0;
	int parent =
		open(real, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	struct stat st;
	if (parent < 0)
		return 0;
	int ok = !fstat(parent, &st) && st.st_uid == geteuid() &&
		 !(st.st_mode & 0022);
	if (!fstatat(parent, ".tls-rotation", &st, AT_SYMLINK_NOFOLLOW))
		ok = 0;
	if (ok && fstatat(parent, "tls", &st, AT_SYMLINK_NOFOLLOW)) {
		if (errno == ENOENT)
			ok = generate(parent);
		else
			ok = 0;
	} else if (ok) {
		int dir =
			openat(parent,
			       "tls",
			       O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
		ok = dir >= 0 && !fstat(dir, &st) && st.st_uid == geteuid() &&
		     !(st.st_mode & 0077) &&
		     read_private(dir, "device.crt", P.cert, sizeof P.cert) &&
		     read_private(dir, "device.key", P.key, sizeof P.key) &&
		     identity_valid();
		if (dir >= 0)
			close(dir);
	}
	close(parent);
	return ok;
}

static void remove_pair(int parent, const char *name)
{
	int fd = openat(
		parent, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	if (fd >= 0) {
		unlinkat(fd, "device.crt", 0);
		unlinkat(fd, "device.key", 0);
		fsync(fd);
		close(fd);
		unlinkat(parent, name, AT_REMOVEDIR);
	}
}

static int rotate_identity(void)
{
	if (!P.rng_ready)
		P.rng_ready = os_seed();
	if (!P.rng_ready)
		return 0;
	const char *path = getenv("SBWEBSERVER_CONFIG_DIR");
	if (!path)
		path = "/mnt/storage/sbwebserver";
	char real[PATH_MAX];
	if (!realpath(path, real))
		return 0;
	int parent =
		open(real, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	struct stat st;
	if (parent < 0)
		return 0;
	int ok = !fstat(parent, &st) && st.st_uid == geteuid() &&
		 !(st.st_mode & 0022);
	if (ok)
		ok = fstatat(parent,
			     "tls.previous",
			     &st,
			     AT_SYMLINK_NOFOLLOW) &&
		     errno == ENOENT;
	int marker = ok ? openat(parent,
				 ".tls-rotation",
				 O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW |
					 O_CLOEXEC,
				 0600)
			: -1;
	if (marker < 0) {
		close(parent);
		return 0;
	}
	ok = !fsync(marker) && !fsync(parent);
	close(marker);
	int moved = 0;
	if (ok) {
		moved = !renameat(parent, "tls", parent, "tls.previous");
		ok = moved;
	}
	char old_cert[sizeof P.cert], old_key[sizeof P.key],
		old_fp[sizeof P.fingerprint];
	memcpy(old_cert, P.cert, sizeof old_cert);
	memcpy(old_key, P.key, sizeof old_key);
	memcpy(old_fp, P.fingerprint, sizeof old_fp);
	if (ok)
		ok = auth_revoke_all() && generate(parent);
	if (ok) {
		remove_pair(parent, "tls.previous");
		unlinkat(parent, ".tls-rotation", 0);
		fsync(parent);
	} else {
		remove_pair(parent, ".tls-new");
		if (moved)
			remove_pair(parent, "tls");
		int restored = !moved ||
			       !renameat(parent, "tls.previous", parent, "tls");
		if (restored) {
			unlinkat(parent, ".tls-rotation", 0);
			fsync(parent);
		}
		memcpy(P.cert, old_cert, sizeof P.cert);
		memcpy(P.key, old_key, sizeof P.key);
		memcpy(P.fingerprint, old_fp, sizeof P.fingerprint);
	}
	memset(old_key, 0, sizeof old_key);
	close(parent);
	return ok;
}

#endif
int portal_host_ok(struct mg_connection *c, struct mg_http_message *m)
{
	struct mg_str *h = mg_http_get_header(m, "Host");
	if (!h || h->len >= 256 || !h->len)
		return 0;
	char host[256];
	memcpy(host, h->buf, h->len);
	host[h->len] = 0;
	char *port = strrchr(host, ':');
	if (port) {
		char *end;
		long n = strtol(port + 1, &end, 10);
		if (*end || n != mg_ntohs(c->loc.port))
			return 0;
		*port = 0;
	} else if (mg_ntohs(c->loc.port) != (c->is_tls ? 443 : 80))
		return 0;
	char ip[64];
	mg_snprintf(ip, sizeof ip, "%M", mg_print_ip, &c->loc);
	char hostname[128] = {0};
	gethostname(hostname, sizeof hostname - 7);
	char local[140];
	snprintf(local, sizeof local, "%s.local", hostname);
	int matched = !strcmp(host, ip) || !strcasecmp(host, "localhost") ||
		      !strcasecmp(host, hostname) || !strcasecmp(host, local);
	if (matched)
		return 1;
	struct ifaddrs *addresses = NULL;
	if (!getifaddrs(&addresses)) {
		for (struct ifaddrs *a = addresses; a; a = a->ifa_next)
			if (a->ifa_addr && a->ifa_addr->sa_family == AF_INET) {
				char address[INET_ADDRSTRLEN];
				if (inet_ntop(
					    AF_INET,
					    &((struct sockaddr_in *)a->ifa_addr)
						     ->sin_addr,
					    address,
					    sizeof address) &&
				    !strcmp(host, address))
					matched = 1;
			}
		freeifaddrs(addresses);
	}
	return matched;
}

#if MG_TLS != MG_TLS_NONE
static void portal_try_start(void *unused)
{
	(void)unused;
	if (P.available)
		return;
	if (!P.rng_ready)
		P.rng_ready = os_seed();
	if (!P.rng_ready || !identity()) {
		strcpy(P.error,
		       "Device certificate unavailable: check clock, entropy "
		       "and protected storage; existing identity was "
		       "preserved");
		memset(P.key, 0, sizeof P.key);
		return;
	}
	char url[64];
	snprintf(url, sizeof url, "http://0.0.0.0:%d", P.port);
	struct mg_connection *listener = mg_http_listen(G.mgr, url, G.fn, G.data);
	if (listener) {
		listener->data[0] = 1;
		P.available = 1;
		P.error[0] = 0;
	} else
		strcpy(P.error, "Secure portal port is unavailable");
}
#endif

void portal_init(struct mg_mgr *mgr,
		 int http_port,
		 mg_event_handler_t fn,
		 void *data)
{
	G.mgr = mgr;
	G.fn = fn;
	G.data = data;
	memset(&P, 0, sizeof P);
	gethostname(P.hostname, sizeof P.hostname - 7);
	for (char *p = P.hostname; *p; p++)
		if (!isalnum((unsigned char)*p) && *p != '-' && *p != '.')
			*p = '-';
	if (!strchr(P.hostname, '.'))
		strcat(P.hostname, ".local");
	const char *e = getenv("SBWEBSERVER_HTTPS_PORT");
	P.port = e ? atoi(e) : http_port == 80 ? 443 : 0;
	if (P.port == 0) {
		strcpy(P.error,
		       "Secure portal disabled for this listener "
		       "configuration");
		return;
	}
	if (P.port < 1 || P.port > 65535) {
		strcpy(P.error, "Invalid secure portal port");
		return;
	}
#if MG_TLS != MG_TLS_NONE
	portal_try_start(NULL);
	mg_timer_add(mgr, 5000, MG_TIMER_REPEAT, portal_try_start, NULL);
#else
	(void)mgr;
	(void)fn;
	(void)data;
	strcpy(P.error, "This build has no server TLS support");
#endif
}
#if MG_TLS != MG_TLS_NONE
static unsigned handshakes(struct mg_mgr *mgr)
{
	unsigned count = 0;
	for (struct mg_connection *p = mgr->conns; p; p = p->next)
		if (p->is_tls_hs && !p->is_closing)
			count++;
	return count;
}

static void start_tls(struct mg_connection *c)
{
	c->is_full = 0;
	c->data[1] = 0;
	struct mg_tls_opts opts = {0};
	opts.cert = mg_str(P.cert);
	opts.key = mg_str(P.key);
	mg_tls_init(c, &opts);
	if (c->tls) {
		struct mg_tls *tls = c->tls;
		if (SSL_set_cipher_list(tls->ssl,
					"ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-"
					"ECDSA-AES256-GCM-SHA384") != 1)
			c->is_closing = 1;
	}
}
#endif
int portal_event(struct mg_connection *c, int ev, void *data)
{
	(void)data;
	/* Mongoose copies the wildcard listener address into accepted sockets.
	 * Resolve the actual destination once so ordinary Host validation does
	 * not enumerate interfaces for every native or browser request. */
	if (ev == MG_EV_ACCEPT && !c->rem.is_ip6) {
		struct sockaddr_in address;
		socklen_t length = sizeof address;
		if (!getsockname((int)(uintptr_t)c->fd,
				 (struct sockaddr *)&address,
				 &length) &&
		    address.sin_family == AF_INET) {
			memcpy(c->loc.ip, &address.sin_addr, 4);
			c->loc.port = address.sin_port;
		}
	}
#if MG_TLS != MG_TLS_NONE
	if (ev == MG_EV_ACCEPT && P.available &&
	    mg_ntohs(c->loc.port) == P.port) {
		c->data[0] = 1;
		unsigned active = 0;
		for (struct mg_connection *p = c->mgr->conns; p; p = p->next)
			if (!p->is_listening && p->data[0])
				active++;
		if (active > 12) {
			c->is_closing = 1;
			return 1;
		}
		int slot = -1;
		for (unsigned i = 0; i < 24; i++)
			if (!P.ids[i]) {
				slot = (int)i;
				break;
			}
		if (slot < 0) {
			c->is_closing = 1;
			return 1;
		}
		P.ids[slot] = c->id;
		P.started[slot] = mg_millis();
		/* Queue accepted sockets without a TLS allocation. Do not read
		 * their ClientHello as HTTP while waiting for one of the two
		 * handshake slots. */
		if (handshakes(c->mgr) >= 2) {
			c->is_full = 1;
			c->data[1] = 1;
		} else
			start_tls(c);
	}
	if (c->data[0] && ev == MG_EV_READ &&
	    c->recv.len > 2u * 1024u * 1024u) {
		c->is_closing = 1;
		return 1;
	}
	if (ev == MG_EV_POLL || ev == MG_EV_CLOSE || ev == MG_EV_HTTP_MSG ||
	    ev == MG_EV_READ || ev == MG_EV_WRITE)
		for (unsigned i = 0; i < 24; i++)
			if (P.ids[i] == c->id) {
				if (ev == MG_EV_CLOSE)
					P.ids[i] = 0;
				else if (ev == MG_EV_HTTP_MSG ||
				         ((ev == MG_EV_READ || ev == MG_EV_WRITE) &&
				          !c->is_tls_hs && !c->data[1]))
					P.started[i] = mg_millis();
				else if ((c->is_tls_hs || c->data[1]) &&
					 mg_millis() - P.started[i] > 10000)
					c->is_closing = 1;
				else if (c->data[1] && handshakes(c->mgr) < 2)
					start_tls(c);
				else if (!c->is_tls_hs && !c->data[1] &&
					 mg_millis() - P.started[i] > 60000)
					c->is_closing = 1;
			}
#endif
	return 0;
}

int portal_handle(struct mg_connection *c,
		  struct mg_http_message *m,
		  int internal)
{
	int reset =
		mg_strcmp(
			m->uri,
			mg_str("/api/internal/web-access/certificate-reset")) ==
		0;
	if (reset) {
		if (!internal || mg_strcmp(m->method, mg_str("POST"))) {
			mg_http_reply(c, 403, "", "{}");
			return 1;
		}
#if MG_TLS != MG_TLS_NONE
		int ok = rotate_identity();
		if (ok && !P.available && P.port > 0 && P.port <= 65535) {
			char url[64];
			snprintf(url, sizeof url, "http://0.0.0.0:%d", P.port);
			struct mg_connection *listener =
				mg_http_listen(G.mgr, url, G.fn, G.data);
			if (listener) {
				listener->data[0] = 1;
				P.available = 1;
				P.error[0] = 0;
			} else {
				strcpy(P.error,
				       "Certificate reset; secure portal port "
				       "is unavailable");
			}
		}
		if (ok)
			for (struct mg_connection *peer = c->mgr->conns; peer;
			     peer = peer->next)
				if (peer->is_tls && !peer->is_listening)
					peer->is_closing = 1;
		mg_http_reply(c,
			      ok ? 200 : 409,
			      "Content-Type: "
			      "application/json\r\nCache-Control: no-store\r\n",
			      ok ? "{\"ok\":true}"
				 : "{\"error\":\"Identity rotation failed; "
				   "existing identity retained. Check secure "
				   "storage and entropy.\"}");
#else
		mg_http_reply(c, 503, "", "{}");
#endif
		return 1;
	}
	int native =
		mg_strcmp(m->uri,
			  mg_str("/api/internal/web-access/certificate")) == 0;
	if (!native && mg_strcmp(m->uri, mg_str("/api/secure-portal")))
		return 0;
	if (native && !internal) {
		mg_http_reply(c, 403, "", "{}");
		return 1;
	}
	if (mg_strcmp(m->method, mg_str("GET")) &&
	    !(native && !mg_strcmp(m->method, mg_str("POST")))) {
		mg_http_reply(c, 405, "", "{}");
		return 1;
	}
	/* Fingerprint shown in a browser is informational only; physical
	 * verification is mandatory. */
	mg_http_reply(
		c,
		200,
		"Content-Type: application/json\r\nCache-Control: no-store\r\n",
		"{\"available\":%s,\"secure\":%s,\"port\":%d,\"hostname\":%m,"
		"\"fingerprint\":%m,\"error\":%m}",
		P.available ? "true" : "false",
		c->is_tls ? "true" : "false",
		P.port,
		MG_ESC(P.hostname),
		MG_ESC(P.fingerprint),
		MG_ESC(P.error));
	return 1;
}

int portal_https_port(void)
{
	return P.available ? P.port : 0;
}
