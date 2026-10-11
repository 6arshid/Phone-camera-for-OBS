#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ocp {

struct ControlKeys {
	std::array<uint8_t, 32> enc{};
	std::array<uint8_t, 32> mac{};
	bool active = false;
};

class ControlFrameEncoder {
public:
	void setKeys(const ControlKeys &keys);
	bool encryptionActive() const { return keys.active; }

	std::vector<uint8_t> encode(std::string_view payload);

private:
	ControlKeys keys;
	uint64_t counter = 0;
};

class ControlFrameReader {
public:
	void setKeys(const ControlKeys &keys);
	void reset();

	// Returns false and fills *error on a protocol violation.
	// Complete, authenticated payloads are appended to *out.
	bool feed(const uint8_t *data, size_t len, std::vector<std::string> *out, std::string *error);

private:
	bool extract(std::vector<std::string> *out, std::string *error);

	std::vector<uint8_t> buffer;
	ControlKeys keys;
	uint64_t counter = 0;
};

}
