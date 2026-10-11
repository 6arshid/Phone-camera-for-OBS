#pragma once

#include "server.h"
#include "session.h"

#include <obs.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace ocp {
namespace plugin {

enum class SourceState { Idle, Waiting, Connecting, Streaming, Error };

class PhoneSource : public SessionListener {
public:
	PhoneSource(obs_data_t *settings, obs_source_t *handle);
	~PhoneSource() override;

	static void registerSource();

	obs_source_t *rawSource() const { return handle; }
	std::string preferredDeviceId();

	void updateSettings(obs_data_t *settings);
	void activate();
	void deactivate();
	void show();
	void hide();
	void videoTick(float seconds);
	uint32_t width() const;
	uint32_t height() const;

	void onSessionAuthenticated(Session *session) override;
	void onSessionClosed(Session *session, const std::string &reason) override;
	void onPhoneStats(const StatsMessage &stats, double latencyMs,
			  double lossPct) override;
	void onVideoDimensions(int width, int height) override;

	std::string statusText() const;
	std::string avatarStatusText() const;

private:
	void loadAvatarImage(const std::string &path);
	SessionSettings currentSettings() const;
	void pushSettingsToSession();
	void maybeOutputStatusCard();

	obs_source_t *handle = nullptr;

	mutable std::mutex sessionMutex;
	Session *session = nullptr;

	mutable std::mutex settingsMutex;
	std::string devicePref = "any";
	std::string resolutionPreset = "auto";
	int customWidth = 1920;
	int customHeight = 1080;
	std::string fpsPreset = "auto";
	int bitrateKbps = 0;
	int gopS = 2;
	std::string camera = "back";
	std::string orientation = "auto";
	std::string aspect = "source";
	bool audioEnabled = false;
	bool facePixelation = false;
	int facePixelationStrength = 50;
	AvatarConfig avatar;
	std::string avatarPath;
	int64_t avatarStamp = 0;
	std::shared_ptr<const std::vector<uint8_t>> avatarData;
	std::string avatarStatus;
	bool statusOverlay = true;
	std::string languagePref = "auto";
	bool requirePairingPref = true;
	int controlPortPref = 47100;
	bool streamActive = false;

	std::atomic<int> state{(int)SourceState::Idle};
	std::atomic<int> outWidth{0};
	std::atomic<int> outHeight{0};
	std::atomic<int> batteryPct{-1};
	std::atomic<double> latencyMs{0.0};
	std::atomic<double> lossPct{0.0};
	std::atomic<int> phoneFps{0};

	mutable std::mutex infoMutex;
	std::string phoneName;
	std::string lastError;
	std::string thermalState;

	float tickAccum = 0.0f;
	std::vector<uint8_t> cardBuffer;
	int cardStride = 0;
};

}
}
