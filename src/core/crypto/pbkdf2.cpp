#include "crypto/pbkdf2.h"

#include "crypto/hmac.h"

#include <cstring>

namespace ocp {

bool pbkdf2Sha256(const uint8_t *password, size_t passwordLen, const uint8_t *salt, size_t saltLen,
		  uint32_t iterations, uint8_t *out, size_t outLen)
{
	if (!out || outLen == 0 || iterations == 0)
		return false;
	if (!salt && saltLen)
		return false;
	if (!password && passwordLen)
		return false;

	uint32_t blockCount = uint32_t((outLen + 31) / 32);
	for (uint32_t block = 1; block <= blockCount; block++) {
		uint8_t u[32];
		uint8_t idx[4] = {uint8_t(block >> 24), uint8_t(block >> 16), uint8_t(block >> 8),
				  uint8_t(block)};

		HmacSha256 h;
		h.init(password, passwordLen);
		if (saltLen)
			h.update(salt, saltLen);
		h.update(idx, 4);
		h.final(u);

		uint8_t acc[32];
		memcpy(acc, u, sizeof(acc));

		for (uint32_t i = 1; i < iterations; i++) {
			uint8_t next[32];
			HmacSha256::oneShot(password, passwordLen, u, sizeof(u), next);
			memcpy(u, next, sizeof(u));
			for (size_t j = 0; j < sizeof(acc); j++)
				acc[j] ^= u[j];
		}

		size_t take = sizeof(acc);
		size_t offset = size_t(block - 1) * 32;
		if (offset + take > outLen)
			take = outLen - offset;
		memcpy(out + offset, acc, take);

		memset(u, 0, sizeof(u));
		memset(acc, 0, sizeof(acc));
	}
	return true;
}

}
