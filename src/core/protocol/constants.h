#pragma once

#include <cstddef>
#include <cstdint>

namespace ocp {

constexpr const char *kProtocolVersion = "ocp/1";
constexpr int kProtocolVersionNum = 1;

constexpr uint8_t kControlMagic[4] = {'O', 'C', 'P', '1'};
constexpr uint8_t kMediaMagic[4] = {'O', 'C', 'P', 'M'};

constexpr size_t kControlHeaderSize = 27;
constexpr size_t kControlMaxPayload = 65536;
constexpr size_t kTagSize = 16;

constexpr size_t kMediaHeaderSize = 32;
constexpr size_t kMaxDatagram = 1200;
constexpr size_t kMediaCipherMax = kMaxDatagram - kMediaHeaderSize - kTagSize;

constexpr uint16_t kDefaultControlPort = 47100;
constexpr uint16_t kDefaultDiscoveryPort = 47109;
constexpr uint16_t kDefaultMediaPort = 47101;

constexpr const char *kMulticastGroup = "239.255.74.79";

constexpr size_t kSecretSize = 32;
constexpr size_t kNonceSize = 16;
constexpr size_t kSessionIdSize = 16;
constexpr uint32_t kMinPbkdf2Iterations = 100000;

constexpr const char *kPairingUrlScheme = "ocp1";
constexpr const char *kPairingUrlHost = "pair";

}
