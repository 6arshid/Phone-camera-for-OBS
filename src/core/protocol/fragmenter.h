#pragma once

#include "protocol/media_packet.h"

#include <cstdint>
#include <vector>

namespace ocp {

std::vector<std::vector<uint8_t>> fragmentAndSeal(const MediaKeys &keys, uint64_t sessionTag,
						  uint8_t stream, uint32_t frameId, uint8_t flags,
						  uint64_t tsNs, const uint8_t *payload,
						  size_t payloadLen);

}
