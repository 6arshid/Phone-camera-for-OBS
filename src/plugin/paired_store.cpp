#include "paired_store.h"

#include "json/json.h"

#include <cstdio>
#include <sstream>

namespace ocp {
namespace plugin {

void PairedStore::load(const std::string &filePath)
{
	std::lock_guard<std::mutex> lock(mutex);
	path = filePath;
	devices.clear();

	FILE *f = fopen(filePath.c_str(), "rb");
	if (!f)
		return;
	std::ostringstream ss;
	char buf[4096];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
		ss.write(buf, n);
	fclose(f);

	std::string error;
	json::Value root = json::Value::parse(ss.str(), &error);
	if (!error.empty() || !root.isObject())
		return;

	const json::Value &list = root["devices"];
	if (!list.isObject())
		return;
	for (const auto &kv : list.members()) {
		PairedDevice device;
		device.name = kv.second.getString("name");
		device.secretHex = kv.second.getString("secret");
		device.addedAt = kv.second.getInt("added", 0);
		if (device.secretHex.size() == 64)
			devices[kv.first] = device;
	}
}

void PairedStore::save() const
{
	std::lock_guard<std::mutex> lock(mutex);
	if (path.empty())
		return;

	json::Value list = json::Value::object();
	for (const auto &kv : devices) {
		json::Value entry = json::Value::object();
		entry.set("name", kv.second.name);
		entry.set("secret", kv.second.secretHex);
		entry.set("added", kv.second.addedAt);
		list.set(kv.first, entry);
	}
	json::Value root = json::Value::object();
	root.set("devices", list);

	FILE *f = fopen(path.c_str(), "wb");
	if (!f)
		return;
	std::string text = root.dump();
	fwrite(text.data(), 1, text.size(), f);
	fclose(f);
}

bool PairedStore::get(const std::string &deviceId, PairedDevice *out) const
{
	std::lock_guard<std::mutex> lock(mutex);
	auto it = devices.find(deviceId);
	if (it == devices.end())
		return false;
	if (out)
		*out = it->second;
	return true;
}

void PairedStore::put(const std::string &deviceId, const PairedDevice &device)
{
	{
		std::lock_guard<std::mutex> lock(mutex);
		devices[deviceId] = device;
	}
	save();
}

void PairedStore::remove(const std::string &deviceId)
{
	{
		std::lock_guard<std::mutex> lock(mutex);
		devices.erase(deviceId);
	}
	save();
}

void PairedStore::clear()
{
	{
		std::lock_guard<std::mutex> lock(mutex);
		devices.clear();
	}
	save();
}

size_t PairedStore::count() const
{
	std::lock_guard<std::mutex> lock(mutex);
	return devices.size();
}

std::string PairedStore::summary() const
{
	std::lock_guard<std::mutex> lock(mutex);
	std::string out;
	for (const auto &kv : devices) {
		if (!out.empty())
			out += ", ";
		out += kv.second.name.empty() ? kv.first : kv.second.name;
	}
	return out;
}

std::vector<std::pair<std::string, PairedDevice>> PairedStore::list() const
{
	std::lock_guard<std::mutex> lock(mutex);
	std::vector<std::pair<std::string, PairedDevice>> out;
	out.reserve(devices.size());
	for (const auto &kv : devices)
		out.push_back(kv);
	return out;
}

}
}
