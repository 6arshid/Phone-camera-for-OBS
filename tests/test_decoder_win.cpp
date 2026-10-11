#include "test_framework.h"

#include "codec/h264.h"
#include "decoder.h"

#include <cstdio>
#include <string>
#include <vector>

#ifndef OCP_TEST_DATA_DIR
#define OCP_TEST_DATA_DIR "."
#endif

using namespace ocp;
using namespace ocp::plugin;

namespace {

std::vector<uint8_t> readFile(const char *name)
{
	std::string path = std::string(OCP_TEST_DATA_DIR) + "/" + name;
	FILE *f = fopen(path.c_str(), "rb");
	if (!f)
		return {};
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	std::vector<uint8_t> data((size_t)size);
	if (size > 0)
		fread(data.data(), 1, (size_t)size, f);
	fclose(f);
	return data;
}

// Groups Annex-B NALs into access units. A new AU starts at each VCL NAL
// (slice) for single-slice-per-frame streams, which is what the fixtures use.
std::vector<std::vector<uint8_t>> groupAccessUnits(const std::vector<uint8_t> &data)
{
	std::vector<std::vector<uint8_t>> aus;
	auto nals = splitAnnexB(data.data(), data.size());
	std::vector<uint8_t> current;
	bool sawVcl = false;
	for (size_t i = 0; i < nals.size(); i++) {
		const AnnexBNal &nal = nals[i];
		size_t nalOff = (size_t)(nal.data - data.data());
		int scLen = 3;
		if (nalOff >= 4 && data[nalOff - 4] == 0 && data[nalOff - 3] == 0 &&
		    data[nalOff - 2] == 0 && data[nalOff - 1] == 1)
			scLen = 4;
		size_t auStart = nalOff - (size_t)scLen;
		bool vcl = nal.type == 1 || nal.type == 5;
		if (vcl && sawVcl) {
			aus.push_back(current);
			current.clear();
			sawVcl = false;
		}
		if (vcl)
			sawVcl = true;
		current.insert(current.end(), data.begin() + (long)auStart,
			       data.begin() + (long)(nalOff + nal.size));
	}
	if (!current.empty())
		aus.push_back(current);
	return aus;
}

}

TEST(decoder_mf_h264)
{
	if (!decoderSupported()) {
		fprintf(stderr, "[skip] no H.264 MF decoder on this system\n");
		return;
	}

	auto bitstream = readFile("test_320x240.h264");
	CHECK(!bitstream.empty());

	auto decoder = createVideoDecoder();
	std::string err;
	CHECK(decoder->configure(320, 240, 30, 300, &err));

	auto aus = groupAccessUnits(bitstream);
	if (getenv("OCP_DEC_DEBUG"))
		fprintf(stderr, "[dbg] aus=%zu firstAu=%zu\n", aus.size(),
			aus.empty() ? (size_t)0 : aus[0].size());
	CHECK(aus.size() >= 15);

	int frames = 0;
	int lastW = 0, lastH = 0;
	for (const auto &au : aus) {
		bool ok = decoder->feed(au.data(), au.size(), &err);
		if (!ok && getenv("OCP_DEC_DEBUG"))
			fprintf(stderr, "[dbg] feed failed: %s\n", err.c_str());
		CHECK(ok);
		DecodedFrame frame;
		while (decoder->take(&frame)) {
			lastW = frame.width;
			lastH = frame.height;
			size_t expected = (size_t)frame.strideY * frame.height +
					   (size_t)frame.strideUV * (frame.height / 2);
			CHECK(frame.nv12.size() >= expected);
			CHECK(frame.strideY >= frame.width);
			frames++;
		}
	}

	// The Microsoft software decoder buffers frames for up to ~1s before
	// emitting them. flush() drains the remainder.
	decoder->flush();
	DecodedFrame frame;
	while (decoder->take(&frame)) {
		lastW = frame.width;
		lastH = frame.height;
		frames++;
	}

	if (getenv("OCP_DEC_DEBUG"))
		fprintf(stderr, "[dbg] frames=%d last=%dx%d\n", frames, lastW, lastH);
	CHECK(frames >= 10);
	CHECK_EQ(lastW, 320);
	CHECK_EQ(lastH, 240);
}
