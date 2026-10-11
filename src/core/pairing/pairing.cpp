#include "pairing/pairing.h"

#include "protocol/constants.h"

#include "crypto/hex.h"
#include "crypto/hkdf.h"
#include "crypto/hmac.h"
#include "crypto/pbkdf2.h"
#include "crypto/random.h"
#include "crypto/sha256.h"
#include <cstdio>

#include <cstring>

namespace ocp {

namespace {

const char kInfoAuth[] = "ocp1-auth";
const char kInfoCtrl[] = "ocp1-ctrl";
const char kInfoMedia[] = "ocp1-media";

std::string urlEncode(const std::string &in)
{
	std::string out;
	static const char *hex = "0123456789ABCDEF";
	for (unsigned char c : in) {
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
		    c == '-' || c == '_' || c == '.' || c == '~') {
			out.push_back(char(c));
		} else {
			out.push_back('%');
			out.push_back(hex[c >> 4]);
			out.push_back(hex[c & 0xf]);
		}
	}
	return out;
}

bool urlDecode(const std::string &in, std::string *out)
{
	out->clear();
	for (size_t i = 0; i < in.size(); i++) {
		char c = in[i];
		if (c == '%') {
			if (i + 2 >= in.size())
				return false;
			auto nibble = [](char ch) -> int {
				if (ch >= '0' && ch <= '9')
					return ch - '0';
				if (ch >= 'A' && ch <= 'F')
					return ch - 'A' + 10;
				if (ch >= 'a' && ch <= 'f')
					return ch - 'a' + 10;
				return -1;
			};
			int hi = nibble(in[i + 1]);
			int lo = nibble(in[i + 2]);
			if (hi < 0 || lo < 0)
				return false;
			out->push_back(char((hi << 4) | lo));
			i += 2;
		} else if (c == '+') {
			out->push_back(' ');
		} else {
			out->push_back(c);
		}
	}
	return true;
}

}

MasterKey masterFromSecretHex(const std::string &hex32)
{
	MasterKey master{};
	fromHex(hex32, master.data(), master.size());
	return master;
}

MasterKey masterFromCode(const std::string &code, const uint8_t salt[16], uint32_t iterations)
{
	MasterKey master{};
	if (iterations < kMinPbkdf2Iterations)
		iterations = kMinPbkdf2Iterations;
	pbkdf2Sha256(reinterpret_cast<const uint8_t *>(code.data()), code.size(), salt, 16,
		     iterations, master.data(), master.size());
	return master;
}

ProofHash transcriptHash(std::string_view helloPayload, std::string_view challengePayload)
{
	Sha256 h;
	h.update(reinterpret_cast<const uint8_t *>(helloPayload.data()), helloPayload.size());
	h.update(reinterpret_cast<const uint8_t *>(challengePayload.data()),
		 challengePayload.size());
	ProofHash out{};
	h.final(out.data());
	return out;
}

SessionKeys deriveSessionKeys(const MasterKey &master, const uint8_t clientNonce[16],
			      const uint8_t serverNonce[16])
{
	uint8_t salt[32];
	memcpy(salt, clientNonce, 16);
	memcpy(salt + 16, serverNonce, 16);

	SessionKeys keys;
	hkdfSha256(salt, sizeof(salt), master.data(), master.size(),
		   reinterpret_cast<const uint8_t *>(kInfoAuth), strlen(kInfoAuth),
		   keys.kAuth.data(), keys.kAuth.size());

	uint8_t ctrl[64];
	hkdfSha256(salt, sizeof(salt), master.data(), master.size(),
		   reinterpret_cast<const uint8_t *>(kInfoCtrl), strlen(kInfoCtrl), ctrl,
		   sizeof(ctrl));
	memcpy(keys.kCtrlEnc.data(), ctrl, 32);
	memcpy(keys.kCtrlMac.data(), ctrl + 32, 32);

	uint8_t media[64];
	hkdfSha256(salt, sizeof(salt), master.data(), master.size(),
		   reinterpret_cast<const uint8_t *>(kInfoMedia), strlen(kInfoMedia), media,
		   sizeof(media));
	memcpy(keys.kMediaEnc.data(), media, 32);
	memcpy(keys.kMediaMac.data(), media + 32, 32);

	memset(ctrl, 0, sizeof(ctrl));
	memset(media, 0, sizeof(media));
	memset(salt, 0, sizeof(salt));
	return keys;
}

