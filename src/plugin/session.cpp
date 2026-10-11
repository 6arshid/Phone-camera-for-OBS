#include "session.h"

#include "net_util.h"
#include "nv12_util.h"
#include "server.h"

#include "codec/h264.h"
#include "crypto/hex.h"
#include "crypto/random.h"
#include "crypto/sha256.h"
#include "pairing/pairing.h"
#include "protocol/constants.h"

#include <obs-module.h>
#include <util/platform.h>

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>

#include <algorithm>
#include <chrono>
#include <cstring>

namespace ocp {
namespace plugin {

namespace {

uint64_t readU64Be(const uint8_t *p)
{
	uint64_t v = 0;
	for (int i = 0; i < 8; i++)
		v = (v << 8) | p[i];
	return v;
}

bool selectReadable(int fd, int timeoutMs)
{
	fd_set set;
	FD_ZERO(&set);
	FD_SET((SOCKET)fd, &set);
	timeval tv;
	tv.tv_sec = timeoutMs / 1000;
	tv.tv_usec = (timeoutMs % 1000) * 1000;
	int rc = select(0, &set, nullptr, nullptr, &tv);
	return rc > 0 && FD_ISSET((SOCKET)fd, &set);
}

MasterKey openModeMaster(const uint8_t salt[16], const uint8_t clientNonce[16])
{
	Sha256 h;
	const char label[] = "ocp1-open";
	h.update(reinterpret_cast<const uint8_t *>(label), sizeof(label) - 1);
	h.update(salt, 16);
	h.update(clientNonce, 16);
	MasterKey master{};
	h.final(master.data());
	return master;
}

std::string dumpMessage(const Message &msg) { return toJson(msg).dump(); }

}

Session::Session(int fdIn, uint16_t mediaPort, Server *serverIn)
	: fd(fdIn), mediaPort_(mediaPort), server(serverIn)
{
}

Session::~Session()
{
	requestStop();
	join();
}

void Session::start()
{
	controlThread = std::thread(&Session::controlLoop, this);
	videoThread = std::thread(&Session::videoLoop, this);
	audioThread = std::thread(&Session::audioLoop, this);
}

void Session::requestStop()
{
	stop = true;
	closeSocket();
	videoCv.notify_all();
	audioCv.notify_all();
}

void Session::join()
{
	if (controlThread.joinable())
		controlThread.join();
	if (videoThread.joinable())
		videoThread.join();
	if (audioThread.joinable())
		audioThread.join();
}

void Session::closeSocket()
{
	int expected = fd;
	if (expected < 0)
		return;
	fd = -1;
	shutdown((SOCKET)expected, SD_BOTH);
	closesocket((SOCKET)expected);
}

bool Session::attach(obs_source_t *sourceIn, SessionListener *listenerIn)
{
	{
		std::lock_guard<std::mutex> lock(sourceMutex);
		if (listener.load() != nullptr)
			return false;
		source = sourceIn;
		listener.store(listenerIn);
	}
	if (authed.load() && listenerIn)
		listenerIn->onSessionAuthenticated(this);
	return true;
}

void Session::detach(SessionListener *listenerIn)
{
	std::lock_guard<std::mutex> lock(sourceMutex);
	if (listener.load() != listenerIn)
		return;
	source = nullptr;
	listener.store(nullptr);
}

void Session::applySettings(const SessionSettings &settingsIn)
{
	{
		std::lock_guard<std::mutex> lock(settingsMutex);
		settings = settingsIn;
	}
	if (authed.load())
		sendConfig();
}

std::string Session::thermal()
{
	std::lock_guard<std::mutex> lock(thermalMutex);
	return thermalState;
}

bool Session::sendRaw(const std::vector<uint8_t> &frame)
{
	int sock = fd;
	if (sock < 0)
		return false;
	size_t off = 0;
	while (off < frame.size()) {
		int n = send((SOCKET)sock, reinterpret_cast<const char *>(frame.data()) + off,
			     (int)(frame.size() - off), 0);
		if (n <= 0)
			return false;
		off += (size_t)n;
	}
	return true;
}

bool Session::sendMessage(const Message &msg)
{
	std::lock_guard<std::mutex> lock(sendMutex);
	std::string payload = dumpMessage(msg);
	return sendRaw(encoder.encode(payload));
}

void Session::sendConfig()
{
	SessionSettings copy;
	{
		std::lock_guard<std::mutex> lock(settingsMutex);
		copy = settings;
	}

	ConfigMessage cfg;
	cfg.has_video = true;
	cfg.video.width = copy.video.width;
	cfg.video.height = copy.video.height;
	cfg.video.fps = copy.video.fps;
	cfg.video.bitrate_kbps = copy.video.bitrateKbps;
	cfg.video.gop_s = copy.video.gopS;
	cfg.video.orientation = copy.video.orientation;
	cfg.video.codec = "h264";
	cfg.video.camera = copy.camera;
	cfg.has_camera = true;
	cfg.camera = copy.cameraCtl;
	cfg.has_audio = true;
	cfg.audio.enabled = copy.audioEnabled;
	cfg.audio.sample_rate = 48000;
	cfg.audio.channels = 1;
	cfg.has_stream = true;
	cfg.stream.active = copy.streamActive;
	cfg.has_avatar = true;
	cfg.avatar = copy.avatar;
	if (!copy.avatarData) {
		cfg.avatar.image.clear();
		cfg.avatar.size = 0;
	}
	blog(LOG_INFO, "[obs-phone-camera] config for %s: active=%s %dx%d @ %d fps",
	     peerName.c_str(), copy.streamActive ? "true" : "false",
	     cfg.video.width, cfg.video.height, cfg.video.fps);
	sendMessage(makeConfig(cfg));
}

void Session::onMediaDatagram(const uint8_t *data, size_t len)
{
	if (!mediaReady.load() || len < 12 || readU64Be(data + 8) != sessionTag)
		return;

	MediaHeader header;
	std::vector<uint8_t> plain;
	std::string err;
	if (!openMediaPacket(mediaKeys, data, len, &header, &plain, &err)) {
		mediaFailed = true;
		return;
	}

	std::vector<ReassembledFrame> frames;
	std::string rerr;
	reassembler.push(header, std::move(plain), &frames, &rerr);
	statDelivered = reassembler.stats(MEDIA_STREAM_VIDEO).framesDelivered +
		       reassembler.stats(MEDIA_STREAM_AUDIO).framesDelivered;
	statLost = reassembler.stats(MEDIA_STREAM_VIDEO).framesLost +
		   reassembler.stats(MEDIA_STREAM_AUDIO).framesLost;
	statLossPct = (reassembler.stats(MEDIA_STREAM_VIDEO).lossPercent() +
			reassembler.stats(MEDIA_STREAM_AUDIO).lossPercent()) / 2.0;

	for (ReassembledFrame &frame : frames) {
		if (frame.stream == MEDIA_STREAM_VIDEO)
			pushVideo(std::move(frame));
		else
			pushAudio(std::move(frame));
	}
}

void Session::pushVideo(ReassembledFrame &&frame)
{
	std::lock_guard<std::mutex> lock(videoMutex);
	if (videoQueue.size() >= 16) {
		videoQueue.pop_front();
		dropped++;
	}
	videoQueue.push_back(std::move(frame));
	videoCv.notify_all();
}

void Session::pushAudio(ReassembledFrame &&frame)
{
	std::lock_guard<std::mutex> lock(audioMutex);
	if (audioQueue.size() >= 64) {
		audioQueue.pop_front();
		dropped++;
	}
	audioQueue.push_back(std::move(frame));
	audioCv.notify_all();
}

void Session::controlLoop()
{
	std::string err;
	bool ok = handshakePhase(&err);
	if (ok && server)
		server->sessionReady(this);
	if (ok)
		ok = authenticatedLoop(&err);
	if (!ok && !stop.load() && server) {
		blog(LOG_INFO, "[obs-phone-camera] session ended: %s", err.c_str());
	}

	requestStop();

	SessionListener *list = listener.load();
	if (list)
		list->onSessionClosed(this, err.empty() ? "disconnected" : err);
	done = true;
}

bool Session::handshakePhase(std::string *err)
{
	auto deadline = monotonicNs() + 15ull * 1000 * 1000 * 1000;

	bool haveHello = false;
	std::string helloRaw;
	std::string challengeRaw;
	std::array<uint8_t, 16> clientNonce{};
	std::array<uint8_t, 16> serverNonce{};
	uint8_t salt[16] = {};
	uint32_t iterations = kMinPbkdf2Iterations;
	bool codeKdf = false;

	while (!stop.load()) {
		if (monotonicNs() > deadline) {
			if (err)
				*err = "handshake timeout";
			return false;
		}
		if (fd < 0)
			return false;
		if (!selectReadable(fd, 200))
			continue;

		uint8_t buf[4096];
		int n = recv((SOCKET)fd, reinterpret_cast<char *>(buf), sizeof(buf), 0);
		if (n <= 0) {
			if (err)
				*err = "peer closed during handshake";
			return false;
		}

		std::vector<std::string> payloads;
		std::string ferr;
		if (!reader.feed(buf, (size_t)n, &payloads, &ferr)) {
			if (err)
				*err = ferr;
			return false;
		}

		for (const std::string &payload : payloads) {
			std::string perr;
			Message msg = parseMessage(payload, &perr);
			if (!perr.empty()) {
				if (err)
					*err = perr;
				return false;
			}

			if (!haveHello) {
				if (msg.type != MsgType::Hello) {
					if (err)
						*err = "expected hello";
					return false;
				}
				if (msg.proto != kProtocolVersion) {
					if (err)
						*err = "unsupported protocol version";
					return false;
				}
				codeKdf = msg.kdf == "pbkdf2-sha256";
				if (!codeKdf && msg.kdf != "hkdf-sha256") {
					if (err)
						*err = "unsupported kdf";
					return false;
				}
				if ((codeKdf && msg.iter < kMinPbkdf2Iterations) ||
				    (!codeKdf && msg.iter != 0)) {
					if (err)
						*err = "pbkdf2 iteration count too low";
					return false;
				}
				if (!fromHex(msg.salt, salt, sizeof(salt)) ||
				    !fromHex(msg.nonce, clientNonce.data(), clientNonce.size())) {
					if (err)
						*err = "malformed salt or nonce";
					return false;
				}
				iterations = msg.iter;
				haveHello = true;
				helloRaw = payload;
				peerId = msg.device.id;
				peerName = msg.device.name;
				if (peerId.empty()) {
					if (err)
						*err = "device id missing";
					return false;
				}

				if (!randomBytes(serverNonce.data(), serverNonce.size())) {
					if (err)
						*err = "rng failure";
					return false;
				}
				Message challenge =
					makeChallenge(msg.kdf, msg.iter, toHex(serverNonce.data(),
									       serverNonce.size()));
				challengeRaw = dumpMessage(challenge);
				if (!sendRaw(encoder.encode(challengeRaw)))
					return false;
				continue;
			}

			if (msg.type != MsgType::Auth) {
				if (err)
					*err = "expected auth";
				return false;
			}

			ProofHash transcript = transcriptHash(helloRaw, challengeRaw);

			struct Candidate {
				MasterKey master;
				int kind = 0; // 1 stored, 2 open, 3 qr, 4 code
			};
			std::vector<Candidate> candidates;

			PairedDevice stored;
			if (server->paired().get(peerId, &stored)) {
				Candidate c;
				c.master = masterFromSecretHex(stored.secretHex);
				c.kind = 1;
				candidates.push_back(c);
			}
			if (!server->requirePairing()) {
				Candidate c;
				c.master = openModeMaster(salt, clientNonce.data());
				c.kind = 2;
				candidates.push_back(c);
			}
			{
				std::string qr = server->qrSecret();
				if (!qr.empty()) {
					Candidate c;
					c.master = masterFromSecretHex(qr);
					c.kind = 3;
					candidates.push_back(c);
				}
			}
			if (codeKdf) {
				std::string code = server->pairingCode();
				if (isValidPairingCode(code)) {
					Candidate c;
					c.master = masterFromCode(
						code, salt,
						iterations < kMinPbkdf2Iterations
							? kMinPbkdf2Iterations
							: iterations);
					c.kind = 4;
					candidates.push_back(c);
				}
			}

			bool matched = false;
			SessionKeys keys;
			int matchedKind = 0;
			MasterKey matchedMaster{};
			for (const Candidate &c : candidates) {
				SessionKeys tryKeys = deriveSessionKeys(
					c.master, clientNonce.data(), serverNonce.data());
				if (verifyPhoneProof(tryKeys.kAuth, clientNonce.data(),
						     serverNonce.data(), transcript,
						     msg.proof)) {
					keys = tryKeys;
					matched = true;
					matchedKind = c.kind;
					matchedMaster = c.master;
					break;
				}
			}

			if (!matched) {
				sendMessage(makeError("auth",
						       "device is not paired; scan the QR or "
						       "enter the pairing code"));
				if (err)
					*err = "authentication failed";
				return false;
			}

			uint8_t idBytes[kSessionIdSize];
			if (!randomBytes(idBytes, sizeof(idBytes))) {
				if (err)
					*err = "rng failure";
				return false;
			}
			sessionId = toHex(idBytes, sizeof(idBytes));
			sessionTag = 0;
			for (int i = 0; i < 8; i++)
				sessionTag = (sessionTag << 8) | idBytes[i];

			pairedFlag = matchedKind == 1 || matchedKind == 3 || matchedKind == 4;
			if (matchedKind == 3 || matchedKind == 4) {
				PairedDevice device;
				device.name = peerName;
				device.addedAt = (int64_t)(wallClockNs() / 1000000000ull);
				device.secretHex = matchedKind == 3 ? server->qrSecret()
								   : toHex(
									 matchedMaster.data(),
									 32);
				server->paired().put(peerId, device);
			}

			mediaKeys = keys.media();
			std::string obsProof =
			    computeObsProof(keys.kAuth, clientNonce.data(),
					    serverNonce.data(), transcript);
			if (!sendMessage(makeAuthOk(sessionId, (int)mediaPort_, obsProof,
						    pairedFlag))) {
				if (err)
					*err = "failed to send auth ok";
				return false;
			}
			encoder.setKeys(keys.control());
			reader.setKeys(keys.control());
			mediaReady = true;
			authed = true;

			blog(LOG_INFO,
			     "[obs-phone-camera] phone paired: %s (%s) via %s",
			     peerName.c_str(), peerId.c_str(),
			     matchedKind == 1   ? "stored secret"
			     : matchedKind == 2 ? "open mode"
			     : matchedKind == 3 ? "qr secret"
					       : "pairing code");

			SessionListener *list = listener.load();
			if (list)
				list->onSessionAuthenticated(this);
			return true;
		}
	}

	if (err)
		*err = "stopped during handshake";
	return false;
}

bool Session::authenticatedLoop(std::string *err)
{
	auto lastPing = monotonicNs();
	auto lastHint = monotonicNs();

	while (!stop.load()) {
		if (fd < 0) {
			if (err)
				*err = "socket closed";
			return false;
		}
		bool uploading = pumpAvatarUpload();
		if (selectReadable(fd, uploading ? 0 : 250)) {
			uint8_t buf[8192];
			int n = recv((SOCKET)fd, reinterpret_cast<char *>(buf), sizeof(buf), 0);
			if (n <= 0) {
				if (err)
					*err = "peer closed";
				return false;
			}
			std::vector<std::string> payloads;
			std::string ferr;
			if (!reader.feed(buf, (size_t)n, &payloads, &ferr)) {
				if (err)
					*err = ferr;
				return false;
			}
			for (const std::string &payload : payloads) {
				if (!handleMessage(payload)) {
					if (err)
						*err = "bad control message";
					return false;
				}
			}
		}

		uint64_t now = monotonicNs();
		if (now - lastPing > 2ull * 1000 * 1000 * 1000) {
			lastPing = now;
			lastPingSentNs = now;
			if (!sendMessage(makePing((int64_t)(now / 1000000ull)))) {
				if (err)
					*err = "send ping failed";
				return false;
			}
		}

		SessionSettings copy;
		{
			std::lock_guard<std::mutex> lock(settingsMutex);
			copy = settings;
		}
		if (copy.streamActive && now - lastHint > 1000ull * 1000 * 1000) {
			lastHint = now;
			HintMessage hint;
			hint.loss_pct = statLossPct.load();
			hint.rtt_ms = rttMs.load();
			int fps = copy.video.fps > 0 ? copy.video.fps : 30;
			size_t queueLen;
			{
				std::lock_guard<std::mutex> lock(videoMutex);
				queueLen = videoQueue.size();
			}
			hint.queue_ms = 1000.0 * (double)queueLen / (double)fps;
			sendMessage(makeHint(hint));
		}
	}

	if (err)
		err->clear();
	return true;
}

bool Session::handleMessage(const std::string &payload)
{
	std::string perr;
	Message msg = parseMessage(payload, &perr);
	if (!perr.empty())
		return false;

	switch (msg.type) {
	case MsgType::Stats: {
		{
			std::lock_guard<std::mutex> lock(thermalMutex);
			thermalState = msg.stats.thermal;
		}
		batteryPct = msg.stats.battery;
		uint64_t now = monotonicNs();
		uint64_t sent = lastPingSentNs.load();
		double latency = sent && now > sent ? (double)(now - sent) / 1000000.0 : 0.0;
		statLatencyMs = latency;
		SessionListener *list = listener.load();
		if (list)
			list->onPhoneStats(msg.stats, latency, statLossPct.load());
		return true;
	}
	case MsgType::Pong: {
		uint64_t sent = lastPingSentNs.load();
		uint64_t now = monotonicNs();
		if (sent && now > sent)
			rttMs = (double)(now - sent) / 1000000.0;
		return true;
	}
	case MsgType::Ping:
		return sendMessage(makePong(msg.t));
	case MsgType::Ready: {
		phoneAvatar = msg.caps["features"].getBool("avatar", false);
		phoneAvatarHave = msg.caps.getString("avatar_have");
		return true;
	}
	case MsgType::Error:
		blog(LOG_WARNING, "[obs-phone-camera] phone error: %s %s", msg.code.c_str(),
		     msg.msg.c_str());
		return true;
	case MsgType::Bye:
		stop = true;
		closeSocket();
		return true;
	default:
		return true;
	}
}

// Sends the avatar image in bounded bursts so pings and reads keep flowing.
// Returns true while more chunks remain.
bool Session::pumpAvatarUpload()
{
	if (!phoneAvatar)
		return false;
	std::shared_ptr<const std::vector<uint8_t>> data;
	std::string id;
	{
		std::lock_guard<std::mutex> lock(settingsMutex);
		if (settings.avatar.enabled) {
			data = settings.avatarData;
			id = settings.avatar.image;
		}
	}
	if (!data || data->empty() || id.empty() || id == avatarSentId)
		return false;
	if (id == phoneAvatarHave) {
		avatarSentId = id;
		return false;
	}
	if (id != avatarUploadId) {
		avatarUploadId = id;
		avatarUploadOffset = 0;
	}

	size_t budget = 16 * kAvatarChunkBytes;
	while (budget > 0 && avatarUploadOffset < data->size()) {
		size_t n = std::min(kAvatarChunkBytes, data->size() - avatarUploadOffset);
		if (!sendMessage(makeAvatarChunk(id, (int64_t)avatarUploadOffset,
						 (int64_t)data->size(),
						 data->data() + avatarUploadOffset, n)))
			return false;
		avatarUploadOffset += n;
		budget -= std::min(budget, n);
	}
	if (avatarUploadOffset < data->size())
		return true;
	avatarSentId = id;
	blog(LOG_INFO, "[obs-phone-camera] avatar image sent to %s (%zu bytes)",
	     peerName.c_str(), data->size());
	return false;
}

void Session::videoLoop()
{
	std::unique_ptr<VideoDecoder> localDecoder;
	int decW = 0, decH = 0;
	CropRect crop = {};

	while (!stop.load()) {
		ReassembledFrame frame;
		{
			std::unique_lock<std::mutex> lock(videoMutex);
			videoCv.wait_for(lock, std::chrono::milliseconds(100),
					 [this] { return stop.load() || !videoQueue.empty(); });
			if (videoQueue.empty())
				continue;
			frame = std::move(videoQueue.front());
			videoQueue.pop_front();
		}

		obs_source_t *target;
		{
			std::lock_guard<std::mutex> lock(sourceMutex);
			target = source;
		}
		if (!target)
			continue;

		SessionSettings copy;
		{
			std::lock_guard<std::mutex> lock(settingsMutex);
			copy = settings;
		}

		const uint8_t *sps = nullptr;
		size_t spsSize = 0;
		if (findNalOfType(frame.payload.data(), frame.payload.size(), 7, &sps,
				  &spsSize)) {
			H264SpsInfo info;
			if (parseSps(sps, spsSize, &info) && info.valid) {
				if (!localDecoder || info.width != decW || info.height != decH) {
					std::string derr;
					int bitrate = copy.video.bitrateKbps > 0
							      ? copy.video.bitrateKbps
							      : autoBitrateKbps(
									    info.width,
									    info.height,
									    copy.video.fps);
					if (localDecoder) {
						localDecoder->reset();
					} else {
						localDecoder = createVideoDecoder();
					}
					if (!localDecoder->configure(info.width, info.height,
								     copy.video.fps, bitrate,
								     &derr)) {
						blog(LOG_ERROR,
						     "[obs-phone-camera] decoder configure "
						     "failed: %s",
						     derr.c_str());
						localDecoder.reset();
						continue;
					}
					decW = info.width;
					decH = info.height;
					crop = centerCrop(decW, decH, copy.aspect);
					SessionListener *list = listener.load();
					if (list)
						list->onVideoDimensions(decW, decH);
				}
			}
		}
		if (!localDecoder)
			continue;

		std::string derr;
		if (!localDecoder->feed(frame.payload.data(), frame.payload.size(), &derr)) {
			if (!derr.empty())
				blog(LOG_DEBUG, "[obs-phone-camera] decode feed: %s",
				     derr.c_str());
			localDecoder->reset();
			continue;
		}

		DecodedFrame decoded;
		while (localDecoder->take(&decoded)) {
			obs_source_frame out;
			fillNv12Frame(&out, decoded, crop);
			if (out.width == 0)
				continue;
			out.timestamp = os_gettime_ns();
			obs_source_output_video(target, &out);
			if (framesOut.fetch_add(1) == 0)
				blog(LOG_INFO, "[obs-phone-camera] first decoded frame from %s: %ux%u",
				     peerName.c_str(), out.width, out.height);
		}
	}
}

void Session::audioLoop()
{
	uint64_t nextTs = 0;
	while (!stop.load()) {
		ReassembledFrame frame;
		{
			std::unique_lock<std::mutex> lock(audioMutex);
			audioCv.wait_for(lock, std::chrono::milliseconds(100),
					 [this] { return stop.load() || !audioQueue.empty(); });
			if (audioQueue.empty())
				continue;
			frame = std::move(audioQueue.front());
			audioQueue.pop_front();
		}

		obs_source_t *target;
		bool audioEnabled;
		{
			std::lock_guard<std::mutex> lock(sourceMutex);
			target = source;
		}
		{
			std::lock_guard<std::mutex> lock(settingsMutex);
			audioEnabled = settings.audioEnabled;
		}
		if (!target || !audioEnabled)
			continue;
		if (frame.payload.size() < 2 || (frame.payload.size() & 1))
			continue;

		uint64_t now = monotonicNs();
		if (nextTs == 0 || now > nextTs + 200ull * 1000 * 1000)
			nextTs = now;

		uint32_t samples = (uint32_t)(frame.payload.size() / 2);
		obs_source_audio data = {};
		data.data[0] = frame.payload.data();
		data.frames = samples;
		data.timestamp = nextTs;
		data.format = AUDIO_FORMAT_16BIT;
		data.samples_per_sec = 48000;
		data.speakers = SPEAKERS_MONO;
		obs_source_output_audio(target, &data);
		nextTs += (uint64_t)((double)samples * 1000000000.0 / 48000.0);
	}
}

}
}
