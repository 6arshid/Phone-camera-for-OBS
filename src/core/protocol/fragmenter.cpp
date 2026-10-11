#include "protocol/fragmenter.h"

#include "protocol/constants.h"

namespace ocp {

std::vector<std::vector<uint8_t>> fragmentAndSeal(const MediaKeys &keys, uint64_t sessionTag,
						  uint8_t stream, uint32_t frameId, uint8_t flags,
						  uint64_t tsNs, const uint8_t *payload,
						  size_t payloadLen)
{
	std::vector<std::vector<uint8_t>> packets;
	if (payloadLen == 0) {
		MediaHeader h;
		h.stream = stream;
		h.flags = flags;
		h.session_tag = sessionTag;
		h.frame_id = frameId;
		h.frag_idx = 0;
		h.frag_count = 1;
		h.ts_ns = tsNs;
		packets.push_back(sealMediaPacket(keys, h, nullptr, 0));
		return packets;
	}

	size_t fragCount = (payloadLen + kMediaCipherMax - 1) / kMediaCipherMax;
	if (fragCount > 0xffff)
		fragCount = 0xffff;

	packets.reserve(fragCount);
	size_t offset = 0;
	for (size_t i = 0; i < fragCount; i++) {
		size_t take = payloadLen - offset;
		if (take > kMediaCipherMax)
			take = kMediaCipherMax;

		MediaHeader h;
		h.stream = stream;
		h.flags = flags;
		h.session_tag = sessionTag;
		h.frame_id = frameId;
		h.frag_idx = uint16_t(i);
		h.frag_count = uint16_t(fragCount);
		h.ts_ns = tsNs;

		packets.push_back(sealMediaPacket(keys, h, payload + offset, take));
		offset += take;
	}
	return packets;
}

}