namespace {

std::string proofWithOrder(const std::array<uint8_t, 32> &kAuth, const char *role,
			   const uint8_t *first, const uint8_t *second,
			   const ProofHash &transcript)
{
	HmacSha256 h;
	h.init(kAuth.data(), kAuth.size());
	h.update(reinterpret_cast<const uint8_t *>(role), strlen(role));
	h.update(first, 16);
	h.update(second, 16);
	h.update(transcript.data(), transcript.size());
	uint8_t out[32];
	h.final(out);
	return toHex(out, sizeof(out));
}

bool proofMatches(const std::string &expected, const std::string &proofHex)
{
	if (expected.size() != proofHex.size())
		return false;
	uint8_t diff = 0;
	for (size_t i = 0; i < expected.size(); i++)
		diff |= uint8_t(expected[i]) ^ uint8_t(proofHex[i]);
	return diff == 0;
}

}

std::string computePhoneProof(const std::array<uint8_t, 32> &kAuth,
			      const uint8_t clientNonce[16], const uint8_t serverNonce[16],
			      const ProofHash &transcript)
{
	return proofWithOrder(kAuth, "phone", clientNonce, serverNonce, transcript);
}

std::string computeObsProof(const std::array<uint8_t, 32> &kAuth,
			    const uint8_t clientNonce[16], const uint8_t serverNonce[16],
			    const ProofHash &transcript)
{
	return proofWithOrder(kAuth, "obs", serverNonce, clientNonce, transcript);
}

bool verifyPhoneProof(const std::array<uint8_t, 32> &kAuth, const uint8_t clientNonce[16],
		      const uint8_t serverNonce[16], const ProofHash &transcript,
		      const std::string &proofHex)
{
	return proofMatches(computePhoneProof(kAuth, clientNonce, serverNonce, transcript),
			    proofHex);
}

bool verifyObsProof(const std::array<uint8_t, 32> &kAuth, const uint8_t clientNonce[16],
		    const uint8_t serverNonce[16], const ProofHash &transcript,
		    const std::string &proofHex)
{
	return proofMatches(computeObsProof(kAuth, clientNonce, serverNonce, transcript),
			    proofHex);
}

std::string makePairingUrl(const PairingUrl &url)
{
	std::string out = "ocp1://pair?h=";
	out += url.ip;
	out += "&p=" + std::to_string(url.port);
	out += "&s=" + url.secretHex;
	out += "&n=" + urlEncode(url.name);
	return out;
}

bool parsePairingUrl(const std::string &text, PairingUrl *out)
{
	if (!out)
		return false;
	const std::string prefix = "ocp1://pair?";
	if (text.rfind(prefix, 0) != 0)
		return false;

	std::string query = text.substr(prefix.size());
	PairingUrl parsed;
	size_t pos = 0;
	while (pos < query.size()) {
		size_t amp = query.find('&', pos);
		if (amp == std::string::npos)
			amp = query.size();
		std::string pair = query.substr(pos, amp - pos);
		pos = amp + 1;

		size_t eq = pair.find('=');
		if (eq == std::string::npos)
			continue;
		std::string key = pair.substr(0, eq);
		std::string value = pair.substr(eq + 1);

		if (key == "h") {
			parsed.ip = value;
		} else if (key == "p") {
			int port = atoi(value.c_str());
			if (port <= 0 || port > 65535)
				return false;
			parsed.port = uint16_t(port);
		} else if (key == "s") {
			parsed.secretHex = value;
		} else if (key == "n") {
			if (!urlDecode(value, &parsed.name))
				return false;
		}
	}

	if (parsed.ip.empty() || parsed.port == 0)
		return false;
	uint8_t secret[32];
	if (!fromHex(parsed.secretHex, secret, sizeof(secret)))
		return false;
	memset(secret, 0, sizeof(secret));

	*out = std::move(parsed);
	return true;
}

std::string generatePairingCode()
{
	uint8_t bytes[4];
	if (!randomBytes(bytes, sizeof(bytes)))
		return std::string();
	uint32_t v = (uint32_t(bytes[0]) << 24) | (uint32_t(bytes[1]) << 16) |
		     (uint32_t(bytes[2]) << 8) | uint32_t(bytes[3]);
	v %= 1000000;
	char buf[8];
	snprintf(buf, sizeof(buf), "%06u", v);
	return std::string(buf);
}

bool isValidPairingCode(const std::string &code)
{
	if (code.size() != 6)
		return false;
	for (char c : code) {
		if (c < '0' || c > '9')
			return false;
	}
	return true;
}

}
