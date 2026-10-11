#pragma once

#include <string>

namespace ocp {
namespace plugin {

// Shows (or updates) the pairing QR overlay window. Thread-safe.
void showPairingQrWindow(const std::string &url, const std::string &code,
			 const std::string &addresses);
void closePairingQrWindow();

}
}
