#pragma once

#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace ocp {
namespace plugin {

struct PairedDevice {
	std::string name;
	std::string secretHex;
	int64_t addedAt = 0;
};

// Persisted table of paired phone secrets (trusted-on-first-use), stored as
// JSON in the OBS module configuration directory.
class PairedStore {
public:
	void load(const std::string &path);
	void save() const;

	bool get(const std::string &deviceId, PairedDevice *out) const;
	void put(const std::string &deviceId, const PairedDevice &device);
	void remove(const std::string &deviceId);
	void clear();
	size_t count() const;
	std::string summary() const;
	std::vector<std::pair<std::string, PairedDevice>> list() const;

private:
	mutable std::mutex mutex;
	std::string path;
	std::map<std::string, PairedDevice> devices;
};

}
}
