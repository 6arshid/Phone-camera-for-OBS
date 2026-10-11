#include "server.h"

#include "net_util.h"
#include "qr_window.h"
#include "source.h"
#include "tr.h"

#include "crypto/hex.h"
#include "crypto/random.h"
#include "discovery/discovery.h"
#include "pairing/pairing.h"
#include "protocol/constants.h"

#include <obs-module.h>

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace ocp {
namespace plugin {

namespace {

std::once_flag g_wsaOnce;

void ensureWinsock()
{
	std::call_once(g_wsaOnce, [] {
		WSADATA data;
		WSAStartup(MAKEWORD(2, 2), &data);
	});
}

}

Server &Server::instance()
{
	static Server server;
	return server;
}

bool Server::start(uint16_t port, std::string *err)
{
	ensureWinsock();

	std::lock_guard<std::mutex> lock(mutex);
	if (runningFlag)
		return true;

	port_ = port;
	mediaPort_ = (uint16_t)(port + 1);

	// Pairing store + module config live in the OBS module config directory.
	char *cfgPath = obs_module_config_path("");
	if (cfgPath) {
		configDirPath = cfgPath;
		bfree(cfgPath);
		if (!configDirPath.empty() && configDirPath.back() != '\\' &&
		    configDirPath.back() != '/')
			configDirPath += '\\';
		std::error_code ec;
		std::filesystem::create_directories(configDirPath, ec);
	}
	pairedStore.load(configDirPath + "paired.json");

	ModuleConfig cfg = loadModuleConfig();
	if (cfg.controlPort != 0 && cfg.controlPort != port) {
		port_ = cfg.controlPort;
		mediaPort_ = (uint16_t)(port_ + 1);
	}
	requirePairingFlag = cfg.requirePairing;
	if (cfg.language == "en")
		setLanguage(Language::English);
	else if (cfg.language == "fa")
		setLanguage(Language::Persian);
	else
		setLanguage(Language::Auto);

	tcpFd = (int)socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	udpFd = (int)socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	discoveryFd = (int)socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (tcpFd < 0 || udpFd < 0 || discoveryFd < 0) {
		if (err)
			*err = "socket creation failed: " + winSocketError(WSAGetLastError());
		return false;
	}

	int yes = 1;
	setsockopt((SOCKET)tcpFd, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));
	setsockopt((SOCKET)udpFd, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));
	setsockopt((SOCKET)discoveryFd, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes,
		   sizeof(yes));
	setsockopt((SOCKET)discoveryFd, SOL_SOCKET, SO_BROADCAST, (const char *)&yes,
		   sizeof(yes));

	sockaddr_in addr = {};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(port_);
	if (bind((SOCKET)tcpFd, (sockaddr *)&addr, sizeof(addr)) != 0) {
		if (err)
			*err = "cannot bind control port " + std::to_string(port_) + ": " +
			       winSocketError(WSAGetLastError());
		return false;
	}
	if (listen((SOCKET)tcpFd, 8) != 0) {
		if (err)
			*err = "listen failed: " + winSocketError(WSAGetLastError());
		return false;
	}

	addr.sin_port = htons(mediaPort_);
	if (bind((SOCKET)udpFd, (sockaddr *)&addr, sizeof(addr)) != 0) {
		if (err)
			*err = "cannot bind media port " + std::to_string(mediaPort_) + ": " +
			       winSocketError(WSAGetLastError());
		return false;
	}

	addr.sin_port = htons(kDefaultDiscoveryPort);
	if (bind((SOCKET)discoveryFd, (sockaddr *)&addr, sizeof(addr)) != 0) {
		if (err)
			*err = "cannot bind discovery port: " +
			       winSocketError(WSAGetLastError());
		return false;
	}
	ip_mreq mreq = {};
	mreq.imr_multiaddr.s_addr = inet_addr(kMulticastGroup);
	mreq.imr_interface.s_addr = htonl(INADDR_ANY);
	setsockopt((SOCKET)discoveryFd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char *)&mreq,
		   sizeof(mreq));

	stopFlag = false;
	runningFlag = true;
	acceptThread = std::thread(&Server::acceptLoop, this);
	mediaThread = std::thread(&Server::mediaLoop, this);
	discoveryThread = std::thread(&Server::discoveryLoop, this);

	blog(LOG_INFO, "[obs-phone-camera] server listening on tcp/%u udp/%u",
	     (unsigned)port_, (unsigned)mediaPort_);
	return true;
}

