#include "source.h"

#include "net_util.h"
#include "nv12_util.h"
#include "qr_window.h"
#include "status_card.h"
#include "tr.h"

#include "settings/config.h"

#include <obs-module.h>
#include <util/platform.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <cstring>

namespace ocp {
namespace plugin {

namespace {

void copyToClipboard(const std::string &text)
{
	if (!OpenClipboard(nullptr))
		return;
	EmptyClipboard();
	int len = MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0);
	if (len > 0) {
		HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)(len + 1) * sizeof(wchar_t));
		if (mem) {
			wchar_t *out = (wchar_t *)GlobalLock(mem);
			MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), out,
					    len);
			out[len] = 0;
			GlobalUnlock(mem);
			SetClipboardData(CF_UNICODETEXT, mem);
		}
	}
	CloseClipboard();
}

const char *trState(int state)
{
	switch ((SourceState)state) {
	case SourceState::Idle:
		return tr("Status.Idle");
	case SourceState::Waiting:
		return tr("Status.Waiting");
	case SourceState::Connecting:
		return tr("Status.Connecting");
	case SourceState::Streaming:
		return tr("Status.Streaming");
	case SourceState::Error:
		return tr("Status.Error");
	}
	return "";
}

bool showQrClicked(obs_properties_t *, obs_property_t *, void *)
{
	Server &server = Server::instance();
	showPairingQrWindow(server.makeQrUrl(), server.pairingCode(),
			    server.addressesSummary());
	return true;
}

bool newCodeClicked(obs_properties_t *, obs_property_t *, void *)
{
	Server::instance().rotatePairingCode();
	return true;
}

bool copyInfoClicked(obs_properties_t *, obs_property_t *, void *data)
{
	PhoneSource *source = (PhoneSource *)data;
	Server &server = Server::instance();
	std::string text = std::string("Phone Camera (OCP/1)\n") + source->statusText() +
			   "\n" + tr("Pairing.Code") + ": " + server.pairingCode() + "\n" +
			   tr("Network.Addresses") + ": " + server.addressesSummary() +
			   "\ntcp/" + std::to_string(server.controlPort()) + " udp/" +
			   std::to_string(server.mediaPort()) + "\n" +
			   tr("Paired.Devices") + ": " + server.paired().summary();
	copyToClipboard(text);
	return true;
}

bool refreshDevicesClicked(obs_properties_t *props, obs_property_t *, void *)
{
	obs_property_t *device = obs_properties_get(props, "device");
	if (!device)
		return true;

	obs_property_list_clear(device);
	obs_property_list_add_string(device, tr("Device.Any"), "any");

	Server &server = Server::instance();
	for (const DiscoveredPhone &phone : server.phones()) {
		std::string label = phone.name.empty() ? phone.id : phone.name;
		if (!phone.os.empty())
			label += " (" + phone.os + ")";
		if (!phone.address.empty())
			label += " – " + phone.address;
		obs_property_list_add_string(device, label.c_str(), phone.id.c_str());
	}
	for (const auto &entry : server.paired().list()) {
		std::string label = entry.second.name.empty() ? entry.first
							      : entry.second.name;
		label += " ✓";
		obs_property_list_add_string(device, label.c_str(), entry.first.c_str());
	}
	return true;
}

bool disconnectClicked(obs_properties_t *, obs_property_t *, void *data)
{
	PhoneSource *source = (PhoneSource *)data;
	Server::instance().disconnectSource(source);
	return true;
}

bool forgetDevicesClicked(obs_properties_t *, obs_property_t *, void *)
{
	Server::instance().paired().clear();
	return true;
}

bool resolutionModified(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	const char *preset = obs_data_get_string(settings, "resolution");
	bool custom = preset && strcmp(preset, "custom") == 0;
	obs_property_t *w = obs_properties_get(props, "custom_width");
	obs_property_t *h = obs_properties_get(props, "custom_height");
	if (w)
		obs_property_set_visible(w, custom);
	if (h)
		obs_property_set_visible(h, custom);
	return true;
}

void *sourceCreate(obs_data_t *settings, obs_source_t *source)
{
	return new PhoneSource(settings, source);
}

void sourceDestroy(void *data) { delete (PhoneSource *)data; }

void sourceUpdate(void *data, obs_data_t *settings)
{
	((PhoneSource *)data)->updateSettings(settings);
}

