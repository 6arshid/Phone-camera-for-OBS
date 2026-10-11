#include "protocol/messages.h"

#include "crypto/hex.h"
#include "protocol/constants.h"

#include <cmath>
#include <algorithm>

namespace ocp {

namespace {

double round1(double v) { return std::round(v * 10.0) / 10.0; }

void parseVideo(const json::Value &v, VideoRequest &out)
{
	out.codec = v.getString("codec", out.codec);
	out.width = int(v.getInt("width", out.width));
	out.height = int(v.getInt("height", out.height));
	out.fps = int(v.getInt("fps", out.fps));
	out.bitrate_kbps = int(v.getInt("bitrate_kbps", out.bitrate_kbps));
	out.gop_s = int(v.getInt("gop_s", out.gop_s));
	out.camera = v.getString("camera", out.camera);
	out.orientation = v.getString("orientation", out.orientation);
}

void parseCamera(const json::Value &v, CameraControl &out)
{
	out.focus_mode = v.getString("focus_mode", out.focus_mode);
	out.focus_distance = v.getNumber("focus_distance", out.focus_distance);
	out.exposure_mode = v.getString("exposure_mode", out.exposure_mode);
	out.exposure_bias = v.getNumber("exposure_bias", out.exposure_bias);
	out.wb_mode = v.getString("wb_mode", out.wb_mode);
	out.wb_kelvin = v.getNumber("wb_kelvin", out.wb_kelvin);
	out.torch = v.getBool("torch", out.torch);
	out.face_pixelation = v.getBool("face_pixelation", false);
	out.face_pixelation_strength = (int)std::clamp<int64_t>(v.getInt("face_pixelation_strength", 50), 1, 100);
}

void parseAudio(const json::Value &v, AudioRequest &out)
{
	out.enabled = v.getBool("enabled", out.enabled);
	out.sample_rate = int(v.getInt("sample_rate", out.sample_rate));
	out.channels = int(v.getInt("channels", out.channels));
}

int clampInt(int64_t v, int lo, int hi) { return (int)std::clamp<int64_t>(v, lo, hi); }

void parseAvatar(const json::Value &v, AvatarConfig &out)
{
	out.enabled = v.getBool("enabled", false);
	out.image = v.getString("image");
	out.size = std::clamp<int64_t>(v.getInt("size", 0), 0, (int64_t)kAvatarMaxBytes);
	out.mode = v.getString("mode", "animate") == "position" ? "position" : "animate";
	out.cols = clampInt(v.getInt("cols", 1), 1, 16);
	out.rows = clampInt(v.getInt("rows", 1), 1, 16);
	out.fps = clampInt(v.getInt("fps", 12), 1, 30);
	out.scale = clampInt(v.getInt("scale", 150), 50, 300);
	out.offset_y = clampInt(v.getInt("offset_y", -10), -100, 100);
	out.flip = v.getBool("flip", false);
}

}

const char *msgTypeName(MsgType type)
{
	switch (type) {
	case MsgType::Hello:
		return "hello";
	case MsgType::Challenge:
		return "challenge";
	case MsgType::Auth:
		return "auth";
	case MsgType::AuthOk:
		return "auth_ok";
	case MsgType::Ready:
		return "ready";
	case MsgType::Config:
		return "config";
	case MsgType::Stats:
		return "stats";
	case MsgType::Hint:
		return "hint";
	case MsgType::Ping:
		return "ping";
	case MsgType::Pong:
		return "pong";
	case MsgType::Error:
		return "error";
	case MsgType::Bye:
		return "bye";
	case MsgType::AvatarChunk:
		return "avatar_chunk";
	default:
		return "unknown";
	}
}

MsgType messageTypeFromName(const std::string &name)
{
	if (name == "hello")
		return MsgType::Hello;
	if (name == "challenge")
		return MsgType::Challenge;
	if (name == "auth")
		return MsgType::Auth;
	if (name == "auth_ok")
		return MsgType::AuthOk;
	if (name == "ready")
		return MsgType::Ready;
	if (name == "config")
		return MsgType::Config;
	if (name == "stats")
		return MsgType::Stats;
	if (name == "hint")
		return MsgType::Hint;
	if (name == "ping")
		return MsgType::Ping;
	if (name == "pong")
		return MsgType::Pong;
	if (name == "error")
		return MsgType::Error;
	if (name == "bye")
		return MsgType::Bye;
	if (name == "avatar_chunk")
		return MsgType::AvatarChunk;
	return MsgType::Unknown;
}

Message makeHello(const std::string &kdf, uint32_t iter, const std::string &salt,
		  const std::string &nonce, const DeviceInfo &device)
{
	Message m;
	m.type = MsgType::Hello;
	m.proto = "ocp/1";
	m.kdf = kdf;
	m.iter = iter;
	m.salt = salt;
	m.nonce = nonce;
	m.device = device;
	return m;
}

Message makeChallenge(const std::string &kdf, uint32_t iter, const std::string &nonce)
{
	Message m;
	m.type = MsgType::Challenge;
	m.kdf = kdf;
	m.iter = iter;
	m.nonce = nonce;
	return m;
}

Message makeAuth(const std::string &nonce, const std::string &proof)
{
	Message m;
	m.type = MsgType::Auth;
	m.nonce = nonce;
	m.proof = proof;
	return m;
}

Message makeAuthOk(const std::string &session, int mediaPort, const std::string &proof,
		   bool paired)
{
	Message m;
	m.type = MsgType::AuthOk;
	m.session = session;
	m.media_port = mediaPort;
	m.proof = proof;
	m.paired = paired;
	return m;
}

Message makeReady(json::Value caps)
{
	Message m;
	m.type = MsgType::Ready;
	m.caps = std::move(caps);
	return m;
}

Message makeConfig(const ConfigMessage &config)
{
	Message m;
	m.type = MsgType::Config;
	m.config = config;
	m.config.has_video = m.config.has_camera = m.config.has_audio = m.config.has_stream = true;
	m.config.has_avatar = true;
	return m;
}

Message makeStats(const StatsMessage &stats)
{
	Message m;
	m.type = MsgType::Stats;
	m.stats = stats;
	return m;
}

Message makeHint(const HintMessage &hint)
{
	Message m;
	m.type = MsgType::Hint;
	m.hint = hint;
	return m;
}

Message makePing(int64_t t)
{
	Message m;
	m.type = MsgType::Ping;
	m.t = t;
	return m;
}

Message makePong(int64_t t)
{
	Message m;
	m.type = MsgType::Pong;
	m.t = t;
	return m;
}

Message makeError(const std::string &code, const std::string &msg)
{
	Message m;
	m.type = MsgType::Error;
	m.code = code;
	m.msg = msg;
	return m;
}

Message makeBye(const std::string &reason)
{
	Message m;
	m.type = MsgType::Bye;
	m.reason = reason;
	return m;
}

Message makeAvatarChunk(const std::string &id, int64_t offset, int64_t total,
			const uint8_t *data, size_t len)
{
	Message m;
	m.type = MsgType::AvatarChunk;
	m.chunk.id = id;
	m.chunk.offset = offset;
	m.chunk.total = total;
	m.chunk.data = toHex(data, len);
	return m;
}

json::Value configToJson(const ConfigMessage &c)
{
	json::Value out = json::Value::object();
	if (c.has_video) {
		json::Value v = json::Value::object();
		v.set("codec", c.video.codec);
		v.set("width", int64_t(c.video.width));
		v.set("height", int64_t(c.video.height));
		v.set("fps", int64_t(c.video.fps));
		v.set("bitrate_kbps", int64_t(c.video.bitrate_kbps));
		v.set("gop_s", int64_t(c.video.gop_s));
		v.set("camera", c.video.camera);
		v.set("orientation", c.video.orientation);
		out.set("video", v);
	}
	if (c.has_camera) {
		json::Value v = json::Value::object();
		v.set("focus_mode", c.camera.focus_mode);
		v.set("focus_distance", round1(c.camera.focus_distance));
		v.set("exposure_mode", c.camera.exposure_mode);
		v.set("exposure_bias", round1(c.camera.exposure_bias * 10.0) / 10.0);
		v.set("wb_mode", c.camera.wb_mode);
		v.set("wb_kelvin", round1(c.camera.wb_kelvin));
		v.set("torch", c.camera.torch);
		v.set("face_pixelation", c.camera.face_pixelation);
		v.set("face_pixelation_strength", c.camera.face_pixelation_strength);
		out.set("camera", v);
	}
	if (c.has_audio) {
		json::Value v = json::Value::object();
		v.set("enabled", c.audio.enabled);
		v.set("sample_rate", int64_t(c.audio.sample_rate));
		v.set("channels", int64_t(c.audio.channels));
		out.set("audio", v);
	}
	if (c.has_stream) {
		json::Value v = json::Value::object();
		v.set("active", c.stream.active);
		out.set("stream", v);
	}
	if (c.has_avatar) {
		json::Value v = json::Value::object();
		v.set("enabled", c.avatar.enabled);
		v.set("image", c.avatar.image);
		v.set("size", c.avatar.size);
		v.set("mode", c.avatar.mode);
		v.set("cols", c.avatar.cols);
		v.set("rows", c.avatar.rows);
		v.set("fps", c.avatar.fps);
		v.set("scale", c.avatar.scale);
		v.set("offset_y", c.avatar.offset_y);
		v.set("flip", c.avatar.flip);
		out.set("avatar", v);
	}
	return out;
}

json::Value toJson(const Message &msg)
{
	json::Value out = json::Value::object();
	out.set("type", msgTypeName(msg.type));

	switch (msg.type) {
	case MsgType::Hello: {
		out.set("proto", msg.proto.empty() ? "ocp/1" : msg.proto);
		out.set("kdf", msg.kdf);
		out.set("iter", int64_t(msg.iter));
		out.set("salt", msg.salt);
		out.set("nonce", msg.nonce);
		json::Value dev = json::Value::object();
		dev.set("id", msg.device.id);
		dev.set("name", msg.device.name);
		dev.set("os", msg.device.os);
		dev.set("app", msg.device.app);
		out.set("device", dev);
		break;
	}
	case MsgType::Challenge:
		out.set("nonce", msg.nonce);
		out.set("kdf", msg.kdf);
		out.set("iter", int64_t(msg.iter));
		break;
	case MsgType::Auth:
		out.set("nonce", msg.nonce);
		out.set("proof", msg.proof);
		break;
	case MsgType::AuthOk:
		out.set("session", msg.session);
		out.set("media_port", int64_t(msg.media_port));
		out.set("proof", msg.proof);
		out.set("paired", msg.paired);
		break;
	case MsgType::Ready:
		out.set("caps", msg.caps);
		break;
	case MsgType::Config: {
		json::Value body = configToJson(msg.config);
		for (const auto &kv : body.members())
			out.set(kv.first, kv.second);
		break;
	}
	case MsgType::Stats:
		out.set("fps", msg.stats.fps);
		out.set("bitrate_kbps", msg.stats.bitrate_kbps);
		out.set("frames", msg.stats.frames);
		out.set("battery", int64_t(msg.stats.battery));
		out.set("thermal", msg.stats.thermal);
		out.set("camera", msg.stats.camera);
		out.set("ts_rel_ns", msg.stats.ts_rel_ns);
		break;
	case MsgType::Hint:
		out.set("loss_pct", msg.hint.loss_pct);
		out.set("rtt_ms", msg.hint.rtt_ms);
		out.set("queue_ms", msg.hint.queue_ms);
		break;
	case MsgType::Ping:
	case MsgType::Pong:
		out.set("t", msg.t);
		break;
	case MsgType::Error:
		out.set("code", msg.code);
		out.set("msg", msg.msg);
		break;
	case MsgType::Bye:
		out.set("reason", msg.reason);
		break;
	case MsgType::AvatarChunk:
		out.set("id", msg.chunk.id);
		out.set("offset", msg.chunk.offset);
		out.set("total", msg.chunk.total);
		out.set("data", msg.chunk.data);
		break;
	default:
		break;
	}

	return out;
}

Message parseMessage(const std::string &payload, std::string *error)
{
	std::string localError;
	if (!error)
		error = &localError;
	error->clear();

	Message out;
	json::Value root = json::Value::parse(payload, error);
	if (!error->empty())
		return out;

	std::string type = root.getString("type");
	if (type.empty()) {
		*error = "missing message type";
		return out;
	}
	out.type = messageTypeFromName(type);
	if (out.type == MsgType::Unknown) {
		*error = "unknown message type: " + type;
		return out;
	}

	switch (out.type) {
	case MsgType::Hello: {
		out.proto = root.getString("proto");
		out.kdf = root.getString("kdf");
		out.iter = uint32_t(root.getInt("iter", 0));
		out.salt = root.getString("salt");
		out.nonce = root.getString("nonce");
		const json::Value &dev = root["device"];
		out.device.id = dev.getString("id");
		out.device.name = dev.getString("name");
		out.device.os = dev.getString("os");
		out.device.app = dev.getString("app");
		if (out.proto != "ocp/1") {
			*error = "unsupported protocol: " + out.proto;
			out.type = MsgType::Unknown;
		}
		break;
	}
	case MsgType::Challenge:
		out.nonce = root.getString("nonce");
		out.kdf = root.getString("kdf");
		out.iter = uint32_t(root.getInt("iter", 0));
		break;
	case MsgType::Auth:
		out.nonce = root.getString("nonce");
		out.proof = root.getString("proof");
		break;
	case MsgType::AuthOk:
		out.session = root.getString("session");
		out.media_port = int(root.getInt("media_port", 0));
		out.proof = root.getString("proof");
		out.paired = root.getBool("paired", false);
		break;
	case MsgType::Ready:
		out.caps = root["caps"];
		break;
	case MsgType::Config:
		if (root.has("video")) {
			out.config.has_video = true;
			parseVideo(root["video"], out.config.video);
		}
		if (root.has("camera")) {
			out.config.has_camera = true;
			parseCamera(root["camera"], out.config.camera);
		}
		if (root.has("audio")) {
			out.config.has_audio = true;
			parseAudio(root["audio"], out.config.audio);
		}
		if (root.has("stream")) {
			out.config.has_stream = true;
			out.config.stream.active =
			    root["stream"].getBool("active", out.config.stream.active);
		}
		if (root.has("avatar")) {
			out.config.has_avatar = true;
			parseAvatar(root["avatar"], out.config.avatar);
		}
		break;
	case MsgType::Stats:
		out.stats.fps = root.getNumber("fps", 0);
		out.stats.bitrate_kbps = root.getNumber("bitrate_kbps", 0);
		out.stats.frames = root.getInt("frames", 0);
		out.stats.battery = int(root.getInt("battery", -1));
		out.stats.thermal = root.getString("thermal");
		out.stats.camera = root.getString("camera");
		out.stats.ts_rel_ns = root.getInt("ts_rel_ns", 0);
		break;
	case MsgType::Hint:
		out.hint.loss_pct = root.getNumber("loss_pct", 0);
		out.hint.rtt_ms = root.getNumber("rtt_ms", 0);
		out.hint.queue_ms = root.getNumber("queue_ms", 0);
		break;
	case MsgType::Ping:
	case MsgType::Pong:
		out.t = root.getInt("t", 0);
		break;
	case MsgType::Error:
		out.code = root.getString("code");
		out.msg = root.getString("msg");
		break;
	case MsgType::Bye:
		out.reason = root.getString("reason");
		break;
	case MsgType::AvatarChunk:
		out.chunk.id = root.getString("id");
		out.chunk.offset = root.getInt("offset", 0);
		out.chunk.total = root.getInt("total", 0);
		out.chunk.data = root.getString("data");
		break;
	default:
		break;
	}

	return out;
}

}