void Server::shutdown()
{
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (!runningFlag)
			return;
		stopFlag = true;
		if (tcpFd >= 0) {
			closesocket((SOCKET)tcpFd);
			tcpFd = -1;
		}
		if (udpFd >= 0) {
			closesocket((SOCKET)udpFd);
			udpFd = -1;
		}
		if (discoveryFd >= 0) {
			closesocket((SOCKET)discoveryFd);
			discoveryFd = -1;
		}
	}

	if (acceptThread.joinable())
		acceptThread.join();
	if (mediaThread.joinable())
		mediaThread.join();
	if (discoveryThread.joinable())
		discoveryThread.join();

	std::vector<std::shared_ptr<Session>> toClose;
	{
		std::lock_guard<std::mutex> lock(mutex);
		toClose.swap(sessions);
		sources.clear();
		runningFlag = false;
	}
	for (auto &session : toClose) {
		session->requestStop();
		session->join();
	}

	closePairingQrWindow();
	blog(LOG_INFO, "[obs-phone-camera] server stopped");
}

std::string Server::pairingCode()
{
	std::lock_guard<std::mutex> lock(mutex);
	uint64_t now = monotonicNs();
	if (pairingCodeValue.empty() || now >= pairingCodeExpiryNs) {
		pairingCodeValue = generatePairingCode();
		pairingCodeExpiryNs = now + 300ull * 1000 * 1000 * 1000;
	}
	return pairingCodeValue;
}

std::string Server::rotatePairingCode()
{
	std::lock_guard<std::mutex> lock(mutex);
	pairingCodeValue = generatePairingCode();
	pairingCodeExpiryNs = monotonicNs() + 300ull * 1000 * 1000 * 1000;
	return pairingCodeValue;
}

std::string Server::qrSecret()
{
	std::lock_guard<std::mutex> lock(mutex);
	if (qrSecretHex.empty()) {
		uint8_t bytes[kSecretSize];
		if (randomBytes(bytes, sizeof(bytes)))
			qrSecretHex = toHex(bytes, sizeof(bytes));
	}
	return qrSecretHex;
}

std::string Server::rotateQrSecret()
{
	std::lock_guard<std::mutex> lock(mutex);
	uint8_t bytes[kSecretSize];
	if (randomBytes(bytes, sizeof(bytes)))
		qrSecretHex = toHex(bytes, sizeof(bytes));
	return qrSecretHex;
}

std::string Server::makeQrUrl()
{
	PairingUrl url;
	url.ip = firstLocalIPv4();
	url.port = port_;
	url.secretHex = qrSecret();
	url.name = "OBS Studio";
	return makePairingUrl(url);
}

std::string Server::addressesSummary()
{
	auto ips = localIPv4Addresses();
	std::string out;
	for (const std::string &ip : ips) {
		if (!out.empty())
			out += ", ";
		out += ip;
	}
	return out.empty() ? std::string("127.0.0.1") : out;
}

void Server::registerSource(PhoneSource *source)
{
	std::lock_guard<std::mutex> lock(mutex);
	sources.push_back(source);
}

void Server::unregisterSource(PhoneSource *source)
{
	disconnectSource(source);
	std::lock_guard<std::mutex> lock(mutex);
	for (auto it = sources.begin(); it != sources.end(); ++it) {
		if (*it == source) {
			sources.erase(it);
			break;
		}
	}
}

void Server::requestAttach(PhoneSource *source)
{
	std::vector<std::shared_ptr<Session>> snapshot;
	{
		std::lock_guard<std::mutex> lock(mutex);
		snapshot = sessions;
	}
	for (auto &session : snapshot) {
		if (session->attached() || !session->isAuthed())
			continue;
		std::string want = source->preferredDeviceId();
		if (want != "any" && want != session->deviceId())
			continue;
		if (session->attach(source->rawSource(), source))
			return;
	}
}

void Server::disconnectSource(PhoneSource *source)
{
	std::vector<std::shared_ptr<Session>> snapshot;
	{
		std::lock_guard<std::mutex> lock(mutex);
		snapshot = sessions;
	}
	for (auto &session : snapshot) {
		if (!session->attachedTo(source))
			continue;
		session->detach(source);
		session->requestStop();
		session->join();
	}
	{
		std::lock_guard<std::mutex> lock(mutex);
		sessions.erase(std::remove_if(sessions.begin(), sessions.end(),
					      [](const std::shared_ptr<Session> &s) {
						      return s->finished();
					      }),
			       sessions.end());
	}
}

