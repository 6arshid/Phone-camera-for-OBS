#pragma once

#include "decoder.h"
#include "protocol/control_frame.h"
#include "protocol/media_packet.h"
#include "protocol/messages.h"
#include "protocol/reassembler.h"
#include "settings/config.h"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct obs_source;
typedef struct obs_source obs_source_t;

namespace ocp {
namespace plugin {

class Server;

struct SessionSettings {
	VideoProfile video;
	std::string camera = "back";
	CameraControl cameraCtl;
	bool audioEnabled = false;
	bool streamActive = false;
	std::string aspect = "source";
};

class SessionListener {
public:
	virtual ~SessionListener() = default;
	virtual void onSessionAuthenticated(class Session *) {}
	virtual void onSessionClosed(class Session *, const std::string &reason) {}
	virtual void onPhoneStats(const StatsMessage &stats, double latencyMs, double lossPct)
	{
	}
	virtual void onVideoDimensions(int width, int height) {}
};

class Session : public std::enable_shared_from_this<Session> {
public:
	Session(int fd, uint16_t mediaPort, Server *server);
	~Session();

	void start();
	void requestStop();
	void join();
	bool finished() const { return done.load(); }

	bool attach(obs_source_t *source, SessionListener *listener);
	void detach(SessionListener *listener);
	bool attached() const { return listener.load() != nullptr; }
	bool attachedTo(const SessionListener *other) const
	{
		return listener.load() == other;
	}

	void applySettings(const SessionSettings &settings);
	void sendConfig();

	void onMediaDatagram(const uint8_t *data, size_t len);

	uint64_t mediaTag() const { return sessionTag; }
	std::string deviceId() const { return peerId; }
	std::string deviceName() const { return peerName; }
	bool isAuthed() const { return authed.load(); }
	bool isPaired() const { return pairedFlag; }
	double latencyMs() const { return statLatencyMs.load(); }
	double lossPct() const { return statLossPct.load(); }
	int battery() const { return batteryPct.load(); }
	std::string thermal();
	uint16_t mediaPortValue() const { return mediaPort_; }

private:
	void controlLoop();
	void videoLoop();
	void audioLoop();
	bool handshakePhase(std::string *err);
	bool authenticatedLoop(std::string *err);
	bool handleMessage(const std::string &payload);
	bool sendMessage(const Message &msg);
	bool sendRaw(const std::vector<uint8_t> &frame);
	void pushVideo(ReassembledFrame &&frame);
	void pushAudio(ReassembledFrame &&frame);
	void closeSocket();

	int fd = -1;
	uint16_t mediaPort_ = 0;
	Server *server = nullptr;

	std::atomic<bool> stop{false};
	std::atomic<bool> done{false};
	std::atomic<bool> authed{false};
	std::atomic<bool> mediaReady{false};
	bool pairedFlag = false;
	std::atomic<bool> mediaFailed{false};

	uint64_t sessionTag = 0;
	std::string sessionId;
	std::string peerId;
	std::string peerName;

	ControlFrameEncoder encoder;
	ControlFrameReader reader;
	MediaKeys mediaKeys;
	Reassembler reassembler;

	std::mutex sourceMutex;
	obs_source_t *source = nullptr;
	std::atomic<SessionListener *> listener{nullptr};

	std::mutex settingsMutex;
	SessionSettings settings;

	// Decoded / raw frame queues (producer: media thread; consumer: video/audio threads)
	std::mutex videoMutex;
	std::condition_variable videoCv;
	std::deque<ReassembledFrame> videoQueue;

	std::mutex audioMutex;
	std::condition_variable audioCv;
	std::deque<ReassembledFrame> audioQueue;

	std::atomic<uint64_t> statDelivered{0};
	std::atomic<uint64_t> statLost{0};
	std::atomic<uint64_t> framesOut{0};
	std::atomic<uint64_t> dropped{0};
	std::atomic<double> statLatencyMs{0.0};
	std::atomic<double> statLossPct{0.0};
	std::atomic<double> rttMs{0.0};
	std::atomic<int> batteryPct{-1};
	std::mutex thermalMutex;
	std::string thermalState;
	std::atomic<uint64_t> lastPingSentNs{0};

	std::thread controlThread;
	std::thread videoThread;
	std::thread audioThread;

	friend class Server;
};

}
}
