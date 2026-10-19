// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

namespace km {
namespace platform {

// Two letter code of the language Windows is set to, limited to the languages
// this application ships translations for: "en", "ru", "ja", "id". Anything
// else reports "en". Only used to pick a starting language on a first run; the
// dashboard's language selector has the last word.
std::string system_locale();

}  // namespace platform
}  // namespace km
