#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace ocp {

bool randomBytes(uint8_t *out, size_t len);
std::string randomHex(size_t bytes);
uint32_t randomUint32();

}
