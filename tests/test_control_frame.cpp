#include "test_framework.h"

#include "pairing/pairing.h"
#include "protocol/constants.h"
#include "protocol/control_frame.h"

#include <cstring>
#include <string>
#include <vector>

using namespace ocp;

namespace {

ControlKeys makeKeys(uint8_t seed = 0, bool active = true)
{
	ControlKeys keys;
	for (size_t i = 0; i < 32; i++) {
		keys.enc[i] = uint8_t(i * 3 + 1 + seed * 17);
		keys.mac[i] = uint8_t(255 - i - seed * 13);
	}
	keys.active = active;
	return keys;
}

}

TEST(control_frame_plaintext_round_trip)
{
	ControlFrameEncoder enc;
	ControlFrameReader dec;

	std::string payload = R"({"type":"hello","proto":"ocp/1"})";
	auto frame = enc.encode(payload);

	std::vector<std::string> out;
	std::string err;
	CHECK(dec.feed(frame.data(), frame.size(), &out, &err));
	CHECK(err.empty());
	CHECK_EQ(out.size(), size_t(1));
	CHECK_STR_EQ(out[0], payload);
}

TEST(control_frame_encrypted_round_trip)
{
	ControlKeys keys = makeKeys();
	ControlFrameEncoder enc;
	enc.setKeys(keys);
	ControlFrameReader dec;
	dec.setKeys(keys);

	const std::string messages[] = {"{\"type\":\"ready\"}", "{\"type\":\"config\"}",
					std::string(1000, 'x'), "{\"type\":\"ping\",\"t\":1}"};

	std::vector<std::string> out;
	std::string err;
	for (const auto &m : messages) {
		auto frame = enc.encode(m);
		CHECK(dec.feed(frame.data(), frame.size(), &out, &err));
	}
	CHECK(err.empty());
	CHECK_EQ(out.size(), size_t(4));
	for (size_t i = 0; i < 4; i++)
		CHECK_STR_EQ(out[i], messages[i]);
}

TEST(control_frame_incremental_feed)
{
	ControlKeys keys = makeKeys();
	ControlFrameEncoder enc;
	enc.setKeys(keys);
	ControlFrameReader dec;
	dec.setKeys(keys);

	auto frame1 = enc.encode("{\"a\":1}");
	auto frame2 = enc.encode("{\"b\":2}");
	std::vector<uint8_t> stream = frame1;
	stream.insert(stream.end(), frame2.begin(), frame2.end());

	std::vector<std::string> out;
	std::string err;
	for (size_t i = 0; i < stream.size(); i++) {
		CHECK(dec.feed(&stream[i], 1, &out, &err));
	}
	CHECK(err.empty());
	CHECK_EQ(out.size(), size_t(2));
	CHECK_STR_EQ(out[0], "{\"a\":1}");
	CHECK_STR_EQ(out[1], "{\"b\":2}");
}

TEST(control_frame_tamper_rejected)
{
	ControlKeys keys = makeKeys();
	ControlFrameEncoder enc;
	enc.setKeys(keys);
	ControlFrameReader dec;
	dec.setKeys(keys);

	auto frame = enc.encode("{\"type\":\"auth_ok\"}");
	frame[kControlHeaderSize + 2] ^= 0x40;

	std::vector<std::string> out;
	std::string err;
	CHECK(!dec.feed(frame.data(), frame.size(), &out, &err));
	CHECK(!err.empty());
}

TEST(control_frame_wrong_key_rejected)
{
	ControlFrameEncoder enc;
	enc.setKeys(makeKeys());
	ControlFrameReader dec;
	dec.setKeys(makeKeys(1));

	auto frame = enc.encode("{\"secret\":true}");
	std::vector<std::string> out;
	std::string err;
	CHECK(!dec.feed(frame.data(), frame.size(), &out, &err));
	CHECK(!err.empty());
}

TEST(control_frame_bad_magic_rejected)
{
	ControlFrameReader dec;
	dec.setKeys(makeKeys(1));
	std::vector<uint8_t> junk(64, 0x41);
	std::vector<std::string> out;
	std::string err;
	CHECK(!dec.feed(junk.data(), junk.size(), &out, &err));
	CHECK(!err.empty());
}

TEST(control_frame_encrypted_before_keys_rejected)
{
	ControlKeys keys = makeKeys();
	ControlFrameEncoder enc;
	enc.setKeys(keys);
	ControlFrameReader dec;

	auto frame = enc.encode("{\"type\":\"ready\"}");
	std::vector<std::string> out;
	std::string err;
	CHECK(!dec.feed(frame.data(), frame.size(), &out, &err));
	CHECK(!err.empty());
}
