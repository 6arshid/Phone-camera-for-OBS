#pragma once

#include "protocol/control_frame.h"
#include "protocol/media_packet.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace ocp {

struct SessionKeys {
	std::array<uint8_t, 32> kAuth{};
	std::array<uint8_t, 32> kCtrlEnc{};
	std::array<uint8_t, 32> kCtrlMac{};
	std::array<uint8_t, 32> kMediaEnc{};
	std::array<uint8_t, 32> kMediaMac{};

	ControlKeys control() const
	{
		ControlKeys k;
		k.enc = kCtrlEnc;
		k.mac = kCtrlMac;
		k.active = true;
		return k;
	}
	MediaKeys media() const
	{
		MediaKeys k;
		k.enc = kMediaEnc;
		k.mac = kMediaMac;
		return k;
	}
};

using MasterKey = std::array<uint8_t, 32>;
using Nonce16 = std::array<uint8_t, 16>;
using ProofHash = std::array<uint8_t, 32>;

MasterKey masterFromSecretHex(const std::string &hex32);
MasterKey masterFromCode(const std::string &code, const uint8_t salt[16], uint32_t iterations);

ProofHash transcriptHash(std::string_view helloPayload, std::string_view challengePayload);

SessionKeys deriveSessionKeys(const MasterKey &master, const uint8_t clientNonce[16],
			      const uint8_t serverNonce[16]);

// proof = HMAC-SHA256(k_auth, "phone" || client_nonce || server_nonce || transcript)
std::string computePhoneProof(const std::array<uint8_t, 32> &kAuth,
			      const uint8_t clientNonce[16], const uint8_t serverNonce[16],
			      const ProofHash &transcript);

// proof = HMAC-SHA256(k_auth, "obs" || server_nonce || client_nonce || transcript)
// Note the reversed nonce order compared to the phone proof.
std::string computeObsProof(const std::array<uint8_t, 32> &kAuth,
			    const uint8_t clientNonce[16], const uint8_t serverNonce[16],
			    const ProofHash &transcript);

bool verifyPhoneProof(const std::array<uint8_t, 32> &kAuth, const uint8_t clientNonce[16],
		      const uint8_t serverNonce[16], const ProofHash &transcript,
		      const std::string &proofHex);

bool verifyObsProof(const std::array<uint8_t, 32> &kAuth, const uint8_t clientNonce[16],
		    const uint8_t serverNonce[16], const ProofHash &transcript,
		    const std::string &proofHex);

struct PairingUrl {
	std::string ip;
	uint16_t port = 0;
	std::string secretHex;
	std::string name;
};

std::string makePairingUrl(const PairingUrl &url);
bool parsePairingUrl(const std::string &text, PairingUrl *out);

std::string generatePairingCode();
bool isValidPairingCode(const std::string &code);

}
