#pragma once

#include <cstdint>
#include <string>

namespace ocp {

struct ObsAnnounce {
	int version = 1;
	std::string id;
	std::string name;
	uint16_t tcp = 0;
	uint16_t udp = 0;
};

struct PhoneAnnounce {
	int version = 1;
	std::string id;
	std::string name;
	std::string os;
	std::string proto = "ocp/1";
};

enum class DiscoveryKind { None, Obs, Phone };

struct DiscoveryMessage {
	DiscoveryKind kind = DiscoveryKind::None;
	ObsAnnounce obs;
	PhoneAnnounce phone;
};

std::string encodeObsAnnounce(const ObsAnnounce &announce);
std::string encodePhoneAnnounce(const PhoneAnnounce &announce);

DiscoveryMessage parseDiscovery(const char *data, size_t len);

}
