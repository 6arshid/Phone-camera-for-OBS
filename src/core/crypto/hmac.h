#pragma once

#include "crypto/sha256.h"

#include <cstddef>
#include <cstdint>

namespace ocp {

class HmacSha256 {
public:
	HmacSha256() = default;
	void init(const uint8_t *key, size_t keyLen);
	void update(const uint8_t *data, size_t len);
	void final(uint8_t out[32]);

	static void oneShot(const uint8_t *key, size_t keyLen, const uint8_t *msg, size_t msgLen,
			    uint8_t out[32]);

private:
	Sha256 inner;
	uint8_t opadKey[64];
};

}
