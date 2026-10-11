#include "test_framework.h"

#include "protocol/constants.h"
#include "protocol/fragmenter.h"
#include "protocol/media_packet.h"
#include "protocol/reassembler.h"

#include <cstring>
#include <string>
#include <vector>

using namespace ocp;

namespace {

MediaKeys makeMediaKeys()
{
	MediaKeys keys;
	for (size_t i = 0; i < 32; i++) {
		keys.enc[i] = uint8_t(i * 7 + 3);
		keys.mac[i] = uint8_t(i ^ 0x5a);
	}
	return keys;
}

std::vector<uint8_t> bytes(size_t n, uint8_t seed)
{
	std::vector<uint8_t> v(n);
	for (size_t i = 0; i < n; i++)
		v[i] = uint8_t(seed + i * 31);
	return v;
}

}

TEST(media_packet_round_trip)
{
	MediaKeys keys = makeMediaKeys();
	auto payload = bytes(100, 9);

	MediaHeader h;
	h.stream = MEDIA_STREAM_VIDEO;
	h.flags = MEDIA_FLAG_KEYFRAME;
	h.session_tag = 0x1122334455667788ULL;
	h.frame_id = 42;
	h.frag_idx = 0;
	h.frag_count = 1;
	h.ts_ns = 123456789ULL;

	auto packet = sealMediaPacket(keys, h, payload.data(), payload.size());
	CHECK_EQ(packet.size(), kMediaHeaderSize + payload.size() + kTagSize);
	CHECK(packet.size() <= kMaxDatagram);

	MediaHeader got;
	std::vector<uint8_t> plain;
	std::string err;
	CHECK(openMediaPacket(keys, packet.data(), packet.size(), &got, &plain, &err));
	CHECK(err.empty());
	CHECK_EQ(got.stream, h.stream);
	CHECK_EQ(got.flags, h.flags);
	CHECK_EQ(got.session_tag, h.session_tag);
	CHECK_EQ(got.frame_id, h.frame_id);
	CHECK_EQ(got.frag_count, h.frag_count);
	CHECK_EQ(got.ts_ns, h.ts_ns);
	CHECK(plain == payload);
}

TEST(media_packet_bit_flip_rejected)
{
	MediaKeys keys = makeMediaKeys();
	auto payload = bytes(64, 1);
	MediaHeader h;
	h.frame_id = 7;

	auto packet = sealMediaPacket(keys, h, payload.data(), payload.size());
	packet[kMediaHeaderSize + 5] ^= 0x01;

	MediaHeader got;
	std::vector<uint8_t> plain;
	std::string err;
	CHECK(!openMediaPacket(keys, packet.data(), packet.size(), &got, &plain, &err));
	CHECK(!err.empty());
}

TEST(media_packet_wrong_key_rejected)
{
	auto payload = bytes(32, 4);
	MediaHeader h;
	h.frame_id = 3;
	auto packet = sealMediaPacket(makeMediaKeys(), h, payload.data(), payload.size());

	MediaHeader got;
	std::vector<uint8_t> plain;
	std::string err;
	CHECK(!openMediaPacket(MediaKeys{}, packet.data(), packet.size(), &got, &plain, &err));
	CHECK(!err.empty());
}

TEST(fragment_and_reassemble_large_frame)
{
	MediaKeys keys = makeMediaKeys();
	auto payload = bytes(40000, 11);

	auto packets = fragmentAndSeal(keys, 0xaabbccdd, MEDIA_STREAM_VIDEO, 5, MEDIA_FLAG_KEYFRAME,
				       999, payload.data(), payload.size());
	CHECK(packets.size() > 10);
	for (const auto &p : packets)
		CHECK(p.size() <= kMaxDatagram);

	Reassembler re;
	std::vector<ReassembledFrame> out;
	std::string err;
	for (const auto &p : packets) {
		MediaHeader h;
		std::vector<uint8_t> frag;
		CHECK(openMediaPacket(keys, p.data(), p.size(), &h, &frag, &err));
		CHECK(re.push(h, std::move(frag), &out, &err));
	}
	CHECK(err.empty());
	CHECK_EQ(out.size(), size_t(1));
	CHECK(out[0].keyframe);
	CHECK_EQ(out[0].frameId, uint32_t(5));
	CHECK_EQ(out[0].tsNs, uint64_t(999));
	CHECK(out[0].payload == payload);
	CHECK_EQ(re.stats(MEDIA_STREAM_VIDEO).framesDelivered, uint64_t(1));
}

