#include "test_framework.h"

#include "crypto/chacha20.h"
#include "crypto/hex.h"
#include "crypto/hkdf.h"
#include "crypto/hmac.h"
#include "crypto/pbkdf2.h"
#include "crypto/random.h"
#include "crypto/sha256.h"

#include <cstring>
#include <string>
#include <vector>

using namespace ocp;

namespace {

std::string sha256Hex(const std::string &input)
{
	uint8_t out[32];
	Sha256::hash(reinterpret_cast<const uint8_t *>(input.data()), input.size(), out);
	return toHex(out, 32);
}

std::string hmacHex(const std::string &key, const std::string &data)
{
	uint8_t out[32];
	HmacSha256::oneShot(reinterpret_cast<const uint8_t *>(key.data()), key.size(),
			    reinterpret_cast<const uint8_t *>(data.data()), data.size(), out);
	return toHex(out, 32);
}

std::vector<uint8_t> unhex(const std::string &hex)
{
	std::vector<uint8_t> out(hex.size() / 2);
	fromHex(hex, out.data(), out.size());
	return out;
}

}

TEST(sha256_vectors)
{
	CHECK_STR_EQ(sha256Hex(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
	CHECK_STR_EQ(sha256Hex("abc"),
		     "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	CHECK_STR_EQ(sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
		     "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
	CHECK_STR_EQ(sha256Hex(std::string(1000000, 'a')),
		     "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST(sha256_incremental_matches_oneshot)
{
	std::string data = "The quick brown fox jumps over the lazy dog";
	uint8_t oneShot[32];
	Sha256::hash(reinterpret_cast<const uint8_t *>(data.data()), data.size(), oneShot);

	for (size_t split : {size_t(1), size_t(7), size_t(33), size_t(42)}) {
		Sha256 h;
		h.update(reinterpret_cast<const uint8_t *>(data.data()), split);
		h.update(reinterpret_cast<const uint8_t *>(data.data()) + split, data.size() - split);
		uint8_t out[32];
		h.final(out);
		CHECK(memcmp(oneShot, out, 32) == 0);
	}
}

TEST(hmac_sha256_rfc4231)
{
	std::vector<uint8_t> key20(20, 0x0b);
	std::vector<uint8_t> hiThere = unhex(
	    "4869205468657265");
	{
		uint8_t out[32];
		HmacSha256::oneShot(key20.data(), key20.size(), hiThere.data(), hiThere.size(), out);
		CHECK_STR_EQ(toHex(out, 32),
			     "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
	}
	CHECK_STR_EQ(hmacHex("Jefe", "what do ya want for nothing?"),
		     "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");

	std::vector<uint8_t> key131(131, 0xaa);
	std::string case6Data = "Test Using Larger Than Block-Size Key - Hash Key First";
	uint8_t out[32];
	HmacSha256::oneShot(key131.data(), key131.size(),
			    reinterpret_cast<const uint8_t *>(case6Data.data()), case6Data.size(),
			    out);
	CHECK_STR_EQ(toHex(out, 32),
		     "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");
}

TEST(hkdf_rfc5869_case1)
{
	auto ikm = unhex("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b");
	auto salt = unhex("000102030405060708090a0b0c");
	auto info = unhex("f0f1f2f3f4f5f6f7f8f9");
	uint8_t okm[42];
	CHECK(hkdfSha256(salt.data(), salt.size(), ikm.data(), ikm.size(), info.data(), info.size(),
			 okm, sizeof(okm)));
	CHECK_STR_EQ(toHex(okm, sizeof(okm)),
		     "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b88718"
		     "5865");
}

TEST(pbkdf2_hmac_sha256_vectors)
{
	auto run = [](const char *pw, const char *salt, uint32_t iter, size_t dkLen) {
		std::vector<uint8_t> out(dkLen);
		pbkdf2Sha256(reinterpret_cast<const uint8_t *>(pw), strlen(pw),
			     reinterpret_cast<const uint8_t *>(salt), strlen(salt), iter, out.data(),
			     out.size());
		return toHex(out.data(), out.size());
	};

	CHECK_STR_EQ(run("password", "salt", 1, 32),
		     "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b");
	CHECK_STR_EQ(run("password", "salt", 2, 32),
		     "ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43");
	CHECK_STR_EQ(run("password", "salt", 4096, 32),
		     "c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a");
	CHECK_STR_EQ(run("passwordPASSWORDpassword", "saltSALTsaltSALTsaltSALTsaltSALTsalt", 4096, 40),
		     "348c89dbcbd32b2f32d814b8116e84cf2b17347ebc1800181c4e2a1fb8dd53e1c635518c7dac47e9");
	CHECK_STR_EQ(run("password", "NaCl1234", 1000, 32),
		     "986c856b216297f7c696d0a8e67e8c1ead27fab3e5e9557d15e3a87970aa2695");
}

TEST(chacha20_rfc8439_section2_4_2)
{
	auto key = unhex(
	    "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
	std::string plain =
	    "Ladies and Gentlemen of the class of '99: If I could offer you only one tip "
	    "for the future, sunscreen would be it.";
	std::string expected =
	    "6e2e359a2568f98041ba0728dd0d6981e97e7aec1d4360c20a27afccfd9fae0bf91b65c5524733ab8f"
	    "593dabcd62b3571639d624e65152ab8f530c359f0861d807ca0dbf500d6a6156a38e088a22b65e52bc"
	    "514d16ccf806818ce91ab77937365af90bbf74a35be6b40b8eedf2785e42874d";

	uint8_t nonce[12] = {0, 0, 0, 0, 0, 0, 0, 0x4a, 0, 0, 0, 0};
	std::vector<uint8_t> cipher(plain.size());
	chacha20Xor(key.data(), nonce, 1, reinterpret_cast<const uint8_t *>(plain.data()),
		    cipher.data(), cipher.size());
	CHECK_STR_EQ(toHex(cipher.data(), cipher.size()), expected);

	std::vector<uint8_t> back(plain.size());
	chacha20Xor(key.data(), nonce, 1, cipher.data(), back.data(), back.size());
	CHECK(std::string(back.begin(), back.end()) == plain);
}

TEST(random_and_hex)
{
	uint8_t a[32], b[32];
	CHECK(randomBytes(a, sizeof(a)));
	CHECK(randomBytes(b, sizeof(b)));
	CHECK(memcmp(a, b, 32) != 0);

	std::string hex = toHex(a, 32);
	CHECK_EQ(hex.size(), size_t(64));
	uint8_t c[32];
	CHECK(fromHex(hex, c, 32));
	CHECK(memcmp(a, c, 32) == 0);

	CHECK(!fromHex("zz", c, 1));
	CHECK(!fromHex("abc", c, 1));
	CHECK(randomHex(16).size() == 32);
}
