#include "test_framework.h"

#include "crypto/hex.h"
#include "pairing/pairing.h"
#include "protocol/messages.h"

#include <cstring>
#include <string>

using namespace ocp;

namespace {

std::string hex32(uint8_t seed)
{
	uint8_t buf[32];
	for (size_t i = 0; i < 32; i++)
		buf[i] = uint8_t(seed + i);
	return toHex(buf, 32);
}

}

TEST(pairing_url_round_trip)
{
	PairingUrl in;
	in.ip = "192.168.1.42";
	in.port = 47100;
	in.secretHex = hex32(7);
	in.name = "Studio PC";

	std::string url = makePairingUrl(in);
	PairingUrl out;
	CHECK(parsePairingUrl(url, &out));
	CHECK_STR_EQ(out.ip, in.ip);
	CHECK_EQ(out.port, in.port);
	CHECK_STR_EQ(out.secretHex, in.secretHex);
	CHECK_STR_EQ(out.name, in.name);

	CHECK(!parsePairingUrl("http://example.com", &out));
	PairingUrl junk;
	CHECK(!parsePairingUrl("ocp1://pair?h=1.2.3.4", &junk));
	CHECK(!parsePairingUrl("ocp1://pair?h=&p=0&s=&n=", &junk));
}

TEST(pairing_code_validation)
{
	std::string code = generatePairingCode();
	CHECK(isValidPairingCode(code));
	CHECK(!isValidPairingCode("12345"));
	CHECK(!isValidPairingCode("1234567"));
	CHECK(!isValidPairingCode("12345a"));
}

TEST(pairing_session_keys_agree)
{
	MasterKey master = masterFromSecretHex(hex32(42));

	uint8_t clientNonce[16], serverNonce[16];
	for (int i = 0; i < 16; i++) {
		clientNonce[i] = uint8_t(i + 1);
		serverNonce[i] = uint8_t(0xf0 - i);
	}

	SessionKeys a = deriveSessionKeys(master, clientNonce, serverNonce);
	SessionKeys b = deriveSessionKeys(master, clientNonce, serverNonce);
	CHECK(a.kAuth == b.kAuth);
	CHECK(a.kCtrlEnc == b.kCtrlEnc);
	CHECK(a.kMediaEnc == b.kMediaEnc);
	CHECK(memcmp(a.kAuth.data(), b.kAuth.data(), 32) == 0);

	SessionKeys c = deriveSessionKeys(master, serverNonce, clientNonce);
	CHECK(memcmp(a.kAuth.data(), c.kAuth.data(), 32) != 0);
}

TEST(pairing_proofs_use_distinct_nonce_order)
{
	MasterKey master = masterFromSecretHex(hex32(3));
	uint8_t clientNonce[16], serverNonce[16];
	memset(clientNonce, 0x11, sizeof(clientNonce));
	memset(serverNonce, 0x22, sizeof(serverNonce));

	SessionKeys keys = deriveSessionKeys(master, clientNonce, serverNonce);
	ProofHash transcript = transcriptHash(R"({"type":"hello"})", R"({"type":"challenge"})");

	// PROTOCOL.md: phone proof = HMAC(k_auth, "phone" || client || server || transcript)
	std::string phoneProof = computePhoneProof(keys.kAuth, clientNonce, serverNonce,
						   transcript);
	// PROTOCOL.md: obs proof = HMAC(k_auth, "obs" || server || client || transcript)
	std::string obsProof =
	    computeObsProof(keys.kAuth, clientNonce, serverNonce, transcript);

	CHECK(phoneProof.size() == 64);
	CHECK(obsProof.size() == 64);
	CHECK(phoneProof != obsProof);

	CHECK(verifyPhoneProof(keys.kAuth, clientNonce, serverNonce, transcript, phoneProof));
	CHECK(verifyObsProof(keys.kAuth, clientNonce, serverNonce, transcript, obsProof));
	// A role's proof must not validate under the other role.
	CHECK(!verifyObsProof(keys.kAuth, clientNonce, serverNonce, transcript, phoneProof));
	CHECK(!verifyPhoneProof(keys.kAuth, clientNonce, serverNonce, transcript, obsProof));

	// Tampered proof must fail.
	CHECK(!verifyPhoneProof(keys.kAuth, clientNonce, serverNonce, transcript,
				phoneProof.substr(0, 63) + "0"));

	// Because the two roles bind nonces in opposite order, the obs proof must
	// depend on the nonce order: swapping the nonces changes the obs proof.
	std::string obsSwapped =
	    computeObsProof(keys.kAuth, serverNonce, clientNonce, transcript);
	CHECK(obsProof != obsSwapped);
	// The phone proof is bound to its own order and must not validate with
	// nonces passed swapped.
	CHECK(!verifyPhoneProof(keys.kAuth, serverNonce, clientNonce, transcript,
				phoneProof));

	ProofHash other = transcriptHash(R"({"type":"hello"})", R"({"type":"challenge2"})");
	CHECK(!verifyPhoneProof(keys.kAuth, clientNonce, serverNonce, other, phoneProof));
	CHECK(!verifyObsProof(keys.kAuth, clientNonce, serverNonce, other, obsProof));
}

TEST(pairing_code_derivation)
{
	uint8_t salt[16];
	memset(salt, 0x5a, sizeof(salt));

	MasterKey a = masterFromCode("123456", salt, 100000);
	MasterKey b = masterFromCode("123456", salt, 100000);
	MasterKey c = masterFromCode("654321", salt, 100000);
	MasterKey d = masterFromCode("123456", salt, 100001);
	CHECK(a == b);
	CHECK(!(a == c));
	CHECK(!(a == d));

	uint8_t salt2[16];
	memset(salt2, 0x5b, sizeof(salt2));
	MasterKey e = masterFromCode("123456", salt2, 100000);
	CHECK(!(a == e));
}

TEST(pairing_secret_hex)
{
	MasterKey m = masterFromSecretHex(hex32(9));
	uint8_t raw[32];
	fromHex(hex32(9), raw, 32);
	CHECK(memcmp(m.data(), raw, 32) == 0);

	MasterKey invalid = masterFromSecretHex("nothex");
	uint8_t zeros[32] = {0};
	CHECK(memcmp(invalid.data(), zeros, 32) == 0);
}