TEST(reassembler_out_of_order_and_loss)
{
	MediaKeys keys = makeMediaKeys();
	Reassembler re;
	std::vector<ReassembledFrame> out;
	std::string err;

	auto send = [&](uint32_t frameId, size_t size) {
		auto payload = bytes(size, uint8_t(frameId));
		auto packets = fragmentAndSeal(keys, 1, MEDIA_STREAM_VIDEO, frameId, 0, frameId,
					       payload.data(), payload.size());
		for (const auto &p : packets) {
			MediaHeader h;
			std::vector<uint8_t> frag;
			CHECK(openMediaPacket(keys, p.data(), p.size(), &h, &frag, &err));
			CHECK(re.push(h, std::move(frag), &out, &err));
		}
	};

	send(1, 2500);
	send(3, 1500);
	CHECK(err.empty());

	bool saw1 = false, saw3 = false;
	for (const auto &f : out) {
		if (f.frameId == 1)
			saw1 = true;
		if (f.frameId == 3)
			saw3 = true;
	}
	CHECK(saw1);
	CHECK(saw3);
	CHECK_EQ(re.stats(MEDIA_STREAM_VIDEO).framesDelivered, uint64_t(2));
	CHECK(re.stats(MEDIA_STREAM_VIDEO).framesLost >= 1);
}

TEST(reassembler_interleaved_streams)
{
	MediaKeys keys = makeMediaKeys();
	Reassembler re;
	std::vector<ReassembledFrame> out;
	std::string err;

	auto send = [&](uint8_t stream, uint32_t frameId, size_t size) {
		auto payload = bytes(size, uint8_t(frameId + stream));
		auto packets = fragmentAndSeal(keys, 2, stream, frameId, 0, frameId * 1000,
					       payload.data(), payload.size());
		for (const auto &p : packets) {
			MediaHeader h;
			std::vector<uint8_t> frag;
			CHECK(openMediaPacket(keys, p.data(), p.size(), &h, &frag, &err));
			CHECK(re.push(h, std::move(frag), &out, &err));
		}
	};

	for (uint32_t i = 1; i <= 5; i++) {
		send(MEDIA_STREAM_VIDEO, i, 1200);
		send(MEDIA_STREAM_AUDIO, i, 960);
	}

	CHECK(err.empty());
	uint64_t video = 0, audio = 0;
	for (const auto &f : out) {
		if (f.stream == MEDIA_STREAM_VIDEO)
			video++;
		else
			audio++;
	}
	CHECK_EQ(video, uint64_t(5));
	CHECK_EQ(audio, uint64_t(5));
	CHECK_EQ(re.stats(MEDIA_STREAM_VIDEO).framesLost, uint64_t(0));
	CHECK_EQ(re.stats(MEDIA_STREAM_AUDIO).framesLost, uint64_t(0));
}

TEST(reassembler_stale_frame_dropped)
{
	MediaKeys keys = makeMediaKeys();
	Reassembler re;
	std::vector<ReassembledFrame> out;
	std::string err;

	auto payload = bytes(500, 3);
	auto packets = fragmentAndSeal(keys, 9, MEDIA_STREAM_VIDEO, 1, 0, 1, payload.data(),
				       payload.size());
	for (const auto &p : packets) {
		MediaHeader h;
		std::vector<uint8_t> frag;
		CHECK(openMediaPacket(keys, p.data(), p.size(), &h, &frag, &err));
		CHECK(re.push(h, std::move(frag), &out, &err));
	}
	CHECK_EQ(out.size(), size_t(1));

	for (const auto &p : packets) {
		MediaHeader h;
		std::vector<uint8_t> frag;
		CHECK(openMediaPacket(keys, p.data(), p.size(), &h, &frag, &err));
		CHECK(re.push(h, std::move(frag), &out, &err));
	}
	CHECK_EQ(out.size(), size_t(1));
	CHECK(re.stats(MEDIA_STREAM_VIDEO).packetsDropped > 0);
}
