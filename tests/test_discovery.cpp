#include "test_framework.h"

#include "discovery/discovery.h"

#include <cstring>

using namespace ocp;

TEST(discovery_obs_round_trip)
{
	ObsAnnounce in;
	in.id = "0123456789abcdef0123456789abcdef";
	in.name = "DESKTOP-PC";
	in.tcp = 47100;
	in.udp = 47109;

	std::string encoded = encodeObsAnnounce(in);
	DiscoveryMessage out = parseDiscovery(encoded.data(), encoded.size());
	CHECK(out.kind == DiscoveryKind::Obs);
	CHECK_STR_EQ(out.obs.id, in.id);
	CHECK_STR_EQ(out.obs.name, in.name);
	CHECK_EQ(out.obs.tcp, in.tcp);
	CHECK_EQ(out.obs.udp, in.udp);
}

TEST(discovery_phone_round_trip)
{
	PhoneAnnounce in;
	in.id = "fedcba9876543210fedcba9876543210";
	in.name = "Pixel 7";
	in.os = "android";

	std::string encoded = encodePhoneAnnounce(in);
	DiscoveryMessage out = parseDiscovery(encoded.data(), encoded.size());
	CHECK(out.kind == DiscoveryKind::Phone);
	CHECK_STR_EQ(out.phone.id, in.id);
	CHECK_STR_EQ(out.phone.name, in.name);
	CHECK_STR_EQ(out.phone.os, "android");
	CHECK_STR_EQ(out.phone.proto, "ocp/1");
}

TEST(discovery_rejects_garbage)
{
	const char *cases[] = {"", "{", "null", R"({"t":"unknown","v":1})",
			       R"({"t":"obs","v":2,"id":"x","tcp":1,"udp":1})",
			       R"({"t":"obs","v":1,"id":"","tcp":47100,"udp":47109})",
			       R"({"t":"obs","v":1,"id":"x","tcp":0,"udp":1})",
			       R"({"t":"phone","v":1,"id":"x","proto":"ocp/2"})",
			       "not json at all"};
	for (const char *text : cases) {
		DiscoveryMessage out = parseDiscovery(text, strlen(text));
		CHECK(out.kind == DiscoveryKind::None);
	}
}