std::vector<DiscoveredPhone> Server::phones() const
{
	uint64_t now = monotonicNs();
	std::vector<DiscoveredPhone> out;
	std::lock_guard<std::mutex> lock(mutex);
	for (const DiscoveredPhone &phone : discovered) {
		if (now - phone.lastSeenNs < 90ull * 1000 * 1000 * 1000)
			out.push_back(phone);
	}
	return out;
}

ModuleConfig Server::loadModuleConfig()
{
	ModuleConfig cfg;
	if (cfg.obsId.empty()) {
		uint8_t id[8];
		if (randomBytes(id, sizeof(id)))
			cfg.obsId = toHex(id, sizeof(id));
	}

	FILE *f = fopen((configDirPath + "config.json").c_str(), "rb");
	if (!f)
		return cfg;
	std::string text;
	char buf[4096];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
		text.append(buf, n);
	fclose(f);

	std::string perr;
	json::Value root = json::Value::parse(text, &perr);
	if (!perr.empty() || !root.isObject())
		return cfg;

	cfg.language = root.getString("language", cfg.language);
	cfg.requirePairing = root.getBool("require_pairing", cfg.requirePairing);
	cfg.controlPort = (uint16_t)root.getInt("control_port", cfg.controlPort);
	cfg.obsId = root.getString("obs_id", cfg.obsId);
	if (cfg.obsId.empty()) {
		uint8_t id[8];
		if (randomBytes(id, sizeof(id)))
			cfg.obsId = toHex(id, sizeof(id));
	}
	return cfg;
}

void Server::saveModuleConfig(const ModuleConfig &cfg)
{
	json::Value root = json::Value::object();
	root.set("language", cfg.language);
	root.set("require_pairing", cfg.requirePairing);
	root.set("control_port", (int)cfg.controlPort);
	root.set("obs_id", cfg.obsId);

	FILE *f = fopen((configDirPath + "config.json").c_str(), "wb");
	if (!f)
		return;
	std::string text = root.dump();
	fwrite(text.data(), 1, text.size(), f);
	fclose(f);
}

void Server::applyModuleConfig(const ModuleConfig &cfg)
{
	{
		std::lock_guard<std::mutex> lock(mutex);
		requirePairingFlag = cfg.requirePairing;
	}
	if (cfg.language == "en")
		setLanguage(Language::English);
	else if (cfg.language == "fa")
		setLanguage(Language::Persian);
	else
		setLanguage(Language::Auto);
}

void Server::acceptLoop()
{
	while (true) {
		{
			std::lock_guard<std::mutex> lock(mutex);
			if (stopFlag)
				break;
		}

		fd_set set;
		FD_ZERO(&set);
		int sock;
		{
			std::lock_guard<std::mutex> lock(mutex);
			sock = tcpFd;
		}
		if (sock < 0)
			break;
		FD_SET((SOCKET)sock, &set);
		timeval tv = {0, 500000};
		int rc = select(0, &set, nullptr, nullptr, &tv);
		if (rc > 0 && FD_ISSET((SOCKET)sock, &set)) {
			sockaddr_in peer = {};
			int peerLen = sizeof(peer);
			int client = (int)accept((SOCKET)sock, (sockaddr *)&peer, &peerLen);
			if (client >= 0) {
				int nodelay = 1;
				setsockopt((SOCKET)client, IPPROTO_TCP, TCP_NODELAY,
					   (const char *)&nodelay, sizeof(nodelay));
				uint16_t mport;
				{
					std::lock_guard<std::mutex> lock(mutex);
					mport = mediaPort_;
				}
				auto session = std::make_shared<Session>(client, mport, this);
				{
					std::lock_guard<std::mutex> lock(mutex);
					sessions.push_back(session);
				}
				blog(LOG_INFO,
				     "[obs-phone-camera] incoming connection from %s",
				     inet_ntoa(peer.sin_addr));
				session->start();
			}
		}
		pruneSessions();
	}
}

void Server::mediaLoop()
{
	while (true) {
		int sock;
		{
			std::lock_guard<std::mutex> lock(mutex);
			if (stopFlag)
				break;
			sock = udpFd;
		}
		if (sock < 0)
			break;

		fd_set set;
		FD_ZERO(&set);
		FD_SET((SOCKET)sock, &set);
		timeval tv = {0, 300000};
		int rc = select(0, &set, nullptr, nullptr, &tv);
		if (rc <= 0 || !FD_ISSET((SOCKET)sock, &set))
			continue;

		uint8_t buf[2048];
		int n = recvfrom((SOCKET)sock, (char *)buf, sizeof(buf), 0, nullptr, nullptr);
		if (n < 12 || memcmp(buf, kMediaMagic, 4) != 0)
			continue;
		uint64_t tag = 0;
		for (int i = 0; i < 8; i++)
			tag = (tag << 8) | buf[8 + i];
		Session *session = findSessionByTag(tag);
		if (session)
			session->onMediaDatagram(buf, (size_t)n);
	}
}

