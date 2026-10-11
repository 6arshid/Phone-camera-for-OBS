#include "tr.h"

#include <obs-module.h>

#include <map>
#include <mutex>
#include <string>

namespace ocp {
namespace plugin {

namespace {

std::mutex g_mutex;
Language g_language = Language::Auto;
std::map<std::string, std::string> g_en;
std::map<std::string, std::string> g_fa;
bool g_loaded = false;

void parseIniFile(const char *path, std::map<std::string, std::string> &out)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return;

	char line[1024];
	while (fgets(line, sizeof(line), f)) {
		std::string text(line);
		while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
			text.pop_back();
		if (text.empty() || text[0] == '#' || text[0] == ';')
			continue;
		size_t eq = text.find('=');
		if (eq == std::string::npos || eq == 0)
			continue;
		std::string value = text.substr(eq + 1);
		if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
			value = value.substr(1, value.size() - 2);
		out[text.substr(0, eq)] = value;
	}
	fclose(f);
}

void loadLocked()
{
	if (g_loaded)
		return;
	g_loaded = true;

	char *enPath = obs_module_file("locale/en-US.ini");
	if (enPath) {
		parseIniFile(enPath, g_en);
		bfree(enPath);
	}
	char *faPath = obs_module_file("locale/fa-IR.ini");
	if (faPath) {
		parseIniFile(faPath, g_fa);
		bfree(faPath);
	}
}

const std::string &lookupLocked(const std::map<std::string, std::string> &table, const char *key)
{
	static std::string fallback;
	auto it = table.find(key);
	if (it != table.end())
		return it->second;
	auto enIt = g_en.find(key);
	if (enIt != g_en.end())
		return enIt->second;
	fallback = key;
	return fallback;
}

}

void setLanguage(Language lang)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	g_language = lang;
}

Language language()
{
	std::lock_guard<std::mutex> lock(g_mutex);
	return g_language;
}

const char *tr(const char *key)
{
	Language lang;
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		lang = g_language;
	}

	if (lang == Language::Auto) {
		const char *translated = obs_module_text(key);
		return translated ? translated : key;
	}

	std::lock_guard<std::mutex> lock(g_mutex);
	loadLocked();
	if (lang == Language::Persian)
		return lookupLocked(g_fa, key).c_str();
	return lookupLocked(g_en, key).c_str();
}

std::string trString(const char *key) { return std::string(tr(key)); }

}
}
