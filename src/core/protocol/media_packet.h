#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace ocp {

struct MediaKeys {
	std::array<uint8_t, 32> enc{};
	std::array<uint8_t, 32> mac{};
};

enum MediaStream : uint8_t {
	MEDIA_STREAM_VIDEO = 0,
	MEDIA_STREAM_AUDIO = 1,
};

enum MediaFlags : uint8_t {
	MEDIA_FLAG_KEYFRAME = 1,
};

struct MediaHeader {
	uint8_t stream = MEDIA_STREAM_VIDEO;
	uint8_t flags = 0;
	uint64_t session_tag = 0;
	uint32_t frame_id = 0;
	uint16_t frag_idx = 0;
	uint16_t frag_count = 1;
	uint64_t ts_ns = 0;
};

std::vector<uint8_t> sealMediaPacket(const MediaKeys &keys, const MediaHeader &header,
				     const uint8_t *plain, size_t plainLen);

bool openMediaPacket(const MediaKeys &keys, const uint8_t *data, size_t len, MediaHeader *header,
		     std::vector<uint8_t> *plain, std::string *error);

}
