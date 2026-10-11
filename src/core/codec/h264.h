#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ocp {

struct H264SpsInfo {
	int profile = 0;
	int level = 0;
	int width = 0;
	int height = 0;
	int codedWidth = 0;
	int codedHeight = 0;
	int picOrderCntType = -1;
	int chromaFormatIdc = 1;
	bool frameMbsOnly = true;
	bool valid = false;
};

struct AnnexBNal {
	const uint8_t *data = nullptr;
	size_t size = 0;
	int type = 0;
};

// Splits an Annex-B buffer (start codes 00 00 01 / 00 00 00 01) into NAL
// units. Offsets point into the input buffer.
std::vector<AnnexBNal> splitAnnexB(const uint8_t *data, size_t size);

// Parses a sequence parameter set NAL (including the NAL header byte).
bool parseSps(const uint8_t *nal, size_t size, H264SpsInfo *out);

// Returns the first NAL of the requested type (7 = SPS, 8 = PPS, 5 = IDR).
bool findNalOfType(const uint8_t *data, size_t size, int type, const uint8_t **nal,
		   size_t *nalSize);

// True when the access unit contains an IDR picture.
bool accessUnitIsKeyframe(const uint8_t *data, size_t size);

}
