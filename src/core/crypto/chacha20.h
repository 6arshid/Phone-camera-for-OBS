#pragma once

#include <cstddef>
#include <cstdint>

namespace ocp {

void chacha20Xor(const uint8_t key[32], const uint8_t nonce[12], uint32_t counter, const uint8_t *in,
		 uint8_t *out, size_t len);

}