void sourceActivate(void *data) { ((PhoneSource *)data)->activate(); }
void sourceDeactivate(void *data) { ((PhoneSource *)data)->deactivate(); }
void sourceShow(void *data) { ((PhoneSource *)data)->show(); }
void sourceHide(void *data) { ((PhoneSource *)data)->hide(); }
void sourceVideoTick(void *data, float seconds)
{
	((PhoneSource *)data)->videoTick(seconds);
}
uint32_t sourceGetWidth(void *data) { return ((PhoneSource *)data)->width(); }
uint32_t sourceGetHeight(void *data) { return ((PhoneSource *)data)->height(); }

const char *sourceGetName(void *) { return tr("Source.Name"); }

void sourceGetDefaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, "device", "any");
	obs_data_set_default_string(settings, "resolution", "auto");
	obs_data_set_default_int(settings, "custom_width", 1920);
	obs_data_set_default_int(settings, "custom_height", 1080);
	obs_data_set_default_string(settings, "fps", "auto");
	obs_data_set_default_int(settings, "bitrate_kbps", 0);
	obs_data_set_default_int(settings, "gop_s", 2);
	obs_data_set_default_string(settings, "camera", "back");
	obs_data_set_default_string(settings, "orientation", "auto");
	obs_data_set_default_string(settings, "aspect", "source");
	obs_data_set_default_bool(settings, "audio_enabled", false);
	obs_data_set_default_bool(settings, "status_overlay", true);
	obs_data_set_default_string(settings, "language", "auto");
	obs_data_set_default_bool(settings, "require_pairing", true);
	obs_data_set_default_int(settings, "control_port", 47100);
}

