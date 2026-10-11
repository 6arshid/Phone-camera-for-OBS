#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ocp {
namespace plugin {

struct StatusCardInput {
	std::string title;
	std::string line1;
	std::string line2;
	std::string line3;
	std::string line4;
	std::string pairingUrl;
};

// Renders a diagnostic card as top-down 32-bit BGRA.
// Returns false when the platform cannot render (no GDI/CG available).
bool renderStatusCard(const StatusCardInput &in, int width, int height,
		      std::vector<uint8_t> *bgraOut, int *strideOut);

}
}
