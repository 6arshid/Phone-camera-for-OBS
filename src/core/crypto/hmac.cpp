#include "crypto/hmac.h"

#include <cstring>

namespace ocp {

void HmacSha256::init(const uint8_t *key, size_t keyLen)
{
	uint8_t k[64];
	memset(k, 0, sizeof(k));
	if (keyLen > 64) {
		Sha256::hash(key, keyLen, k);
	} else {
		memcpy(k, key, keyLen);
	}

	uint8_t ipad[64];
	for (size_t i = 0; i < 64; i++) {
		ipad[i] = k[i] ^ 0x36;
		opadKey[i] = k[i] ^ 0x5c;
	}

	inner.init();
	inner.update(ipad, 64);
	memset(k, 0, sizeof(k));
}

void HmacSha256::update(const uint8_t *data, size_t len) { inner.update(data, len); }

void HmacSha256::final(uint8_t out[32])
{
	uint8_t innerHash[32];
	inner.final(innerHash);

	Sha256 outer;
	outer.update(opadKey, 64);
	outer.update(innerHash, 32);
	outer.final(out);

	memset(innerHash, 0, sizeof(innerHash));
	memset(opadKey, 0, sizeof(opadKey));
}

void HmacSha256::oneShot(const uint8_t *key, size_t keyLen, const uint8_t *msg, size_t msgLen,
			 uint8_t out[32])
{
	HmacSha256 h;
	h.init(key, keyLen);
	h.update(msg, msgLen);
	h.final(out);
}

}
