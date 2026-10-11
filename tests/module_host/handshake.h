#pragma once
#include <winsock2.h>
#include <ws2tcpip.h>
#include "crypto/hex.h"
#include "crypto/random.h"
#include "pairing/pairing.h"
#include "protocol/messages.h"
#include <stdexcept>
#include <fstream>
#include <iterator>
#include <obs.h>
#include "codec/h264.h"
#include "protocol/fragmenter.h"

// Exercise the loaded DLL over TCP, including the plaintext/encrypted boundary.
inline ocp::MasterKey checkHandshake(const std::string &code,
                                    const ocp::MasterKey *stored = nullptr,
                                    obs_source_t *videoTarget = nullptr)
{
    using namespace ocp;
    auto dumpMessage = [](const Message &message) { return toJson(message).dump(); };
    struct Connection {
        SOCKET fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        ~Connection() { if (fd != INVALID_SOCKET) closesocket(fd); }
    } connection;
    auto require = [](bool ok, const char *message) {
        if (!ok) throw std::runtime_error(message);
    };
    DWORD timeout = 5000;
    setsockopt(connection.fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(47100);
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    require(connect(connection.fd, (sockaddr *)&address, sizeof(address)) == 0, "TCP connect");
    ControlFrameEncoder encoder;
    ControlFrameReader reader;
    auto sendPayload = [&](const std::string &payload) {
        auto frame = encoder.encode(payload);
        size_t offset = 0;
        while (offset < frame.size()) {
            int n = send(connection.fd, (const char *)frame.data() + offset, (int)(frame.size() - offset), 0);
            require(n > 0, "send frame");
            offset += n;
        }
    };
    auto readPayload = [&](bool encrypted) {
        std::vector<uint8_t> frame(27);
        auto readExact = [&](uint8_t *dest, size_t count) {
            while (count) {
                int n = recv(connection.fd, (char *)dest, (int)count, 0);
                require(n > 0, "receive frame");
                dest += n;
                count -= n;
            }
        };
        readExact(frame.data(), 27);
        require((frame[8] == 1) == encrypted, "unexpected encryption state");
        size_t size = (size_t(frame[4]) << 24) | (size_t(frame[5]) << 16) |
                      (size_t(frame[6]) << 8) | frame[7];
        require(size > 0 && size <= 65536, "payload size");
        frame.resize(27 + size);
        readExact(frame.data() + 27, size);
        std::vector<std::string> payloads;
        std::string error;
        require(reader.feed(frame.data(), frame.size(), &payloads, &error) && payloads.size() == 1, "decode frame");
        return payloads[0];
    };
    uint8_t salt[16], clientNonce[16], serverNonce[16];
    require(randomBytes(salt, 16) && randomBytes(clientNonce, 16), "random bytes");
    DeviceInfo device;
    device.id = "fead0000000000000000000000000001";
    device.name = "handshake-regression";
    device.os = "android";
    device.app = "test";
    std::string hello = dumpMessage(makeHello(stored ? "hkdf-sha256" : "pbkdf2-sha256",
        stored ? 0 : 100000, toHex(salt, 16), toHex(clientNonce, 16), device));
    sendPayload(hello);
    std::string challengeRaw = readPayload(false);
    Message challenge = parseMessage(challengeRaw);
    require(challenge.type == MsgType::Challenge, "challenge type");
    require(fromHex(challenge.nonce, serverNonce, 16), "challenge nonce");
    MasterKey master = stored ? *stored : masterFromCode(code, salt, 100000);
    auto keys = deriveSessionKeys(master, clientNonce, serverNonce);
    auto transcript = transcriptHash(hello, challengeRaw);
    sendPayload(dumpMessage(makeAuth(toHex(clientNonce, 16),
        computePhoneProof(keys.kAuth, clientNonce, serverNonce, transcript))));
    Message ok = parseMessage(readPayload(false));
    require(ok.type == MsgType::AuthOk && ok.paired, "auth_ok type and pairing");
    require(verifyObsProof(keys.kAuth, clientNonce, serverNonce, transcript, ok.proof), "server proof");
    encoder.setKeys(keys.control());
    reader.setKeys(keys.control());
    sendPayload(dumpMessage(makeReady(json::Value::object())));
    Message config = parseMessage(readPayload(true));
    require(config.type == MsgType::Config, "encrypted config");
    require(config.config.stream.active, "visible source enables camera streaming");
    require(config.config.video.width == 1280 && config.config.video.height == 720,
            "first config uses attached source settings");
    if (videoTarget) {
        std::ifstream fixture(std::string(OCP_TEST_DATA_DIR) + "/test_320x240.h264", std::ios::binary);
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(fixture)), {});
        require(!data.empty(), "video fixture available");
        Connection udp;
        closesocket(udp.fd);
        udp.fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        address.sin_port = htons((u_short)ok.media_port);
        uint8_t sessionBytes[16];
        require(fromHex(ok.session, sessionBytes, 16), "media session id");
        uint64_t tag = 0;
        for (int i = 0; i < 8; ++i) tag = (tag << 8) | sessionBytes[i];
        uint32_t frameId = 0;
        std::vector<uint8_t> unit;
        bool haveSlice = false;
        auto sendUnit = [&] {
            auto packets = fragmentAndSeal(keys.media(), tag, MEDIA_STREAM_VIDEO,
                ++frameId, MEDIA_FLAG_KEYFRAME, frameId * 33333333ull, unit.data(), unit.size());
            for (const auto &packet : packets)
                require(sendto(udp.fd, (const char *)packet.data(), (int)packet.size(), 0,
                    (sockaddr *)&address, sizeof(address)) == (int)packet.size(), "UDP video send");
            unit.clear();
            Sleep(33);
        };
        for (const auto &nal : splitAnnexB(data.data(), data.size())) {
            bool slice = nal.type == 1 || nal.type == 5;
            if (slice && haveSlice) { sendUnit(); haveSlice = false; }
            unit.insert(unit.end(), {0, 0, 0, 1});
            unit.insert(unit.end(), nal.data, nal.data + nal.size);
            haveSlice = haveSlice || slice;
        }
        if (!unit.empty()) sendUnit();
        Sleep(300);
        require(obs_source_get_width(videoTarget) == 320 && obs_source_get_height(videoTarget) == 240,
                "encrypted UDP H264 reaches OBS video source");
    }
    sendPayload(dumpMessage(makeBye("test_complete")));
    return master;
}
