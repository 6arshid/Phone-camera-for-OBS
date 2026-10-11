#include "protocol/control_frame.h"

#include "protocol/constants.h"

#include "crypto/chacha20.h"
#include "crypto/hmac.h"

#include <cstring>

namespace ocp {

namespace {

void writeU32Be(uint8_t *p, uint32_t v)
{
	p[0] = uint8_t(v >> 24);
	p[1] = uint8_t(v >> 16);
	p[2] = uint8_t(v >> 8);
	p[3] = uint8_t(v);
}

uint32_t readU32Be(const uint8_t *p)
{
	return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) |
	       uint32_t(p[3]);
}

void counterNonce(uint64_t counter, uint8_t nonce[12])
{
	memset(nonce, 0, 12);
	for (int i = 0; i < 8; i++)
		nonce[i] = uint8_t(counter >> (i * 8));
}

void computeTag(const ControlKeys &keys, const uint8_t header[kControlHeaderSize],
		const uint8_t *cipher, size_t cipherLen, uint8_t tag[kTagSize])
{
	uint8_t headerCopy[kControlHeaderSize];
	memcpy(headerCopy, header, kControlHeaderSize);
	memset(headerCopy + 11, 0, kTagSize);

	HmacSha256 h;
	h.init(keys.mac.data(), keys.mac.size());
	h.update(headerCopy, kControlHeaderSize);
	if (cipherLen)
		h.update(cipher, cipherLen);
	uint8_t full[32];
	h.final(full);
	memcpy(tag, full, kTagSize);
}

}

void ControlFrameEncoder::setKeys(const ControlKeys &keysIn)
{
	keys = keysIn;
	counter = 0;
}

std::vector<uint8_t> ControlFrameEncoder::encode(std::string_view payload)
{
	if (payload.size() > kControlMaxPayload)
		payload = payload.substr(0, kControlMaxPayload);

	std::vector<uint8_t> frame(kControlHeaderSize + payload.size());
	uint8_t *header = frame.data();

	memcpy(header, kControlMagic, 4);
	writeU32Be(header + 4, uint32_t(payload.size()));
	header[8] = 0;
	header[9] = 0;
	header[10] = 0;
	memset(header + 11, 0, kTagSize);

	if (keys.active) {
		header[8] = 1;
		uint8_t nonce[12];
		counterNonce(counter, nonce);
		chacha20Xor(keys.enc.data(), nonce, 0,
			    reinterpret_cast<const uint8_t *>(payload.data()),
			    frame.data() + kControlHeaderSize, payload.size());
		counter++;

		uint8_t tag[kTagSize];
		computeTag(keys, header, frame.data() + kControlHeaderSize, payload.size(), tag);
		memcpy(header + 11, tag, kTagSize);
	} else if (!payload.empty()) {
		memcpy(frame.data() + kControlHeaderSize, payload.data(), payload.size());
	}

	return frame;
}

void ControlFrameReader::setKeys(const ControlKeys &keysIn)
{
	keys = keysIn;
	counter = 0;
}

void ControlFrameReader::reset()
{
	buffer.clear();
	counter = 0;
}

bool ControlFrameReader::feed(const uint8_t *data, size_t len, std::vector<std::string> *out,
			      std::string *error)
{
	if (error)
		error->clear();
	if (len > 0) {
		if (buffer.size() + len > 4 * kControlMaxPayload + kControlHeaderSize) {
			if (error)
				*error = "control stream buffer overflow";
			return false;
		}
		buffer.insert(buffer.end(), data, data + len);
	}
	return extract(out, error);
}

bool ControlFrameReader::extract(std::vector<std::string> *out, std::string *error)
{
	size_t offset = 0;
	while (buffer.size() - offset >= kControlHeaderSize) {
		const uint8_t *header = buffer.data() + offset;
		if (memcmp(header, kControlMagic, 4) != 0) {
			if (error)
				*error = "bad control frame magic";
			return false;
		}
		uint32_t payloadLen = readU32Be(header + 4);
		if (payloadLen > kControlMaxPayload) {
			if (error)
				*error = "control frame too large";
			return false;
		}
		size_t total = kControlHeaderSize + payloadLen;
		if (buffer.size() - offset < total)
			break;

		const uint8_t *cipher = header + kControlHeaderSize;
		bool encrypted = (header[8] & 1) != 0;
		std::string payload;

		if (encrypted) {
			if (!keys.active) {
				if (error)
					*error = "encrypted frame before keys are active";
				return false;
			}
			uint8_t expected[kTagSize];
			computeTag(keys, header, cipher, payloadLen, expected);
			const uint8_t *got = header + 11;
			uint8_t diff = 0;
			for (size_t i = 0; i < kTagSize; i++)
				diff |= expected[i] ^ got[i];
			if (diff != 0) {
				if (error)
					*error = "control frame authentication failed";
				return false;
			}

			payload.resize(payloadLen);
			uint8_t nonce[12];
			counterNonce(counter, nonce);
			chacha20Xor(keys.enc.data(), nonce, 0, cipher,
				    reinterpret_cast<uint8_t *>(&payload[0]), payloadLen);
			counter++;
		} else {
			if (payloadLen > 0)
				payload.assign(reinterpret_cast<const char *>(cipher), payloadLen);
		}

		if (out)
			out->push_back(std::move(payload));
		offset += total;
	}

	if (offset > 0)
		buffer.erase(buffer.begin(), buffer.begin() + long(offset));
	return true;
}

}
