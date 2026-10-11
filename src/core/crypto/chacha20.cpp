#include "crypto/chacha20.h"

#include <cstring>

namespace ocp {

namespace {

inline uint32_t rotl32(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

inline uint32_t load32le(const uint8_t *p)
{
	return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
	       (uint32_t(p[3]) << 24);
}

inline void store32le(uint8_t *p, uint32_t v)
{
	p[0] = uint8_t(v);
	p[1] = uint8_t(v >> 8);
	p[2] = uint8_t(v >> 16);
	p[3] = uint8_t(v >> 24);
}

#define QUARTER_ROUND(a, b, c, d)          \
	do {                               \
		a += b; d ^= a; d = rotl32(d, 16); \
		c += d; b ^= c; b = rotl32(b, 12); \
		a += b; d ^= a; d = rotl32(d, 8);  \
		c += d; b ^= c; b = rotl32(b, 7);  \
	} while (0)

void block(const uint32_t in[16], uint8_t out[64])
{
	uint32_t x[16];
	memcpy(x, in, sizeof(x));
	for (int i = 0; i < 10; i++) {
		QUARTER_ROUND(x[0], x[4], x[8], x[12]);
		QUARTER_ROUND(x[1], x[5], x[9], x[13]);
		QUARTER_ROUND(x[2], x[6], x[10], x[14]);
		QUARTER_ROUND(x[3], x[7], x[11], x[15]);
		QUARTER_ROUND(x[0], x[5], x[10], x[15]);
		QUARTER_ROUND(x[1], x[6], x[11], x[12]);
		QUARTER_ROUND(x[2], x[7], x[8], x[13]);
		QUARTER_ROUND(x[3], x[4], x[9], x[14]);
	}
	for (int i = 0; i < 16; i++)
		store32le(out + i * 4, x[i] + in[i]);
}

#undef QUARTER_ROUND

}

void chacha20Xor(const uint8_t key[32], const uint8_t nonce[12], uint32_t counter, const uint8_t *in,
		 uint8_t *out, size_t len)
{
	uint32_t state[16];
	state[0] = 0x61707865;
	state[1] = 0x3320646e;
	state[2] = 0x79622d32;
	state[3] = 0x6b206574;
	for (int i = 0; i < 8; i++)
		state[4 + i] = load32le(key + i * 4);
	state[12] = counter;
	state[13] = load32le(nonce);
	state[14] = load32le(nonce + 4);
	state[15] = load32le(nonce + 8);

	uint8_t keystream[64];
	size_t offset = 0;
	while (offset < len) {
		block(state, keystream);
		state[12]++;

		size_t take = 64;
		if (offset + take > len)
			take = len - offset;
		for (size_t i = 0; i < take; i++)
			out[offset + i] = in[offset + i] ^ keystream[i];
		offset += take;
	}

	memset(keystream, 0, sizeof(keystream));
	memset(state, 0, sizeof(state));
}

}
