#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace ocp {

std::string toHex(const uint8_t *data, size_t len);
bool fromHex(std::string_view text, uint8_t *out, size_t outLen);

}
