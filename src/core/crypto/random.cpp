#include "crypto/random.h"

#include "crypto/hex.h"

#include <cstring>

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#else
#include <fcntl.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <stdlib.h>
#else
#include <sys/random.h>
#endif
#endif

namespace ocp {

bool randomBytes(uint8_t *out, size_t len)
{
	if (!out || !len)
		return false;

#ifdef _WIN32
	BCRYPT_ALG_HANDLE alg = nullptr;
	if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_RNG_ALGORITHM, nullptr, 0) != 0)
		return false;
	NTSTATUS status = BCryptGenRandom(alg, out, ULONG(len), 0);
	BCryptCloseAlgorithmProvider(alg, 0);
	return status == 0;
#elif defined(__APPLE__)
	arc4random_buf(out, len);
	return true;
#else
	size_t offset = 0;
	while (offset < len) {
		ssize_t n = getrandom(out + offset, len - offset, 0);
		if (n < 0)
			return false;
		offset += size_t(n);
	}
	return true;
#endif
}

std::string randomHex(size_t bytes)
{
	uint8_t buf[64];
	if (bytes > sizeof(buf))
		return std::string();
	if (!randomBytes(buf, bytes))
		return std::string();
	return toHex(buf, bytes);
}

uint32_t randomUint32()
{
	uint8_t buf[4];
	if (!randomBytes(buf, sizeof(buf)))
		return 0;
	return (uint32_t(buf[0]) << 24) | (uint32_t(buf[1]) << 16) | (uint32_t(buf[2]) << 8) |
	       uint32_t(buf[3]);
}

}