void Server::discoveryLoop()
{
	auto nextAnnounce = monotonicNs();

	ObsAnnounce announce;
	announce.version = 1;
	{
		std::lock_guard<std::mutex> lock(mutex);
		ModuleConfig cfg = loadModuleConfig();
		announce.id = cfg.obsId;
		announce.name = "OBS Studio";
		announce.tcp = port_;
		announce.udp = mediaPort_;
	}
	std::string payload = encodeObsAnnounce(announce);

	while (true) {
		int sock;
		{
			std::lock_guard<std::mutex> lock(mutex);
			if (stopFlag)
				break;
			sock = discoveryFd;
		}
		if (sock < 0)
			break;

		fd_set set;
		FD_ZERO(&set);
		FD_SET((SOCKET)sock, &set);
		timeval tv = {0, 400000};
		int rc = select(0, &set, nullptr, nullptr, &tv);
		if (rc > 0 && FD_ISSET((SOCKET)sock, &set)) {
			char buf[1024];
			sockaddr_in from = {};
			int fromLen = sizeof(from);
			int n = recvfrom((SOCKET)sock, buf, sizeof(buf) - 1, 0,
					 (sockaddr *)&from, &fromLen);
			if (n > 0) {
				buf[n] = 0;
				DiscoveryMessage msg = parseDiscovery(buf, (size_t)n);
				if (msg.kind == DiscoveryKind::Phone && !msg.phone.id.empty()) {
					std::lock_guard<std::mutex> lock(mutex);
					DiscoveredPhone *found = nullptr;
					for (DiscoveredPhone &p : discovered) {
						if (p.id == msg.phone.id) {
							found = &p;
							break;
						}
					}
					if (!found) {
						discovered.push_back({});
						found = &discovered.back();
						found->id = msg.phone.id;
					}
					if (!msg.phone.name.empty())
						found->name = msg.phone.name;
					if (!msg.phone.os.empty())
						found->os = msg.phone.os;
					found->address = inet_ntoa(from.sin_addr);
					found->lastSeenNs = monotonicNs();
				}
			}
		}

		uint64_t now = monotonicNs();
		if (now >= nextAnnounce) {
			nextAnnounce = now + 2ull * 1000 * 1000 * 1000;
			sockaddr_in to = {};
			to.sin_family = AF_INET;
			to.sin_port = htons(kDefaultDiscoveryPort);

			to.sin_addr.s_addr = htonl(INADDR_BROADCAST);
			sendto((SOCKET)sock, payload.data(), (int)payload.size(), 0,
			       (sockaddr *)&to, sizeof(to));

			to.sin_addr.s_addr = inet_addr(kMulticastGroup);
			sendto((SOCKET)sock, payload.data(), (int)payload.size(), 0,
			       (sockaddr *)&to, sizeof(to));
		}
	}
}

void Server::pruneSessions()
{
	std::lock_guard<std::mutex> lock(mutex);
	sessions.erase(std::remove_if(sessions.begin(), sessions.end(),
				      [](const std::shared_ptr<Session> &s) {
					      return s->finished();
				      }),
		       sessions.end());
}

bool Server::hasAuthedSessions() const
{
	std::lock_guard<std::mutex> lock(mutex);
	for (const auto &session : sessions) {
		if (session->isAuthed() && !session->finished())
			return true;
	}
	return false;
}

void Server::sessionReady(Session *session)
{
	std::vector<PhoneSource *> waiting;
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (stopFlag)
			return;
		waiting = sources;
	}

	std::string wantSessionDevice = session->deviceId();
	for (PhoneSource *source : waiting) {
		if (session->attached() || !session->isAuthed())
			return;
		std::string want = source->preferredDeviceId();
		if (want != "any" && want != wantSessionDevice)
			continue;
		if (session->attach(source->rawSource(), source))
			return;
	}
}

Session *Server::findSessionByTag(uint64_t tag)
{
	std::lock_guard<std::mutex> lock(mutex);
	for (auto &session : sessions) {
		if (session->mediaTag() == tag && session->isAuthed())
			return session.get();
	}
	return nullptr;
}

}
}
