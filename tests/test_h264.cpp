#include "test_framework.h"

#include "codec/h264.h"

#include <cstring>
#include <vector>

using namespace ocp;

namespace {

std::vector<uint8_t> hex(const char *s)
{
	std::vector<uint8_t> out;
	size_t len = strlen(s);
	for (size_t i = 0; i + 1 < len; i += 2) {
		auto nib = [](char c) -> uint8_t {
			if (c >= '0' && c <= '9')
				return (uint8_t)(c - '0');
			return (uint8_t)(c - 'a' + 10);
		};
		out.push_back((uint8_t)((nib(s[i]) << 4) | nib(s[i + 1])));
	}
	return out;
}

}

TEST(sps_320x240)
{
	auto sps = hex("6742c01ed90141fb0111000000030010000000303c0f162e480");
	H264SpsInfo info;
	CHECK(parseSps(sps.data(), sps.size(), &info));
	CHECK(info.valid);
	CHECK_EQ(info.width, 320);
	CHECK_EQ(info.height, 240);
	CHECK_EQ(info.profile, 66);
	CHECK_EQ(info.level, 30);
	CHECK(info.frameMbsOnly);
	CHECK_EQ(info.codedWidth, 320);
	CHECK_EQ(info.codedHeight, 240);
}

TEST(sps_176x144)
{
	auto sps = hex("6742c01ed902c4ec0440000003004000000f03c58b92");
	H264SpsInfo info;
	CHECK(parseSps(sps.data(), sps.size(), &info));
	CHECK_EQ(info.width, 176);
	CHECK_EQ(info.height, 144);
	CHECK_EQ(info.profile, 66);
}

TEST(sps_1920x1080)
{
	// Coded 1920x1088 with frame cropping (bottom = 4 units) -> 1080.
	auto sps = hex("674d4028eca03c0113f2e022000003000200000300781e30632c");
	H264SpsInfo info;
	CHECK(parseSps(sps.data(), sps.size(), &info));
	CHECK_EQ(info.codedHeight, 1088);
	CHECK_EQ(info.width, 1920);
	CHECK_EQ(info.height, 1080);
	CHECK_EQ(info.profile, 77);
}

TEST(sps_crop)
{
	// Coded 176x144, cropped to 174x142.
	auto sps = hex("6742c00cd902c4faac0440000003004000000f03c50a92");
	H264SpsInfo info;
	CHECK(parseSps(sps.data(), sps.size(), &info));
	CHECK_EQ(info.codedWidth, 176);
	CHECK_EQ(info.codedHeight, 144);
	CHECK_EQ(info.width, 174);
	CHECK_EQ(info.height, 142);
}

TEST(sps_rejects_garbage)
{
	uint8_t junk[] = {0x67, 0x00, 0x00, 0x00};
	H264SpsInfo info;
	CHECK(!parseSps(junk, sizeof(junk), &info));
	CHECK(!parseSps(nullptr, 0, &info));

	uint8_t notSps[] = {0x65, 0x42, 0xc0, 0x1e};
	CHECK(!parseSps(notSps, sizeof(notSps), &info));
}

TEST(split_annex_b)
{
	// Three NALs with mixed 4-byte, 4-byte and 3-byte start codes.
	const uint8_t buf[] = {0, 0, 0, 1, 0x67, 0x42, 0, 0, 0, 1, 0x68, 0xce, 0, 0, 1,
			       0x65, 0x88};
	auto nals = splitAnnexB(buf, sizeof(buf));
	CHECK_EQ(nals.size(), (size_t)3);
	CHECK_EQ(nals[0].type, 7);
	CHECK_EQ(nals[0].size, (size_t)2);
	CHECK_EQ(nals[1].type, 8);
	CHECK_EQ(nals[2].type, 5);
	CHECK_EQ(nals[2].size, (size_t)2);

	const uint8_t *nal = nullptr;
	size_t nalSize = 0;
	CHECK(findNalOfType(buf, sizeof(buf), 7, &nal, &nalSize));
	CHECK(nal == buf + 4);
	CHECK(accessUnitIsKeyframe(buf, sizeof(buf)));
	CHECK(!accessUnitIsKeyframe(buf, 12));
}