obs_properties_t *sourceGetProperties(void *data)
{
	PhoneSource *source = (PhoneSource *)data;
	Server &server = Server::instance();
	obs_properties_t *props = obs_properties_create_param(data, nullptr);

	std::string status = source ? source->statusText() : std::string();
	obs_properties_add_text(props, "info_status", status.c_str(), OBS_TEXT_INFO);
	obs_properties_add_button(props, "show_qr", tr("Button.ShowQr"), showQrClicked);
	obs_properties_add_button(props, "new_code", tr("Button.NewCode"), newCodeClicked);

	obs_property_t *device = obs_properties_add_list(props, "device", tr("Device.Label"),
							 OBS_COMBO_TYPE_LIST,
							 OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(device, tr("Device.Any"), "any");
	for (const DiscoveredPhone &phone : server.phones()) {
		std::string label = phone.name.empty() ? phone.id : phone.name;
		if (!phone.os.empty())
			label += " (" + phone.os + ")";
		if (!phone.address.empty())
			label += " – " + phone.address;
		obs_property_list_add_string(device, label.c_str(), phone.id.c_str());
	}
	for (const auto &entry : server.paired().list()) {
		std::string label = entry.second.name.empty() ? entry.first
							      : entry.second.name;
		label += " ✓";
		obs_property_list_add_string(device, label.c_str(), entry.first.c_str());
	}
	obs_properties_add_button(props, "refresh_devices", tr("Button.RefreshDevices"),
				  refreshDevicesClicked);

	obs_property_t *resolution = obs_properties_add_list(
		props, "resolution", tr("Resolution.Label"), OBS_COMBO_TYPE_LIST,
		OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(resolution, tr("Resolution.Auto"), "auto");
	obs_property_list_add_string(resolution, "2160p", "2160p");
	obs_property_list_add_string(resolution, "1440p", "1440p");
	obs_property_list_add_string(resolution, "1080p", "1080p");
	obs_property_list_add_string(resolution, "720p", "720p");
	obs_property_list_add_string(resolution, "480p", "480p");
	obs_property_list_add_string(resolution, tr("Resolution.Custom"), "custom");
	obs_property_set_modified_callback(resolution, resolutionModified);

	obs_properties_add_int(props, "custom_width", tr("Resolution.Width"), 160, 3840, 2);
	obs_properties_add_int(props, "custom_height", tr("Resolution.Height"), 120, 2160, 2);

	obs_property_t *fps = obs_properties_add_list(props, "fps", tr("Fps.Label"),
						      OBS_COMBO_TYPE_LIST,
						      OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(fps, tr("Fps.Auto"), "auto");
	obs_property_list_add_string(fps, "15", "15");
	obs_property_list_add_string(fps, "24", "24");
	obs_property_list_add_string(fps, "30", "30");
	obs_property_list_add_string(fps, "60", "60");

	obs_properties_add_int_slider(props, "bitrate_kbps", tr("Bitrate.Label"), 0, 20000,
				      100);
	obs_properties_add_int(props, "gop_s", tr("Gop.Label"), 1, 10, 1);

	obs_property_t *camera = obs_properties_add_list(props, "camera", tr("Camera.Label"),
							 OBS_COMBO_TYPE_LIST,
							 OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(camera, tr("Camera.Back"), "back");
	obs_property_list_add_string(camera, tr("Camera.Front"), "front");

	obs_property_t *orientation = obs_properties_add_list(
		props, "orientation", tr("Orientation.Label"), OBS_COMBO_TYPE_LIST,
		OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(orientation, tr("Orientation.Auto"), "auto");
	obs_property_list_add_string(orientation, tr("Orientation.Portrait"), "portrait");
	obs_property_list_add_string(orientation, tr("Orientation.Landscape"), "landscape");

	obs_property_t *aspect = obs_properties_add_list(props, "aspect", tr("Aspect.Label"),
							 OBS_COMBO_TYPE_LIST,
							 OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(aspect, tr("Aspect.Source"), "source");
	obs_property_list_add_string(aspect, "16:9", "16:9");
	obs_property_list_add_string(aspect, "9:16", "9:16");
	obs_property_list_add_string(aspect, "1:1", "1:1");

	obs_properties_add_bool(props, "audio_enabled", tr("Audio.Enabled"));
	obs_properties_add_bool(props, "status_overlay", tr("Overlay.Status"));

	obs_properties_add_text(props, "info_network",
				 (trString("Network.Summary") + ": " +
				  server.addressesSummary() + "  ·  tcp/" +
				  std::to_string(server.controlPort()) + "  udp/" +
				  std::to_string(server.mediaPort()))
					.c_str(),
				 OBS_TEXT_INFO);
	std::string code = server.pairingCode();
	obs_properties_add_text(props, "info_pairing",
				 (trString("Pairing.Code") + ": " + code + "   " +
				  trString("Pairing.QrHint"))
					.c_str(),
				 OBS_TEXT_INFO);

	std::string paired = server.paired().summary();
	obs_properties_add_text(props, "info_devices",
				 (trString("Paired.Devices") + ": " +
				  (paired.empty() ? tr("Paired.None") : paired))
					.c_str(),
				 OBS_TEXT_INFO);

	obs_properties_add_button(props, "copy_info", tr("Button.CopyInfo"),
				  copyInfoClicked);
	obs_properties_add_button(props, "disconnect", tr("Button.Disconnect"),
				  disconnectClicked);
	obs_properties_add_button(props, "forget_devices", tr("Button.Forget"),
				  forgetDevicesClicked);

	obs_property_t *language = obs_properties_add_list(
		props, "language", tr("Language.Label"), OBS_COMBO_TYPE_LIST,
		OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(language, tr("Language.Auto"), "auto");
	obs_property_list_add_string(language, "English", "en");
	obs_property_list_add_string(language, "فارسی", "fa");

	obs_properties_add_bool(props, "require_pairing", tr("Pairing.Required"));
	obs_properties_add_int(props, "control_port", tr("Network.Port"), 1024, 65535, 1);

	return props;
}

}

PhoneSource::PhoneSource(obs_data_t *settings, obs_source_t *handleIn) : handle(handleIn)
{
	updateSettings(settings);
	Server::instance().registerSource(this);
	state = (int)SourceState::Waiting;
}

PhoneSource::~PhoneSource()
{
	Server::instance().unregisterSource(this);
}

std::string PhoneSource::preferredDeviceId()
{
	std::lock_guard<std::mutex> lock(settingsMutex);
	return devicePref;
}

SessionSettings PhoneSource::currentSettings() const
{
	std::lock_guard<std::mutex> lock(settingsMutex);
	SessionSettings out;
	out.video = resolveVideoProfile(resolutionPreset, customWidth, customHeight,
					fpsPreset, bitrateKbps, gopS, orientation);
	out.camera = camera;
	out.audioEnabled = audioEnabled;
	out.streamActive = handle ? obs_source_showing(handle) || obs_source_active(handle) : streamActive;
	out.aspect = aspect;
	return out;
}

void PhoneSource::pushSettingsToSession()
{
	Session *s;
	{
		std::lock_guard<std::mutex> lock(sessionMutex);
		s = session;
	}
	if (s)
		s->applySettings(currentSettings());
}

void PhoneSource::updateSettings(obs_data_t *settings)
{
	{
		std::lock_guard<std::mutex> lock(settingsMutex);
		devicePref = obs_data_get_string(settings, "device");
		if (devicePref.empty())
			devicePref = "any";
		resolutionPreset = obs_data_get_string(settings, "resolution");
		customWidth = (int)obs_data_get_int(settings, "custom_width");
		customHeight = (int)obs_data_get_int(settings, "custom_height");
		fpsPreset = obs_data_get_string(settings, "fps");
		bitrateKbps = (int)obs_data_get_int(settings, "bitrate_kbps");
		gopS = (int)obs_data_get_int(settings, "gop_s");
		camera = obs_data_get_string(settings, "camera");
		orientation = obs_data_get_string(settings, "orientation");
		aspect = obs_data_get_string(settings, "aspect");
		audioEnabled = obs_data_get_bool(settings, "audio_enabled");
		statusOverlay = obs_data_get_bool(settings, "status_overlay");
		languagePref = obs_data_get_string(settings, "language");
		requirePairingPref = obs_data_get_bool(settings, "require_pairing");
		controlPortPref = (int)obs_data_get_int(settings, "control_port");
		if (controlPortPref < 1024 || controlPortPref > 65535)
			controlPortPref = 47100;
	}

	Server &server = Server::instance();
	ModuleConfig cfg = server.loadModuleConfig();
	cfg.language = languagePref;
	cfg.requirePairing = requirePairingPref;
	cfg.controlPort = (uint16_t)controlPortPref;
	server.saveModuleConfig(cfg);
	server.applyModuleConfig(cfg);

	if (!server.running()) {
		std::string err;
		if (!server.start((uint16_t)controlPortPref, &err))
			blog(LOG_ERROR, "[obs-phone-camera] server start failed: %s",
			     err.c_str());
	} else if (server.controlPort() != (uint16_t)controlPortPref) {
		if (!server.hasAuthedSessions()) {
			server.shutdown();
			std::string err;
			if (!server.start((uint16_t)controlPortPref, &err))
				blog(LOG_ERROR,
				     "[obs-phone-camera] server restart failed: %s",
				     err.c_str());
		} else {
			blog(LOG_WARNING,
			     "[obs-phone-camera] port change requires an idle server");
		}
	}

	pushSettingsToSession();
	server.requestAttach(this);
}

void PhoneSource::activate()
{
	{
		std::lock_guard<std::mutex> lock(settingsMutex);
		streamActive = true;
	}
	pushSettingsToSession();
	Server::instance().requestAttach(this);
}

void PhoneSource::deactivate()
{
	{
		std::lock_guard<std::mutex> lock(settingsMutex);
		streamActive = false;
	}
	pushSettingsToSession();
}

void PhoneSource::show()
{
	{
		std::lock_guard<std::mutex> lock(settingsMutex);
		streamActive = true;
	}
	pushSettingsToSession();
	Server::instance().requestAttach(this);
}

void PhoneSource::hide()
{
	{
		std::lock_guard<std::mutex> lock(settingsMutex);
		streamActive = false;
	}
	pushSettingsToSession();
}

void PhoneSource::videoTick(float seconds)
{
	int st = state.load();
	if (st == (int)SourceState::Streaming || st == (int)SourceState::Error)
		return;

	bool overlay;
	{
		std::lock_guard<std::mutex> lock(settingsMutex);
		overlay = statusOverlay;
	}
	if (!overlay)
		return;

	tickAccum += seconds;
	if (tickAccum < 0.5f)
		return;
	tickAccum = 0.0f;
	maybeOutputStatusCard();
}

void PhoneSource::maybeOutputStatusCard()
{
	if (!handle)
		return;

	StatusCardInput card;
	card.title = tr("Source.Name");
	card.line1 = statusText();
	{
		Server &server = Server::instance();
		card.line2 = std::string(tr("Pairing.Code")) + ": " + server.pairingCode();
		card.line3 = std::string(tr("Network.Addresses")) + ": " +
			     server.addressesSummary();
		card.line4 = "OCP/1 · tcp/" + std::to_string(server.controlPort()) +
			     " udp/" + std::to_string(server.mediaPort());
		card.pairingUrl = server.makeQrUrl();
	}

	int stride = 0;
	if (!renderStatusCard(card, 1280, 720, &cardBuffer, &stride))
		return;
	cardStride = stride;

	obs_source_frame frame;
	fillBgraFrame(&frame, cardBuffer.data(), 1280, 720, cardStride, os_gettime_ns());
	obs_source_output_video(handle, &frame);
	outWidth = 1280;
	outHeight = 720;
}

uint32_t PhoneSource::width() const
{
	int w = outWidth.load();
	if (w > 0)
		return (uint32_t)w;
	bool overlay;
	{
		std::lock_guard<std::mutex> lock(settingsMutex);
		overlay = statusOverlay;
	}
	return overlay ? 1280 : 0;
}

uint32_t PhoneSource::height() const
{
	int h = outHeight.load();
	if (h > 0)
		return (uint32_t)h;
	bool overlay;
	{
		std::lock_guard<std::mutex> lock(settingsMutex);
		overlay = statusOverlay;
	}
	return overlay ? 720 : 0;
}

void PhoneSource::onSessionAuthenticated(Session *s)
{
	{
		std::lock_guard<std::mutex> lock(sessionMutex);
		session = s;
	}
	state = (int)SourceState::Connecting;
	{
		std::lock_guard<std::mutex> lock(infoMutex);
		phoneName = s->deviceName();
		lastError.clear();
	}
	pushSettingsToSession();
}

void PhoneSource::onSessionClosed(Session *s, const std::string &reason)
{
	{
		std::lock_guard<std::mutex> lock(sessionMutex);
		if (session == s)
			session = nullptr;
	}
	state = (int)SourceState::Waiting;
	{
		std::lock_guard<std::mutex> lock(infoMutex);
		lastError = reason;
	}
	outWidth = 0;
	outHeight = 0;
}

void PhoneSource::onPhoneStats(const StatsMessage &stats, double latency, double loss)
{
	batteryPct = stats.battery;
	latencyMs = latency;
	lossPct = loss;
	phoneFps = (int)(stats.fps + 0.5);
	std::lock_guard<std::mutex> lock(infoMutex);
	thermalState = stats.thermal;
	if (stats.frames > 0)
		state = (int)SourceState::Streaming;
}

void PhoneSource::onVideoDimensions(int width, int height)
{
	outWidth = width;
	outHeight = height;
	state = (int)SourceState::Streaming;
}

std::string PhoneSource::statusText() const
{
	int st = state.load();
	std::string detail;
	{
		std::lock_guard<std::mutex> lock(infoMutex);
		detail = phoneName;
	}
	char buf[256];
	switch ((SourceState)st) {
	case SourceState::Idle:
		return tr("Status.Idle");
	case SourceState::Waiting:
		return trString("Status.Waiting") + " · " + Server::instance().addressesSummary();
	case SourceState::Connecting:
		return trString("Status.Connecting") + " " + detail;
	case SourceState::Streaming: {
		int w = outWidth.load(), h = outHeight.load();
		snprintf(buf, sizeof(buf), "%d×%d · %d fps · %.0f ms · loss %.1f%%", w, h,
			 phoneFps.load(), latencyMs.load(), lossPct.load());
		int bat = batteryPct.load();
		std::string text = trString("Status.Streaming") + " " + detail + "  " + buf;
		if (bat >= 0) {
			snprintf(buf, sizeof(buf), " · battery %d%%", bat);
			text += buf;
		}
		return text;
	}
	case SourceState::Error:
		return tr("Status.Error");
	}
	return std::string();
}

void PhoneSource::registerSource()
{
	static obs_source_info info = {};
	info.id = "obs_phone_camera";
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_ASYNC_VIDEO | OBS_SOURCE_AUDIO;
	info.get_name = sourceGetName;
	info.get_defaults = sourceGetDefaults;
	info.get_properties = sourceGetProperties;
	info.create = sourceCreate;
	info.destroy = sourceDestroy;
	info.update = sourceUpdate;
	info.activate = sourceActivate;
	info.deactivate = sourceDeactivate;
	info.show = sourceShow;
	info.hide = sourceHide;
	info.video_tick = sourceVideoTick;
	info.get_width = sourceGetWidth;
	info.get_height = sourceGetHeight;
	obs_register_source(&info);
}

}
}
