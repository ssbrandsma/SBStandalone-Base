/* wolfSSL's generated options undefine feature macros; restore libc declarations. */
#include <wolfssl/options.h>
#include <wolfssl/version.h>
#if LIBWOLFSSL_VERSION_HEX < 0x05009004
#error "Portal TLS requires wolfSSL 5.9.4 or newer; rebuild the dedicated prefix"
#endif
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
