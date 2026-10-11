#include "protocol/media_packet.h"

#include "protocol/constants.h"

#include "crypto/chacha20.h"
#include "crypto/hmac.h"

#include <cstring>

namespace ocp {

namespace {

void writeU16Be(uint8_t *p, uint16_t v)
{
	p[0] = uint8_t(v >> 8);
	p[1] = uint8_t(v);
}

void writeU32Be(uint8_t *p, uint32_t v)
{
	p[0] = uint8_t(v >> 24);
	p[1] = uint8_t(v >> 16);
	p[2] = uint8_t(v >> 8);
	p[3] = uint8_t(v);
}

void writeU64Be(uint8_t *p, uint64_t v)
{
	for (int i = 0; i < 8; i++)
		p[i] = uint8_t(v >> (56 - i * 8));
}

uint16_t readU16Be(const uint8_t *p) { return uint16_t((uint32_t(p[0]) << 8) | p[1]); }

uint32_t readU32Be(const uint8_t *p)
{
	return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) |
	       uint32_t(p[3]);
}

uint64_t readU64Be(const uint8_t *p)
{
	uint64_t v = 0;
	for (int i = 0; i < 8; i++)
		v = (v << 8) | p[i];
	return v;
}

void buildNonce(const uint8_t header[kMediaHeaderSize], uint8_t nonce[12])
{
	nonce[0] = header[8];
	nonce[1] = header[9];
	nonce[2] = header[10];
	nonce[3] = header[11];
	nonce[4] = header[5];
	nonce[5] = header[16];
	nonce[6] = header[17];
	nonce[7] = header[18];
	nonce[8] = header[19];
	nonce[9] = header[20];
	nonce[10] = header[21];
	nonce[11] = 0;
}

void packHeader(const MediaHeader &h, uint8_t out[kMediaHeaderSize])
{
	memcpy(out, kMediaMagic, 4);
	out[4] = 1;
	out[5] = h.stream;
	out[6] = h.flags;
	out[7] = 0;
	writeU64Be(out + 8, h.session_tag);
	writeU32Be(out + 16, h.frame_id);
	writeU16Be(out + 20, h.frag_idx);
	writeU16Be(out + 22, h.frag_count);
	writeU64Be(out + 24, h.ts_ns);
}

void computeTag(const MediaKeys &keys, const uint8_t header[kMediaHeaderSize], const uint8_t *cipher,
		size_t cipherLen, uint8_t tag[kTagSize])
{
	HmacSha256 h;
	h.init(keys.mac.data(), keys.mac.size());
	h.update(header, kMediaHeaderSize);
	if (cipherLen)
		h.update(cipher, cipherLen);
	uint8_t full[32];
	h.final(full);
	memcpy(tag, full, kTagSize);
}

}

std::vector<uint8_t> sealMediaPacket(const MediaKeys &keys, const MediaHeader &header,
				     const uint8_t *plain, size_t plainLen)
{
	std::vector<uint8_t> packet(kMediaHeaderSize + plainLen + kTagSize);
	uint8_t *headerBytes = packet.data();
	packHeader(header, headerBytes);

	uint8_t nonce[12];
	buildNonce(headerBytes, nonce);
	if (plainLen)
		chacha20Xor(keys.enc.data(), nonce, 0, plain, packet.data() + kMediaHeaderSize,
			    plainLen);

	uint8_t tag[kTagSize];
	computeTag(keys, headerBytes, packet.data() + kMediaHeaderSize, plainLen, tag);
	memcpy(packet.data() + kMediaHeaderSize + plainLen, tag, kTagSize);
	return packet;
}

bool openMediaPacket(const MediaKeys &keys, const uint8_t *data, size_t len, MediaHeader *header,
		     std::vector<uint8_t> *plain, std::string *error)
{
	if (len < kMediaHeaderSize + kTagSize) {
		if (error)
			*error = "media packet too short";
		return false;
	}
	if (memcmp(data, kMediaMagic, 4) != 0) {
		if (error)
			*error = "bad media magic";
		return false;
	}
	if (data[4] != 1) {
		if (error)
			*error = "unsupported media version";
		return false;
	}
	if (len > kMaxDatagram) {
		if (error)
			*error = "media packet too large";
		return false;
	}

	size_t cipherLen = len - kMediaHeaderSize - kTagSize;
	const uint8_t *cipher = data + kMediaHeaderSize;
	const uint8_t *gotTag = data + kMediaHeaderSize + cipherLen;

	uint8_t expected[kTagSize];
	computeTag(keys, data, cipher, cipherLen, expected);
	uint8_t diff = 0;
	for (size_t i = 0; i < kTagSize; i++)
		diff |= expected[i] ^ gotTag[i];
	if (diff != 0) {
		if (error)
			*error = "media packet authentication failed";
		return false;
	}

	if (header) {
		header->stream = data[5];
		header->flags = data[6];
		header->session_tag = readU64Be(data + 8);
		header->frame_id = readU32Be(data + 16);
		header->frag_idx = readU16Be(data + 20);
		header->frag_count = readU16Be(data + 22);
		header->ts_ns = readU64Be(data + 24);
		if (header->frag_count == 0 || header->frag_idx >= header->frag_count) {
			if (error)
				*error = "invalid fragment indices";
			return false;
		}
		if (header->stream > MEDIA_STREAM_AUDIO) {
			if (error)
				*error = "invalid stream id";
			return false;
		}
	}

	if (plain) {
		plain->resize(cipherLen);
		if (cipherLen) {
			uint8_t nonce[12];
			buildNonce(data, nonce);
			chacha20Xor(keys.enc.data(), nonce, 0, cipher, plain->data(), cipherLen);
		}
	}
	return true;
}

}
