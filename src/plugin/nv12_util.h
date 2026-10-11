#pragma once

#include "decoder.h"
#include "settings/config.h"

#include <obs.h>

namespace ocp {
namespace plugin {

// Fills an obs_source_frame describing the center-cropped region of a decoded
// NV12 frame. The data pointers reference src.nv12; src must stay alive while
// the frame is consumed (obs_source_output_video copies internally).
void fillNv12Frame(obs_source_frame *out, const DecodedFrame &src, const CropRect &crop);

void fillBgraFrame(obs_source_frame *out, const uint8_t *bgra, int width, int height,
		   int strideBytes, uint64_t timestampNs);

}
}
