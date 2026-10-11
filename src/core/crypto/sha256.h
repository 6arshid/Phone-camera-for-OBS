#pragma once

#include <cstddef>
#include <cstdint>

namespace ocp {

class Sha256 {
public:
	Sha256();
	void init();
	void update(const uint8_t *data, size_t len);
	void final(uint8_t out[32]);

	static void hash(const uint8_t *data, size_t len, uint8_t out[32]);

private:
	void processBlock(const uint8_t block[64]);

	uint32_t state[8];
	uint64_t bitLen;
	uint8_t buffer[64];
	size_t bufferLen;
};

}
