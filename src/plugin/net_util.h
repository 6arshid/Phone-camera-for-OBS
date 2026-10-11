#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ocp {
namespace plugin {

uint64_t monotonicNs();
uint64_t wallClockNs();

std::vector<std::string> localIPv4Addresses();
std::string firstLocalIPv4();
std::string winSocketError(int code);

bool setNonBlocking(int fd, bool nonBlocking);

}
}
