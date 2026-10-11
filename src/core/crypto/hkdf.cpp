#include "crypto/hkdf.h"

#include "crypto/hmac.h"

#include <cstring>

namespace ocp {

bool hkdfSha256(const uint8_t *salt, size_t saltLen, const uint8_t *ikm, size_t ikmLen,
		const uint8_t *info, size_t infoLen, uint8_t *okm, size_t okmLen)
{
	if (!okm || okmLen == 0 || !ikm || ikmLen == 0)
		return false;
	if (okmLen > 255 * 32)
		return false;

	uint8_t defaultSalt[32] = {0};
	const uint8_t *saltPtr = salt;
	size_t saltLenUsed = saltLen;
	if (!saltPtr || saltLenUsed == 0) {
		saltPtr = defaultSalt;
		saltLenUsed = sizeof(defaultSalt);
	}

	uint8_t prk[32];
	HmacSha256::oneShot(saltPtr, saltLenUsed, ikm, ikmLen, prk);

	uint8_t t[32] = {0};
	size_t tLen = 0;
	size_t offset = 0;
	uint8_t counter = 1;
	while (offset < okmLen) {
		HmacSha256 h;
		h.init(prk, sizeof(prk));
		if (tLen)
			h.update(t, tLen);
		if (info && infoLen)
			h.update(info, infoLen);
		h.update(&counter, 1);
		uint8_t block[32];
		h.final(block);

		size_t take = sizeof(block);
		if (offset + take > okmLen)
			take = okmLen - offset;
		memcpy(okm + offset, block, take);
		offset += take;
		memcpy(t, block, sizeof(t));
		tLen = sizeof(t);
		counter++;
	}

	memset(prk, 0, sizeof(prk));
	memset(t, 0, sizeof(t));
	return true;
}

}
