#ifndef SB_SHA256_H
#define SB_SHA256_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
/* Existing storage-helper SHA-256, shared without algorithm changes. */
typedef struct {
	uint32_t h[8];
	uint64_t bits;
	unsigned char block[64];
	size_t used;
} sha256_t;

/**
 * @brief Handle rr.
 * @param x Input value for x.
 * @param n Capacity or item count associated with the operation.
 * @return Value produced by the operation.
 */
static inline uint32_t rr(uint32_t x, unsigned n)
{
	return (x >> n) | (x << (32 - n));
}

/**
 * @brief Handle sha block.
 * @param c Object referenced by c.
 * @param b Input text for b.
 */
static inline void sha_block(sha256_t *c, const unsigned char *b)
{
	static const uint32_t k[64] = {
		0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b,
		0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
		0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7,
		0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
		0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152,
		0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
		0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
		0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
		0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819,
		0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
		0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
		0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
		0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
	uint32_t w[64], a, e, f, g, h, t1, t2, x, y, z;
	int i;
	for (i = 0; i < 16; i++)
		w[i] = (uint32_t)b[i * 4] << 24 | (uint32_t)b[i * 4 + 1] << 16 |
		       (uint32_t)b[i * 4 + 2] << 8 | b[i * 4 + 3];
	for (; i < 64; i++)
		w[i] = (rr(w[i - 2], 17) ^ rr(w[i - 2], 19) ^
			(w[i - 2] >> 10)) +
		       w[i - 7] +
		       (rr(w[i - 15], 7) ^ rr(w[i - 15], 18) ^
			(w[i - 15] >> 3)) +
		       w[i - 16];
	a = c->h[0];
	x = c->h[1];
	y = c->h[2];
	z = c->h[3];
	e = c->h[4];
	f = c->h[5];
	g = c->h[6];
	h = c->h[7];
	for (i = 0; i < 64; i++) {
		t1 = h + (rr(e, 6) ^ rr(e, 11) ^ rr(e, 25)) +
		     ((e & f) ^ ((~e) & g)) + k[i] + w[i];
		t2 = (rr(a, 2) ^ rr(a, 13) ^ rr(a, 22)) +
		     ((a & x) ^ (a & y) ^ (x & y));
		h = g;
		g = f;
		f = e;
		e = z + t1;
		z = y;
		y = x;
		x = a;
		a = t1 + t2;
	}
	c->h[0] += a;
	c->h[1] += x;
	c->h[2] += y;
	c->h[3] += z;
	c->h[4] += e;
	c->h[5] += f;
	c->h[6] += g;
	c->h[7] += h;
}

/**
 * @brief Initialize sha.
 * @param c Object referenced by c.
 */
static inline void sha_init(sha256_t *c)
{
	static const uint32_t v[8] = {0x6a09e667,
				      0xbb67ae85,
				      0x3c6ef372,
				      0xa54ff53a,
				      0x510e527f,
				      0x9b05688c,
				      0x1f83d9ab,
				      0x5be0cd19};
	memset(c, 0, sizeof *c);
	memcpy(c->h, v, sizeof v);
}

/**
 * @brief Handle sha add.
 * @param c Object referenced by c.
 * @param p Input text for p.
 * @param n Capacity or item count associated with the operation.
 */
static inline void sha_add(sha256_t *c, const unsigned char *p, size_t n)
{
	c->bits += (uint64_t)n * 8;
	while (n) {
		size_t q = 64 - c->used;
		if (q > n)
			q = n;
		memcpy(c->block + c->used, p, q);
		c->used += q;
		p += q;
		n -= q;
		if (c->used == 64) {
			sha_block(c, c->block);
			c->used = 0;
		}
	}
}

/**
 * @brief Handle sha done.
 * @param c Object referenced by c.
 * @param out Destination buffer for the result.
 */
static inline void sha_done(sha256_t *c, char out[65])
{
	uint64_t bits = c->bits;
	int i;
	c->block[c->used++] = 0x80;
	if (c->used > 56) {
		while (c->used < 64)
			c->block[c->used++] = 0;
		sha_block(c, c->block);
		c->used = 0;
	}
	while (c->used < 56)
		c->block[c->used++] = 0;
	for (i = 7; i >= 0; i--)
		c->block[c->used++] = (unsigned char)(bits >> (i * 8));
	sha_block(c, c->block);
	for (i = 0; i < 8; i++)
		sprintf(out + i * 8, "%08x", c->h[i]);
	out[64] = 0;
}

#endif
