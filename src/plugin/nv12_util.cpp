#include "nv12_util.h"

#include <media-io/video-io.h>

#include <algorithm>
#include <cstring>

namespace ocp {
namespace plugin {

namespace {

CropRect clampEvenCrop(int srcW, int srcH, const CropRect &crop)
{
	CropRect out = crop;
	if (out.w <= 0 || out.h <= 0) {
		out.x = 0;
		out.y = 0;
		out.w = srcW;
		out.h = srcH;
	}
	out.x &= ~1;
	out.y &= ~1;
	if (out.x >= srcW || out.y >= srcH)
		return {0, 0, srcW, srcH};
	if (out.x + out.w > srcW)
		out.w = srcW - out.x;
	if (out.y + out.h > srcH)
		out.h = srcH - out.y;
	out.w &= ~1;
	out.h &= ~1;
	if (out.w <= 0 || out.h <= 0)
		return {0, 0, srcW & ~1, srcH & ~1};
	return out;
}

}

void fillNv12Frame(obs_source_frame *out, const DecodedFrame &src, const CropRect &crop)
{
	memset(out, 0, sizeof(*out));
	if (src.nv12.empty() || src.width <= 0 || src.height <= 0)
		return;

	CropRect c = clampEvenCrop(src.width, src.height, crop);
	size_t yPlane = (size_t)src.strideY * src.height;
	size_t uvPlane = (size_t)src.strideUV * (src.height / 2);
	if (src.nv12.size() < yPlane + uvPlane)
		return;

	out->data[0] = const_cast<uint8_t *>(src.nv12.data()) + (size_t)c.y * src.strideY +
			c.x;
	out->data[1] = const_cast<uint8_t *>(src.nv12.data()) + yPlane +
			(size_t)(c.y / 2) * src.strideUV + c.x;
	out->linesize[0] = (uint32_t)src.strideY;
	out->linesize[1] = (uint32_t)src.strideUV;
	out->width = (uint32_t)c.w;
	out->height = (uint32_t)c.h;
	out->format = VIDEO_FORMAT_NV12;
	out->full_range = false;
	out->flip = false;
	out->trc = VIDEO_TRC_SRGB;
	video_format_get_parameters_for_format(VIDEO_CS_709, VIDEO_RANGE_PARTIAL,
					     VIDEO_FORMAT_NV12, out->color_matrix,
					     out->color_range_min, out->color_range_max);
}

void fillBgraFrame(obs_source_frame *out, const uint8_t *bgra, int width, int height,
		   int strideBytes, uint64_t timestampNs)
{
	memset(out, 0, sizeof(*out));
	out->data[0] = const_cast<uint8_t *>(bgra);
	out->linesize[0] = (uint32_t)strideBytes;
	out->width = (uint32_t)width;
	out->height = (uint32_t)height;
	out->timestamp = timestampNs;
	out->format = VIDEO_FORMAT_BGRA;
	out->full_range = true;
	out->flip = false;
	out->trc = VIDEO_TRC_SRGB;
	video_format_get_parameters_for_format(VIDEO_CS_709, VIDEO_RANGE_FULL,
					     VIDEO_FORMAT_BGRA, out->color_matrix,
					     out->color_range_min, out->color_range_max);
}

}
}
