#include "test_framework.h"

#include "protocol/messages.h"

#include <string>

using namespace ocp;

namespace {

std::string roundTrip(const Message &msg)
{
	json::Value v = toJson(msg);
	std::string payload = v.dump();
	std::string err;
	Message back = parseMessage(payload, &err);
	CHECK(err.empty());
	CHECK(back.type == msg.type);
	return payload;
}

}

TEST(messages_hello_round_trip)
{
	DeviceInfo dev;
	dev.id = "00112233445566778899aabbccddeeff";
	dev.name = "Pixel 7";
	dev.os = "android";
	dev.app = "1.0.0";

	Message hello = makeHello("hkdf-sha256", 0, std::string(32, 'a'), std::string(32, 'b'), dev);
	roundTrip(hello);

	std::string payload = toJson(hello).dump();
	std::string err;
	Message back = parseMessage(payload, &err);
	CHECK(err.empty());
	CHECK_STR_EQ(back.proto, "ocp/1");
	CHECK_STR_EQ(back.kdf, "hkdf-sha256");
	CHECK_STR_EQ(back.device.id, dev.id);
	CHECK_STR_EQ(back.device.name, dev.name);
	CHECK_STR_EQ(back.salt, std::string(32, 'a'));
}

TEST(messages_config_round_trip)
{
	ConfigMessage cfg;
	cfg.has_video = cfg.has_camera = cfg.has_audio = cfg.has_stream = true;
	cfg.video.width = 1920;
	cfg.video.height = 1080;
	cfg.video.fps = 30;
	cfg.video.bitrate_kbps = 4000;
	cfg.video.camera = "toggle";
	cfg.video.orientation = "portrait";
	cfg.camera.focus_mode = "manual";
	cfg.camera.focus_distance = 0.7;
	cfg.camera.torch = true;
	cfg.camera.face_pixelation = true;
	cfg.camera.face_pixelation_strength = 82;
	cfg.camera.wb_kelvin = 4300;
	cfg.audio.enabled = true;
	cfg.stream.active = true;

	Message msg = makeConfig(cfg);
	std::string payload = roundTrip(msg);

	std::string err;
	Message back = parseMessage(payload, &err);
	CHECK(back.config.has_video);
	CHECK(back.config.has_camera);
	CHECK(back.config.camera.face_pixelation);
	CHECK_EQ(back.config.camera.face_pixelation_strength, 82);
	CHECK(back.config.has_audio);
	CHECK(back.config.has_stream);
	CHECK_EQ(back.config.video.width, 1920);
	CHECK_EQ(back.config.video.bitrate_kbps, 4000);
	CHECK_STR_EQ(back.config.video.camera, "toggle");
	CHECK_STR_EQ(back.config.camera.focus_mode, "manual");
	CHECK(back.config.camera.torch);
	CHECK_EQ(int(back.config.camera.wb_kelvin), 4300);
	CHECK(back.config.audio.enabled);
	CHECK(back.config.stream.active);
}

TEST(messages_partial_config)
{
	std::string err;
	Message m = parseMessage(R"({"type":"config","video":{"camera":"front"}})", &err);
	CHECK(err.empty());
	CHECK(m.type == MsgType::Config);
	CHECK(m.config.has_video);
	CHECK(!m.config.has_camera);
	CHECK_STR_EQ(m.config.video.camera, "front");
	CHECK_EQ(m.config.video.width, 0);
}

TEST(messages_stats_hint_error)
{
	std::string err;
	Message stats = parseMessage(
	    R"({"type":"stats","fps":29.8,"bitrate_kbps":3980,"frames":100,"battery":72,)"
	    R"("thermal":"nominal","camera":"back","ts_rel_ns":5000000})",
	    &err);
	CHECK(err.empty());
	CHECK(stats.type == MsgType::Stats);
	CHECK(stats.stats.fps > 29.7 && stats.stats.fps < 29.9);
	CHECK_EQ(stats.stats.frames, int64_t(100));
	CHECK_EQ(stats.stats.battery, 72);
	CHECK_STR_EQ(stats.stats.camera, "back");

	Message hint =
	    parseMessage(R"({"type":"hint","loss_pct":2.5,"rtt_ms":12,"queue_ms":30})", &err);
	CHECK(err.empty());
	CHECK(hint.type == MsgType::Hint);
	CHECK(hint.hint.loss_pct > 2.4);

	Message error = parseMessage(R"({"type":"error","code":"pairing_failed","msg":"nope"})", &err);
	CHECK(err.empty());
	CHECK(error.type == MsgType::Error);
	CHECK_STR_EQ(error.code, "pairing_failed");
}

TEST(messages_rejects_bad_payloads)
{
	struct Case {
		const char *payload;
		bool fail;
	};
	const Case cases[] = {
	    {"", true},
	    {"not json", true},
	    {R"({"type":"mystery"})", true},
	    {R"({"nofield":1})", true},
	    {R"({"type":"hello","proto":"ocp/2"})", true},
	    {R"({"type":"ping","t":123})", false},
	};
	for (const auto &c : cases) {
		std::string err;
		parseMessage(c.payload, &err);
		if (c.fail)
			CHECK(!err.empty());
		else
			CHECK(err.empty());
	}
}

TEST(messages_ready_authok_bye)
{
	std::string err;
	Message ready = parseMessage(
	    R"({"type":"ready","caps":{"video":{"codec":["h264"],"fps":[30,60]}}})", &err);
	CHECK(err.empty());
	CHECK(ready.type == MsgType::Ready);
	CHECK(ready.caps["video"]["codec"].items()[0].asString() == "h264");

	Message authOk = parseMessage(
	    R"({"type":"auth_ok","session":"aabb","media_port":47101,"proof":"ccdd","paired":true})",
	    &err);
	CHECK(err.empty());
	CHECK_STR_EQ(authOk.session, "aabb");
	CHECK_EQ(authOk.media_port, 47101);
	CHECK(authOk.paired);

	Message bye = parseMessage(R"({"type":"bye","reason":"shutdown"})", &err);
	CHECK(err.empty());
	CHECK_STR_EQ(bye.reason, "shutdown");

	StatsMessage st;
	st.fps = 30;
	st.frames = 10;
	roundTrip(makeStats(st));
	roundTrip(makeAuthOk("sess", 47101, "proof", false));
	roundTrip(makeError("busy", "try later"));
	roundTrip(makeBye("done"));
	roundTrip(makePing(12345));
	roundTrip(makePong(12345));
	roundTrip(makeHint(HintMessage{1.0, 2.0, 3.0}));
}
