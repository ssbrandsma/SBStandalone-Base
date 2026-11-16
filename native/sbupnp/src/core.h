#ifndef SBUPNP_CORE_H
#define SBUPNP_CORE_H
#include <stddef.h>
#include <stdint.h>
#include <ixml.h>
#define SBUPNP_XML_MAX 262144u
#define SBUPNP_PAGE_MAX 50u
#define SBUPNP_SERVERS_MAX 32u
#define SBUPNP_URL_MAX 2048u
typedef struct {
 char udn[256], name[256], type[128], location[SBUPNP_URL_MAX];
 char service[128], control[SBUPNP_URL_MAX], event[SBUPNP_URL_MAX];
 uint64_t expires, generation;
} sbupnp_server;
typedef struct {
 char id[512], parent[512], title[256], artist[256], album[256], genre[128], class_name[128];
 char artwork[SBUPNP_URL_MAX], uri[SBUPNP_URL_MAX], protocol[256];
 uint64_t duration_ms, size;
 unsigned bitrate, rate, channels, track;
 int container, searchable, playable;
} sbupnp_item;
typedef struct { sbupnp_server servers[SBUPNP_SERVERS_MAX]; uint64_t revision; } sbupnp_registry;
/* Numeric IPv4 HTTP only. Every fetched/selected URL must share the advertised
 * server's address. No DNS, credentials, fragments, loopback, public IP or proxy. */
int sbupnp_url(const char *url, const char *origin);
int sbupnp_resolve(const char *base, const char *relative, char *out, size_t cap);
IXML_Document *sbupnp_xml(const char *, size_t);
int sbupnp_description(const char *, size_t, const char *location, const char *udn, sbupnp_server *);
int sbupnp_didl(const char *, size_t, const char *origin, sbupnp_item *, unsigned capacity, unsigned *count);
int sbupnp_search(const char *caps, const char *field, const char *text, char *, size_t);
int sbupnp_capability(const char *caps, const char *field);
int sbupnp_registry_put(sbupnp_registry *, const sbupnp_server *, uint64_t now, unsigned ttl);
void sbupnp_registry_remove(sbupnp_registry *, const char *udn);
void sbupnp_registry_expire(sbupnp_registry *, uint64_t now);
const char *sbupnp_text(IXML_Node *);
IXML_Node *sbupnp_child(IXML_Node *, const char *local);
#endif
