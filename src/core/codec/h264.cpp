#include "codec/h264.h"

namespace ocp {

namespace {

class BitReader {
public:
	BitReader(const uint8_t *nal, size_t size)
	{
		// Remove emulation prevention bytes (00 00 03 -> 00 00).
		rbsp.reserve(size);
		for (size_t i = 0; i < size; i++) {
			if (i + 2 < size && nal[i] == 0 && nal[i + 1] == 0 && nal[i + 2] == 3) {
				rbsp.push_back(0);
				rbsp.push_back(0);
				i += 2;
			} else {
				rbsp.push_back(nal[i]);
			}
		}
	}

	bool ok() const { return good; }
	size_t bitsLeft() const
	{
		return rbsp.size() * 8 > bitPos ? rbsp.size() * 8 - bitPos : 0;
	}

	uint32_t u(int count)
	{
		uint32_t value = 0;
		for (int i = 0; i < count; i++) {
			if (bitPos >= rbsp.size() * 8) {
				good = false;
				return 0;
			}
			value = (value << 1) | ((rbsp[bitPos >> 3] >> (7 - (bitPos & 7))) & 1);
			bitPos++;
		}
		return value;
	}

	uint32_t ue()
	{
		int zeros = 0;
		while (true) {
			if (bitPos >= rbsp.size() * 8 || zeros > 31) {
				good = false;
				return 0;
			}
			uint32_t bit = (rbsp[bitPos >> 3] >> (7 - (bitPos & 7))) & 1;
			bitPos++;
			if (bit)
				break;
			zeros++;
		}
		uint32_t suffix = 0;
		if (zeros > 0)
			suffix = u(zeros);
		return ((1u << zeros) - 1) + suffix;
	}

