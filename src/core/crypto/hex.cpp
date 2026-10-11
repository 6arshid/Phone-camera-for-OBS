#include "crypto/hex.h"

namespace ocp {

namespace {
constexpr char kDigits[] = "0123456789abcdef";

inline int nibble(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}
}

std::string toHex(const uint8_t *data, size_t len)
{
	std::string out;
	out.reserve(len * 2);
	for (size_t i = 0; i < len; i++) {
		out.push_back(kDigits[data[i] >> 4]);
		out.push_back(kDigits[data[i] & 0x0f]);
	}
	return out;
}

bool fromHex(std::string_view text, uint8_t *out, size_t outLen)
{
	if (text.size() != outLen * 2)
		return false;
	for (size_t i = 0; i < outLen; i++) {
		int hi = nibble(text[i * 2]);
		int lo = nibble(text[i * 2 + 1]);
		if (hi < 0 || lo < 0)
			return false;
		out[i] = uint8_t((hi << 4) | lo);
	}
	return true;
}

}
