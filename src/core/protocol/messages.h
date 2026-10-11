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
	AvatarChunk,
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
	bool face_pixelation = false;
	int face_pixelation_strength = 50;
};

struct AudioRequest {
	bool enabled = false;
	int sample_rate = 48000;
	int channels = 1;
};

struct StreamState {
	bool active = true;
};

// Image or character sheet drawn over tracked faces by the phone.
struct AvatarConfig {
	bool enabled = false;
	std::string image; // SHA-256 hex of the file, empty = none
	int64_t size = 0;
	std::string mode = "animate"; // "animate" | "position"
	int cols = 1;
	int rows = 1;
	int fps = 12;
	int scale = 150;    // percent of face height
	int offset_y = -10; // percent of face height, negative = up
	bool flip = false;  // mirror sprite when moving left
};

struct ConfigMessage {
	VideoRequest video;
	CameraControl camera;
	AudioRequest audio;
	StreamState stream;
	AvatarConfig avatar;
	bool has_video = false;
	bool has_camera = false;
	bool has_audio = false;
	bool has_stream = false;
	bool has_avatar = false;
};

struct AvatarChunkMessage {
	std::string id; // SHA-256 hex of the whole file
	int64_t offset = 0;
	int64_t total = 0;
	std::string data; // hex
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
	AvatarChunkMessage chunk;

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
Message makeAvatarChunk(const std::string &id, int64_t offset, int64_t total,
			const uint8_t *data, size_t len);

json::Value toJson(const Message &msg);
json::Value configToJson(const ConfigMessage &config);

// Parses a control payload. On failure returns a message with
// type == MsgType::Unknown and fills *error.
Message parseMessage(const std::string &payload, std::string *error = nullptr);

}
