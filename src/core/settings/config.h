#pragma once

#include <cstdint>
#include <string>

namespace ocp {

struct VideoProfile {
	int width = 1920;
	int height = 1080;
	int fps = 30;
	int bitrateKbps = 0;
	int gopS = 2;
	std::string orientation = "auto";
};

struct CropRect {
	int x = 0;
	int y = 0;
	int w = 0;
	int h = 0;
};

// preset: "auto" | "2160p" | "1440p" | "1080p" | "720p" | "480p" | "custom"
VideoProfile resolveVideoProfile(const std::string &resolutionPreset, int customWidth,
				 int customHeight, const std::string &fpsPreset, int bitrateKbps,
				 int gopS, const std::string &orientation);

int autoBitrateKbps(int width, int height, int fps);

// aspect: "source" (no crop), "16:9", "9:16", "1:1"
CropRect centerCrop(int srcWidth, int srcHeight, const std::string &aspect);

}
