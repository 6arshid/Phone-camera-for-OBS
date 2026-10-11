#include <obs-module.h>

#include "server.h"
#include "source.h"
#include "tr.h"

OBS_DECLARE_MODULE()
OBS_MODULE_AUTHOR("obs-phone-camera contributors")

MODULE_EXPORT const char *obs_module_name(void) { return "Phone Camera"; }

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Use a phone as a wireless camera source (OCP/1 protocol).";
}

static lookup_t *moduleLookup = NULL;

void obs_module_set_locale(const char *locale)
{
	if (moduleLookup)
		text_lookup_destroy(moduleLookup);
	moduleLookup = obs_module_load_locale(obs_current_module(), "en-US", locale);
}

void obs_module_free_locale(void)
{
	if (moduleLookup)
		text_lookup_destroy(moduleLookup);
	moduleLookup = NULL;
}

const char *obs_module_text(const char *val)
{
	const char *out = val;
	if (moduleLookup)
		text_lookup_getstr(moduleLookup, val, &out);
	return out;
}

bool obs_module_get_string(const char *val, const char **out)
{
	return moduleLookup ? text_lookup_getstr(moduleLookup, val, out) : false;
}

bool obs_module_load(void)
{
	blog(LOG_INFO, "[obs-phone-camera] loading (protocol ocp/1)");
	ocp::plugin::PhoneSource::registerSource();
	return true;
}

void obs_module_unload(void)
{
	ocp::plugin::Server::instance().shutdown();
	blog(LOG_INFO, "[obs-phone-camera] unloaded");
}
