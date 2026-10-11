#include "net_util.h"

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>

#include <chrono>
#include <cstdio>

#pragma comment(lib, "iphlpapi.lib")

namespace ocp {
namespace plugin {

uint64_t monotonicNs()
{
	using namespace std::chrono;
	return (uint64_t)duration_cast<nanoseconds>(steady_clock::now().time_since_epoch())
		.count();
}

uint64_t wallClockNs()
{
	using namespace std::chrono;
	return (uint64_t)duration_cast<nanoseconds>(
		       system_clock::now().time_since_epoch())
		.count();
}

std::vector<std::string> localIPv4Addresses()
{
	std::vector<std::string> out;

	ULONG size = 15 * 1024;
	IP_ADAPTER_ADDRESSES *addrs = (IP_ADAPTER_ADDRESSES *)malloc(size);
	if (!addrs)
		return out;

	ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
	DWORD rc = GetAdaptersAddresses(AF_INET, flags, nullptr, addrs, &size);
	if (rc == ERROR_BUFFER_OVERFLOW) {
		free(addrs);
		addrs = (IP_ADAPTER_ADDRESSES *)malloc(size);
		if (!addrs)
			return out;
		rc = GetAdaptersAddresses(AF_INET, flags, nullptr, addrs, &size);
	}
	if (rc != NO_ERROR) {
		free(addrs);
		return out;
	}

	for (IP_ADAPTER_ADDRESSES *a = addrs; a; a = a->Next) {
		if (a->OperStatus != IfOperStatusUp)
			continue;
		for (IP_ADAPTER_UNICAST_ADDRESS *u = a->FirstUnicastAddress; u; u = u->Next) {
			sockaddr_in *sin = (sockaddr_in *)u->Address.lpSockaddr;
			if (!sin || sin->sin_family != AF_INET)
				continue;
			char buf[64] = {};
			InetNtopA(AF_INET, &sin->sin_addr, buf, sizeof(buf));
			std::string ip(buf);
			if (ip.rfind("169.254.", 0) == 0)
				continue;
			out.push_back(ip);
		}
	}
	free(addrs);
	return out;
}

std::string firstLocalIPv4()
{
	auto ips = localIPv4Addresses();
	return ips.empty() ? std::string("127.0.0.1") : ips.front();
}

std::string winSocketError(int code)
{
	char buf[128] = {};
	FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
		       (DWORD)code, 0, buf, sizeof(buf), nullptr);
	std::string text = buf;
	while (!text.empty() && (text.back() == '\r' || text.back() == '\n' || text.back() == ' '))
		text.pop_back();
	if (text.empty()) {
		char fallback[32];
		snprintf(fallback, sizeof(fallback), "error %d", code);
		text = fallback;
	}
	return text;
}

bool setNonBlocking(int fd, bool nonBlocking)
{
	u_long mode = nonBlocking ? 1 : 0;
	return ioctlsocket((SOCKET)fd, FIONBIO, &mode) == 0;
}

}
}
