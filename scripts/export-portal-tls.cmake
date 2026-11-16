if(NOT EXISTS "${SB_TLS_PREFIX}/include/wolfssl/options.h")
 message(FATAL_ERROR "Missing portal wolfSSL installation")
endif()
file(APPEND "${SB_TLS_PREFIX}/include/wolfssl/options.h" "\n#ifndef SP_INT_BITS\n#define SP_INT_BITS 4096\n#elif SP_INT_BITS != 4096\n#error Incompatible portal integer capacity\n#endif\n")

file(APPEND "${SB_TLS_PREFIX}/include/wolfssl/options.h" "\n#ifndef HAVE_SERVER_RENEGOTIATION_INFO\n#define HAVE_SERVER_RENEGOTIATION_INFO\n#endif\n#ifndef HAVE_EXTENDED_MASTER\n#define HAVE_EXTENDED_MASTER\n#endif\n")

# Match the portable P-256 acceleration compiled into the static library.
file(APPEND "${SB_TLS_PREFIX}/include/wolfssl/options.h" "\n#ifndef WOLFSSL_HAVE_SP_ECC\n#define WOLFSSL_HAVE_SP_ECC\n#endif\n#ifndef WOLFSSL_SP_SMALL\n#define WOLFSSL_SP_SMALL\n#endif\n")
