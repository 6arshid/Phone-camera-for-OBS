#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ocp {
namespace plugin {

struct DecodedFrame {
	int width = 0;
	int height = 0;
	int strideY = 0;
	int strideUV = 0;
	uint64_t index = 0;
	std::vector<uint8_t> nv12;
};

class VideoDecoder {
public:
	virtual ~VideoDecoder() = default;

	// (Re)creates the decoder for the given output geometry. Resets state.
	virtual bool configure(int width, int height, int fps, int bitrateKbps,
			   std::string *err) = 0;

	// Feeds one Annex-B access unit. Decoded frames become available via take().
	virtual bool feed(const uint8_t *data, size_t size, std::string *err) = 0;

	// Non-blocking. Returns true when a frame was popped.
	virtual bool take(DecodedFrame *out) = 0;

	// Forces the decoder to emit all buffered frames (MS software decoders
	// buffer up to ~1s before outputting). After flush() the decoder must be
	// re-configured (configure()) before it can accept new input.
	virtual void flush() = 0;

	virtual int pending() const = 0;

	// Drops buffered state (e.g. after severe packet loss).
	virtual void reset() = 0;
};

std::unique_ptr<VideoDecoder> createVideoDecoder();
bool decoderSupported();

}
}
