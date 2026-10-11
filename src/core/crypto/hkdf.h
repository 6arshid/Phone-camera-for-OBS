#pragma once

#include <cstddef>
#include <cstdint>

namespace ocp {

bool hkdfSha256(const uint8_t *salt, size_t saltLen, const uint8_t *ikm, size_t ikmLen,
		const uint8_t *info, size_t infoLen, uint8_t *okm, size_t okmLen);

}
