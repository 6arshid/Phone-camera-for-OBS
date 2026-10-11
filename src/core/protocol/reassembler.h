#pragma once

#include "protocol/media_packet.h"

#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace ocp {

struct ReassembledFrame {
	uint8_t stream = MEDIA_STREAM_VIDEO;
	uint32_t frameId = 0;
	bool keyframe = false;
	uint64_t tsNs = 0;
	std::vector<uint8_t> payload;
};

struct StreamStats {
	uint64_t framesDelivered = 0;
	uint64_t framesLost = 0;
	uint64_t bytesDelivered = 0;
	uint64_t packetsReceived = 0;
	uint64_t packetsDropped = 0;
	uint32_t lastDelivered = 0;
	bool haveLast = false;

	double lossPercent() const
	{
		uint64_t expected = framesDelivered + framesLost;
		if (expected == 0)
			return 0.0;
		return 100.0 * double(framesLost) / double(expected);
	}
};

class Reassembler {
public:
	static constexpr size_t kMaxPending = 8;
	static constexpr size_t kMaxReady = 4;

	// Appends zero or more in-order frames to *out.
	// Returns false only when *error is set (malformed fragment).
	bool push(const MediaHeader &header, std::vector<uint8_t> fragment,
		  std::vector<ReassembledFrame> *out, std::string *error);

	const StreamStats &stats(uint8_t stream) const { return stats_[stream & 1]; }
	size_t pendingCount() const { return pending.size(); }
	size_t readyCount() const { return ready.size(); }
	void reset();

private:
	struct PendingFrame {
		uint16_t fragCount = 1;
		uint16_t received = 0;
		std::vector<std::vector<uint8_t>> parts;
		std::vector<bool> have;
		uint8_t flags = 0;
		uint64_t tsNs = 0;
	};

	using Key = std::pair<uint8_t, uint32_t>;

	void evictOldest();
	void dropStale();
	void deliver(std::vector<ReassembledFrame> *out);
	void drain(std::vector<ReassembledFrame> *out);

	std::map<Key, PendingFrame> pending;
	std::map<Key, ReassembledFrame> ready;
	std::deque<Key> pendingOrder;
	StreamStats stats_[2];
};

}
