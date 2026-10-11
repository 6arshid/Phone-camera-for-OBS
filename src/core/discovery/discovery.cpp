#include "discovery/discovery.h"

#include "json/json.h"

namespace ocp {

std::string encodeObsAnnounce(const ObsAnnounce &announce)
{
	json::Value v = json::Value::object();
	v.set("t", "obs");
	v.set("v", int64_t(announce.version));
	v.set("id", announce.id);
	v.set("name", announce.name);
	v.set("tcp", int64_t(announce.tcp));
	v.set("udp", int64_t(announce.udp));
	return v.dump();
}

std::string encodePhoneAnnounce(const PhoneAnnounce &announce)
{
	json::Value v = json::Value::object();
	v.set("t", "phone");
	v.set("v", int64_t(announce.version));
	v.set("id", announce.id);
	v.set("name", announce.name);
	v.set("os", announce.os);
	v.set("proto", announce.proto);
	return v.dump();
}

DiscoveryMessage parseDiscovery(const char *data, size_t len)
{
	DiscoveryMessage out;
	if (!data || len == 0 || len > 4096)
		return out;

	std::string text(data, len);
	std::string error;
	json::Value v = json::Value::parse(text, &error);
	if (!error.empty() || !v.isObject())
		return out;

	if (v.getInt("v", 0) != 1)
		return out;

	std::string t = v.getString("t");
	if (t == "obs") {
		ObsAnnounce obs;
		obs.version = 1;
		obs.id = v.getString("id");
		obs.name = v.getString("name");
		int64_t tcp = v.getInt("tcp", 0);
		int64_t udp = v.getInt("udp", 0);
		if (obs.id.empty() || tcp <= 0 || tcp > 65535 || udp <= 0 || udp > 65535)
			return out;
		obs.tcp = uint16_t(tcp);
		obs.udp = uint16_t(udp);
		out.kind = DiscoveryKind::Obs;
		out.obs = std::move(obs);
	} else if (t == "phone") {
		PhoneAnnounce phone;
		phone.version = 1;
		phone.id = v.getString("id");
		phone.name = v.getString("name");
		phone.os = v.getString("os");
		phone.proto = v.getString("proto", "ocp/1");
		if (phone.id.empty() || phone.proto != "ocp/1")
			return out;
		out.kind = DiscoveryKind::Phone;
		out.phone = std::move(phone);
	}
	return out;
}

}