	int32_t se()
	{
		uint32_t codeNum = ue();
		if (!good)
			return 0;
		if (codeNum & 1)
			return (int32_t)((codeNum + 1) / 2);
		return -(int32_t)(codeNum / 2);
	}

private:
	std::vector<uint8_t> rbsp;
	size_t bitPos = 0;
	bool good = true;
};

bool isHighProfile(int profile)
{
	switch (profile) {
	case 44:
	case 83:
	case 86:
	case 100:
	case 110:
	case 118:
	case 122:
	case 128:
	case 134:
	case 135:
	case 138:
	case 139:
	case 244:
		return true;
	default:
		return false;
	}
}

}

std::vector<AnnexBNal> splitAnnexB(const uint8_t *data, size_t size)
{
	std::vector<AnnexBNal> nals;
	if (!data || size < 4)
		return nals;

	auto startCodeLen = [&](size_t i) -> int {
		if (i + 3 < size && data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 0 &&
		    data[i + 3] == 1)
			return 4;
		if (i + 2 < size && data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1)
			return 3;
		return 0;
	};

	size_t i = 0;
	while (i + 3 < size) {
		int sc = startCodeLen(i);
		if (sc) {
			size_t nalStart = i + (size_t)sc;
			size_t j = nalStart;
			int nextSc = 0;
			while (j < size) {
				nextSc = startCodeLen(j);
				if (nextSc)
					break;
				j++;
			}
			if (j > nalStart) {
				AnnexBNal nal;
				nal.data = data + nalStart;
				nal.size = j - nalStart;
				nal.type = nal.data[0] & 0x1f;
				nals.push_back(nal);
			}
			i = j;
		} else {
			i++;
		}
	}
	return nals;
}

bool parseSps(const uint8_t *nal, size_t size, H264SpsInfo *out)
{
	if (!nal || !out || size < 4)
		return false;
	if ((nal[0] & 0x1f) != 7)
		return false;

	*out = H264SpsInfo{};
	BitReader br(nal + 1, size - 1);

	out->profile = (int)br.u(8);
	br.u(8); // constraint flags + reserved bits
	out->level = (int)br.u(8);
	br.ue(); // seq_parameter_set_id

	if (isHighProfile(out->profile)) {
		uint32_t chroma = br.ue();
		if (chroma > 3)
			return false;
		out->chromaFormatIdc = (int)chroma;
		if (chroma == 3)
			br.u(1); // separate_colour_plane_flag
		br.ue(); // bit_depth_luma_minus8
		br.ue(); // bit_depth_chroma_minus8
		br.u(1); // qpprime_y_zero_transform_bypass_flag
		if (br.u(1)) { // seq_scaling_matrix_present_flag
			int lists = chroma != 3 ? 8 : 12;
			for (int i = 0; i < lists; i++) {
				if (!br.u(1))
					continue;
				size_t lastScale = 8, nextScale = 8;
				for (size_t j = 0; j < 16; j++) {
					if (nextScale != 0) {
						int32_t delta = br.se();
						nextScale = (lastScale + (size_t)delta + 256) % 256;
					}
					lastScale = nextScale == 0 ? lastScale : nextScale;
				}
			}
		}
	}

	br.ue(); // log2_max_frame_num_minus4
	out->picOrderCntType = (int)br.ue();
	if (out->picOrderCntType == 0) {
		br.ue(); // log2_max_pic_order_cnt_lsb_minus4
	} else if (out->picOrderCntType == 1) {
		br.u(1); // delta_pic_order_always_zero_flag
		br.se();
		br.se();
		uint32_t count = br.ue();
		if (count > 255)
			return false;
		for (uint32_t i = 0; i < count; i++)
			br.se();
	} else if (out->picOrderCntType > 2) {
		return false;
	}

	br.ue(); // max_num_ref_frames
	br.u(1); // gaps_in_frame_num_value_allowed_flag

	uint32_t widthInMbs = br.ue() + 1;
	uint32_t heightInMapUnits = br.ue() + 1;
	if (!br.ok() || widthInMbs > 4096 || heightInMapUnits > 4096)
		return false;

	if (!br.u(1)) { // frame_mbs_only_flag
		out->frameMbsOnly = false;
		br.u(1); // mb_adaptive_frame_field_flag
	} else {
		out->frameMbsOnly = true;
	}
	br.u(1); // direct_8x8_inference_flag

	int cropLeft = 0, cropRight = 0, cropTop = 0, cropBottom = 0;
	if (br.u(1)) { // frame_cropping_flag
		cropLeft = (int)br.ue();
		cropRight = (int)br.ue();
		cropTop = (int)br.ue();
		cropBottom = (int)br.ue();
		if (!br.ok())
			return false;
	}

	int chroma = out->chromaFormatIdc;
	int cropUnitX = (chroma == 1 || chroma == 2) ? 2 : 1;
	int cropUnitY = 1;
	if (chroma == 1)
		cropUnitY = 2;
	else if (chroma == 2)
		cropUnitY = 1;
	if (!out->frameMbsOnly)
		cropUnitY *= 2;

	out->codedWidth = (int)(widthInMbs * 16);
	out->codedHeight = (int)(heightInMapUnits * 16 * (out->frameMbsOnly ? 1 : 2));
	out->width = out->codedWidth - (cropLeft + cropRight) * cropUnitX;
	out->height = out->codedHeight - (cropTop + cropBottom) * cropUnitY;

	if (out->width <= 0 || out->height <= 0 || (out->width & 1) || (out->height & 1))
		return false;
	out->valid = true;
	return true;
}

bool findNalOfType(const uint8_t *data, size_t size, int type, const uint8_t **nal,
		   size_t *nalSize)
{
	for (const AnnexBNal &n : splitAnnexB(data, size)) {
		if (n.type == type) {
			if (nal)
				*nal = n.data;
			if (nalSize)
				*nalSize = n.size;
			return true;
		}
	}
	return false;
}

bool accessUnitIsKeyframe(const uint8_t *data, size_t size)
{
	for (const AnnexBNal &n : splitAnnexB(data, size)) {
		if (n.type == 5)
			return true;
	}
	return false;
}

}
