#pragma once

#include "json/json.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ocp {

enum class MsgType {
	Unknown,
	Hello,
	Challenge,
	Auth,
	AuthOk,
	Ready,
	Config,
	Stats,
	Hint,
	Ping,
	Pong,
	Error,
	Bye,
};

struct DeviceInfo {
	std::string id;
	std::string name;
	std::string os;
	std::string app;
};

struct VideoRequest {
	std::string codec = "h264";
	int width = 0;
	int height = 0;
	int fps = 0;
	int bitrate_kbps = 0;
	int gop_s = 2;
	std::string camera = "back";
	std::string orientation = "auto";
};

struct CameraControl {
	std::string focus_mode = "auto";
	double focus_distance = 0.5;
	std::string exposure_mode = "auto";
	double exposure_bias = 0.0;
	std::string wb_mode = "auto";
	double wb_kelvin = 5500;
	bool torch = false;
};

struct AudioRequest {
	bool enabled = false;
	int sample_rate = 48000;
	int channels = 1;
};

struct StreamState {
	bool active = true;
};

struct ConfigMessage {
	VideoRequest video;
	CameraControl camera;
	AudioRequest audio;
	StreamState stream;
	bool has_video = false;
	bool has_camera = false;
	bool has_audio = false;
	bool has_stream = false;
};

struct StatsMessage {
	double fps = 0.0;
	double bitrate_kbps = 0.0;
	int64_t frames = 0;
	int battery = -1;
	std::string thermal;
	std::string camera;
	int64_t ts_rel_ns = 0;
};

struct HintMessage {
	double loss_pct = 0.0;
	double rtt_ms = 0.0;
	double queue_ms = 0.0;
};

struct Message {
	MsgType type = MsgType::Unknown;

	std::string proto;
	std::string kdf;
	uint32_t iter = 0;
	std::string salt;
	std::string nonce;
	std::string proof;
	DeviceInfo device;

	std::string session;
	int media_port = 0;
	bool paired = false;

	json::Value caps;
	ConfigMessage config;
	StatsMessage stats;
	HintMessage hint;

	int64_t t = 0;
	std::string code;
	std::string msg;
	std::string reason;
};

const char *msgTypeName(MsgType type);
MsgType messageTypeFromName(const std::string &name);

Message makeHello(const std::string &kdf, uint32_t iter, const std::string &salt,
		  const std::string &nonce, const DeviceInfo &device);
Message makeChallenge(const std::string &kdf, uint32_t iter, const std::string &nonce);
Message makeAuth(const std::string &nonce, const std::string &proof);
Message makeAuthOk(const std::string &session, int mediaPort, const std::string &proof,
		   bool paired);
Message makeReady(json::Value caps);
Message makeConfig(const ConfigMessage &config);
Message makeStats(const StatsMessage &stats);
Message makeHint(const HintMessage &hint);
Message makePing(int64_t t);
Message makePong(int64_t t);
Message makeError(const std::string &code, const std::string &msg);
Message makeBye(const std::string &reason);

json::Value toJson(const Message &msg);
json::Value configToJson(const ConfigMessage &config);

// Parses a control payload. On failure returns a message with
// type == MsgType::Unknown and fills *error.
Message parseMessage(const std::string &payload, std::string *error = nullptr);

}
