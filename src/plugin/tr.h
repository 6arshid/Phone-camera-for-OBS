#pragma once

#include <string>

namespace ocp {
namespace plugin {

enum class Language { Auto, English, Persian };

void setLanguage(Language lang);
Language language();

// Translates a key using the configured language. Returns the key itself
// when no translation exists.
const char *tr(const char *key);

std::string trString(const char *key);

}
}
