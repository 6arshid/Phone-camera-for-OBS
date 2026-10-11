#pragma once

#include "paired_store.h"
#include "session.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ocp {
namespace plugin {

class PhoneSource;

struct DiscoveredPhone {
	std::string id;
	std::string name;
	std::string os;
	std::string address;
	uint64_t lastSeenNs = 0;
};

struct ModuleConfig {
	std::string language = "auto";
	uint16_t controlPort = 47100;
	bool requirePairing = true;
	std::string obsId;
};

class Server {
public:
	static Server &instance();

	bool start(uint16_t port, std::string *err);
	void shutdown();
	bool running() const { return runningFlag; }
	uint16_t controlPort() const { return port_; }
	uint16_t mediaPort() const { return mediaPort_; }

	PairedStore &paired() { return pairedStore; }
	bool requirePairing() const { return requirePairingFlag; }

	std::string pairingCode();
	std::string rotatePairingCode();
	std::string qrSecret();
	std::string rotateQrSecret();
	std::string makeQrUrl();
	std::string addressesSummary();

	void registerSource(PhoneSource *source);
	void unregisterSource(PhoneSource *source);
	void requestAttach(PhoneSource *source);
	void disconnectSource(PhoneSource *source);
	void sessionReady(Session *session);
	std::vector<DiscoveredPhone> phones() const;
	bool hasAuthedSessions() const;

	ModuleConfig loadModuleConfig();
	void saveModuleConfig(const ModuleConfig &cfg);
	void applyModuleConfig(const ModuleConfig &cfg);

private:
	Server() = default;

	void acceptLoop();
	void mediaLoop();
	void discoveryLoop();
	void pruneSessions();
	Session *findSessionByTag(uint64_t tag);
	std::shared_ptr<Session> sessionForSource(PhoneSource *source);

	mutable std::mutex mutex;
	bool runningFlag = false;
	bool stopFlag = false;
	uint16_t port_ = 0;
	uint16_t mediaPort_ = 0;

	int tcpFd = -1;
	int udpFd = -1;
	int discoveryFd = -1;

	std::vector<std::shared_ptr<Session>> sessions;
	std::vector<PhoneSource *> sources;
	std::vector<DiscoveredPhone> discovered;

	PairedStore pairedStore;
	bool requirePairingFlag = true;
	std::string pairingCodeValue;
	uint64_t pairingCodeExpiryNs = 0;
	std::string qrSecretHex;

	std::thread acceptThread;
	std::thread mediaThread;
	std::thread discoveryThread;

	std::string configDirPath;
};

}
}
