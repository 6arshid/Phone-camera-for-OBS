#include "protocol/reassembler.h"

#include <algorithm>

namespace ocp {

void Reassembler::reset()
{
	pending.clear();
	ready.clear();
	pendingOrder.clear();
	stats_[0] = StreamStats{};
	stats_[1] = StreamStats{};
}

void Reassembler::evictOldest()
{
	while (!pendingOrder.empty()) {
		Key key = pendingOrder.front();
		pendingOrder.pop_front();
		auto it = pending.find(key);
		if (it != pending.end()) {
			pending.erase(it);
			return;
		}
	}
}

void Reassembler::dropStale()
{
	while (!ready.empty()) {
		const Key &key = ready.begin()->first;
		StreamStats &st = stats_[key.first & 1];
		if (st.haveLast && key.second <= st.lastDelivered) {
			st.packetsDropped++;
			ready.erase(ready.begin());
			continue;
		}
		break;
	}
}

void Reassembler::deliver(std::vector<ReassembledFrame> *out)
{
	auto it = ready.begin();
	const Key key = it->first;
	StreamStats &st = stats_[key.first & 1];

	out->push_back(std::move(it->second));
	ready.erase(it);

	st.lastDelivered = key.second;
	st.haveLast = true;
	st.framesDelivered++;
	st.bytesDelivered += out->back().payload.size();
}

void Reassembler::drain(std::vector<ReassembledFrame> *out)
{
	if (!out)
		return;

	dropStale();

	while (!ready.empty()) {
		const Key key = ready.begin()->first;
		StreamStats &st = stats_[key.first & 1];
		const uint32_t id = key.second;

		if (!st.haveLast) {
			deliver(out);
			dropStale();
			continue;
		}

		const uint32_t next = st.lastDelivered + 1;
		if (id == next) {
			deliver(out);
			dropStale();
			continue;
		}

		bool pendingHasNext = pending.find(Key(key.first, next)) != pending.end();
		if (pendingHasNext && ready.size() < kMaxReady)
			break;

		st.framesLost += uint64_t(id - next);
		deliver(out);
		dropStale();
	}
}

bool Reassembler::push(const MediaHeader &header, std::vector<uint8_t> fragment,
		       std::vector<ReassembledFrame> *out, std::string *error)
{
	if (error)
		error->clear();

	if (header.stream > MEDIA_STREAM_AUDIO) {
		if (error)
			*error = "invalid stream";
		return false;
	}

	StreamStats &st = stats_[header.stream & 1];
	st.packetsReceived++;

	if (header.frame_id == 0) {
		st.packetsDropped++;
		return true;
	}

	if (st.haveLast && header.frame_id <= st.lastDelivered) {
		st.packetsDropped++;
		return true;
	}

	Key key(header.stream, header.frame_id);
	auto it = pending.find(key);
	if (it == pending.end()) {
		if (pending.size() >= kMaxPending)
			evictOldest();

		PendingFrame frame;
		frame.fragCount = header.frag_count;
		frame.have.assign(header.frag_count, false);
		frame.parts.resize(header.frag_count);
		frame.flags = header.flags;
		frame.tsNs = header.ts_ns;
		it = pending.emplace(key, std::move(frame)).first;
		pendingOrder.push_back(key);
	}

	PendingFrame &frame = it->second;
	if (frame.fragCount != header.frag_count || header.frag_idx >= frame.fragCount ||
	    frame.have[header.frag_idx]) {
		st.packetsDropped++;
		return true;
	}

	frame.parts[header.frag_idx] = std::move(fragment);
	frame.have[header.frag_idx] = true;
	frame.received++;

	if (frame.received == frame.fragCount) {
		ReassembledFrame done;
		done.stream = header.stream;
		done.frameId = header.frame_id;
		done.keyframe = (header.flags & MEDIA_FLAG_KEYFRAME) != 0;
		done.tsNs = frame.tsNs;
		size_t total = 0;
		for (const auto &part : frame.parts)
			total += part.size();
		done.payload.reserve(total);
		for (auto &part : frame.parts)
			done.payload.insert(done.payload.end(), part.begin(), part.end());

		pending.erase(it);
		for (auto orderIt = pendingOrder.begin(); orderIt != pendingOrder.end(); ++orderIt) {
			if (*orderIt == key) {
				pendingOrder.erase(orderIt);
				break;
			}
		}
		ready.emplace(key, std::move(done));
	}

	drain(out);
	return true;
}

}
