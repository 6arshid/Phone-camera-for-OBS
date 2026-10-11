#include "settings/config.h"

#include <algorithm>
#include <cmath>

namespace ocp {

namespace {

int clampInt(int v, int lo, int hi) { return std::max(lo, std::min(hi, v)); }

}

int autoBitrateKbps(int width, int height, int fps)
{
	if (width <= 0 || height <= 0)
		return 0;
	if (fps <= 0)
		fps = 30;
	double kbps = double(width) * double(height) * double(fps) * 0.07 / 1000.0;
	int v = int(std::lround(kbps / 100.0) * 100);
	return clampInt(v, 800, 50000);
}

VideoProfile resolveVideoProfile(const std::string &resolutionPreset, int customWidth,
				 int customHeight, const std::string &fpsPreset, int bitrateKbps,
				 int gopS, const std::string &orientation)
{
	VideoProfile profile;
	profile.orientation = orientation.empty() ? "auto" : orientation;
	profile.gopS = clampInt(gopS, 1, 10);

	if (resolutionPreset == "2160p") {
		profile.width = 3840;
		profile.height = 2160;
	} else if (resolutionPreset == "1440p") {
		profile.width = 2560;
		profile.height = 1440;
	} else if (resolutionPreset == "720p") {
		profile.width = 1280;
		profile.height = 720;
	} else if (resolutionPreset == "480p") {
		profile.width = 854;
		profile.height = 480;
	} else if (resolutionPreset == "custom") {
		profile.width = clampInt(customWidth, 160, 7680);
		profile.height = clampInt(customHeight, 120, 4320);
	} else if (resolutionPreset == "auto") {
		profile.width = 0;
		profile.height = 0;
	} else {
		profile.width = 1920;
		profile.height = 1080;
	}

	if (fpsPreset == "60")
		profile.fps = 60;
	else if (fpsPreset == "50")
		profile.fps = 50;
	else if (fpsPreset == "30")
		profile.fps = 30;
	else if (fpsPreset == "25")
		profile.fps = 25;
	else if (fpsPreset == "24")
		profile.fps = 24;
	else if (fpsPreset == "auto")
		profile.fps = 0;
	else
		profile.fps = 30;

	if (bitrateKbps > 0) {
		profile.bitrateKbps = clampInt(bitrateKbps, 200, 100000);
	} else if (profile.width > 0 && profile.height > 0 && profile.fps > 0) {
		profile.bitrateKbps = autoBitrateKbps(profile.width, profile.height, profile.fps);
	} else {
		profile.bitrateKbps = 0;
	}

	return profile;
}

CropRect centerCrop(int srcWidth, int srcHeight, const std::string &aspect)
{
	CropRect rect;
	rect.w = srcWidth;
	rect.h = srcHeight;

	if (srcWidth <= 0 || srcHeight <= 0)
		return rect;

	double target = 0.0;
	if (aspect == "16:9")
		target = 16.0 / 9.0;
	else if (aspect == "9:16")
		target = 9.0 / 16.0;
	else if (aspect == "1:1")
		target = 1.0;
	else
		return rect;

	double src = double(srcWidth) / double(srcHeight);
	if (std::fabs(src - target) < 0.001)
		return rect;

	if (src > target) {
		int newW = int(std::lround(double(srcHeight) * target));
		newW = clampInt(newW, 2, srcWidth);
		newW &= ~1;
		rect.w = newW;
		rect.x = (srcWidth - newW) / 2;
		rect.x &= ~1;
	} else {
		int newH = int(std::lround(double(srcWidth) / target));
		newH = clampInt(newH, 2, srcHeight);
		newH &= ~1;
		rect.h = newH;
		rect.y = (srcHeight - newH) / 2;
		rect.y &= ~1;
	}
	return rect;
}

}
