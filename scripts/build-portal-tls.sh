#!/bin/sh
# Build the pinned portal-only wolfSSL server and certificate generator.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
sdk=${SBHTTPSPROXY_ROOT:-/mnt/c/Projects/SBHttpsProxy}
toolchain=${ARM_TOOLCHAIN_DIR:-$sdk/build/toolchains/armv5-eabi--musl--stable-2020.02-2}
cmake=${CMAKE:-$sdk/build/toolchains/cmake-3.31.8-linux-x86_64/bin/cmake}
if [ -z "${WOLFSSL_SOURCE_DIR:-}" ]; then
 source=$(python3 "$root/scripts/fetch-portal-tls.py" "$root/.downloads")
else
 source=$WOLFSSL_SOURCE_DIR
fi
build=${SB_TLS_BUILD_DIR:-$root/build-portal-tls/wolfssl-5.9.4}
prefix=${ARM_PORTAL_TLS_PREFIX:-$root/build-portal-tls/prefix}
cc="$toolchain/bin/arm-buildroot-linux-musleabi-gcc"
test -x "$cc"
test -x "$cmake"
grep -q 'LIBWOLFSSL_VERSION_STRING "5.9.4"' "$source/wolfssl/version.h"
flags='-Os -marm -march=armv5te -mtune=arm926ej-s -mfloat-abi=soft -ffunction-sections -fdata-sections -DSP_INT_BITS=4096 -DHAVE_SERVER_RENEGOTIATION_INFO -DHAVE_EXTENDED_MASTER -DWOLFSSL_HAVE_SP_ECC -DWOLFSSL_SP_SMALL'
"$cmake" -S "$source" -B "$build" -DCMAKE_SYSTEM_NAME=Linux \
    -DCMAKE_SYSTEM_PROCESSOR=arm -DCMAKE_C_COMPILER="$cc" \
    -DCMAKE_INSTALL_PREFIX="$prefix" -DCMAKE_BUILD_TYPE=MinSizeRel \
    -DCMAKE_C_FLAGS="$flags" -DBUILD_SHARED_LIBS=OFF \
    -DBUILD_SP=ON -DBUILD_SP_C=ON -DWOLFSSL_MLKEM=OFF -DWOLFSSL_PQC_HYBRIDS=OFF -DWOLFSSL_EXTRA_PQC_HYBRIDS=OFF -DWOLFSSL_MLDSA=OFF -DWOLFSSL_DILITHIUM=OFF \
    -DWOLFSSL_ALT_CERT_CHAINS=ON -DWOLFSSL_CURL=ON -DWOLFSSL_TLS13=ON \
    -DWOLFSSL_OPENSSLALL=ON -DWOLFSSL_OPENSSLEXTRA=ON -DWOLFSSL_KEYGEN=ON -DWOLFSSL_CERTGEN=ON -DWOLFSSL_CERTEXT=ON -DWOLFSSL_DTLS=OFF -DWOLFSSL_EXAMPLES=OFF -DWOLFSSL_CRYPT_TESTS=OFF
"$cmake" --build "$build" -j2
"$cmake" --install "$build"
# Export only the integer capacity used for this portal library.
"$cmake" -DSB_TLS_PREFIX="$prefix" -P "$root/scripts/export-portal-tls.cmake"

cp "$source/COPYING" "$prefix/PORTAL-TLS-LICENSE"
{ printf '%s\n' 'wolfSSL=5.9.4' 'source=https://github.com/wolfSSL/wolfssl/tree/v5.9.4-stable' 'archive_sha256=82dbbbb89862c1e29cfe25259d9a1c781e9ecc5db72aeb50a535bc2b819ccbc7'; (cd "$prefix" && sha256sum lib/libwolfssl.a); } > "$prefix/portal-tls-manifest.txt"
