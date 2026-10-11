#pragma once

#include <cstddef>
#include <cstdint>

namespace ocp {

bool pbkdf2Sha256(const uint8_t *password, size_t passwordLen, const uint8_t *salt, size_t saltLen,
		  uint32_t iterations, uint8_t *out, size_t outLen);

}
