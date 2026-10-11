// Minimal OBS host: loads obs-phone-camera.dll into libobs, verifies the
// source type registers, creates/updates/destroys a source instance, and
// shuts down cleanly. Run via CTest with arguments:
//   module_host <plugin.dll> <plugin-data-dir>

#include "handshake.h"
#include <obs.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace {

int failures = 0;

#define CHECK(cond, msg)                                                     \
	do {                                                                 \
		if (!(cond)) {                                                 \
			fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__,   \
				__LINE__);                                    \
			failures++;                                          \
		} else {                                                       \
			fprintf(stderr, "OK:   %s\n", msg);                   \
		}                                                              \
	} while (0)

bool sourceTypeRegistered(const char *id)
{
	size_t idx = 0;
	const char *cur = nullptr;
	while (obs_enum_source_types(idx++, &cur)) {
		if (cur && strcmp(cur, id) == 0)
			return true;
	}
	return false;
}

}

int main(int argc, char **argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: module_host <plugin.dll> <data-dir>\n");
		return 2;
	}
	const char *dllPath = argv[1];
	const char *dataDir = argv[2];

	if (!obs_startup("en-US", nullptr, nullptr)) {
		fprintf(stderr, "FAIL: obs_startup\n");
		return 1;
	}
	fprintf(stderr, "OK:   obs_startup\n");

	obs_module_t *module = nullptr;
	int openRc = obs_open_module(&module, dllPath, dataDir);
	CHECK(openRc == 0 && module != nullptr, "obs_open_module");
	if (!module) {
		obs_shutdown();
		return 1;
	}

	CHECK(obs_init_module(module), "obs_init_module");

	CHECK(sourceTypeRegistered("obs_phone_camera"),
	      "source type obs_phone_camera registered");

	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "resolution", "720p");
	obs_data_set_int(settings, "control_port", 47100);
	obs_data_set_bool(settings, "require_pairing", true);
	obs_data_set_bool(settings, "status_overlay", false);

	obs_source_t *source =
		obs_source_create("obs_phone_camera", "phone-test", settings,
				  nullptr);
	CHECK(source != nullptr, "obs_source_create");
	if (source) {
		obs_source_inc_showing(source);
		obs_properties_t *properties = obs_source_properties(source);
		auto qrButton = obs_properties_get(properties, "show_qr");
		CHECK(qrButton && strcmp(obs_property_description(qrButton), "Show pairing QR") == 0,
		      "QR button has complete translated label");
		for (int attempt = 0; qrButton && attempt < 2; ++attempt) {
			obs_property_button_clicked(qrButton, source);
			HWND window = nullptr;
			for (int wait = 0; wait < 100 && !window; ++wait) {
				Sleep(10);
				window = FindWindowW(L"ObsPhoneCameraPairingQr", nullptr);
			}
			CHECK(window != nullptr, "pairing QR window opens and reopens");
			if (window) {
				PostMessageW(window, WM_CLOSE, 0, 0);
				for (int wait = 0; wait < 100 && IsWindow(window); ++wait)
					Sleep(10);
				Sleep(50);
			}
		}
		auto codeProperty = obs_properties_get(properties, "info_pairing");
		std::string codeLabel = obs_property_description(codeProperty);
		size_t codeStart = codeLabel.find(": ");
		std::string code = codeStart == std::string::npos ? "" : codeLabel.substr(codeStart + 2, 6);
		CHECK(ocp::isValidPairingCode(code), "pairing code property");
		try {
			auto master = checkHandshake(code, nullptr, source);
			CHECK(true, "pairing code TCP handshake and encrypted config");
			CHECK(true, "encrypted UDP H264 delivered to OBS source");
			Sleep(300);
			checkHandshake("", &master);
			CHECK(true, "stored secret HKDF reconnect and encrypted config");
		} catch (const std::exception &error) {
			fprintf(stderr, "handshake error: %s\n", error.what());
			CHECK(false, "TCP handshake regression");
		}
		obs_properties_destroy(properties);
		CHECK(strcmp(obs_source_get_id(source), "obs_phone_camera") == 0,
		      "obs_source_get_id");
		CHECK(strcmp(obs_source_get_name(source), "phone-test") == 0,
		      "obs_source_get_name");
		CHECK(obs_source_get_width(source) == 0,
		      "no video before phone connects (overlay off)");
		CHECK(obs_source_get_height(source) == 0,
		      "no video before phone connects (overlay off)");

		obs_data_set_string(settings, "resolution", "1080p");
		obs_data_set_int(settings, "bitrate_kbps", 6000);
		obs_source_update(source, settings);

		obs_data_t *roundtrip = obs_source_get_settings(source);
		CHECK(roundtrip != nullptr, "obs_source_get_settings");
		if (roundtrip) {
			CHECK(strcmp(obs_data_get_string(roundtrip, "resolution"),
				     "1080p") == 0,
			      "settings round-trip: resolution");
			CHECK(obs_data_get_int(roundtrip, "bitrate_kbps") == 6000,
			      "settings round-trip: bitrate_kbps");
			obs_data_release(roundtrip);
		}

		obs_source_dec_showing(source);
		obs_source_release(source);
		CHECK(true, "obs_source_release");
	}

	obs_data_release(settings);
	obs_shutdown(); // unloads modules
	CHECK(true, "obs_shutdown");

	if (failures) {
		fprintf(stderr, "module_host: %d failure(s)\n", failures);
		return 1;
	}
	fprintf(stderr, "module_host: all checks passed\n");
	return 0;
}
