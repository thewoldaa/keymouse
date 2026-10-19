// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/locale.h"

#include <windows.h>

namespace km {
namespace platform {

std::string system_locale() {
    // The primary language id is the low ten bits of the LANGID, so a regional
    // variant (Russian in Belarus, say) still reports its language.
    switch (GetUserDefaultUILanguage() & 0x3FF) {
        case LANG_RUSSIAN: return "ru";
        case LANG_JAPANESE: return "ja";
        case LANG_INDONESIAN: return "id";
        default: return "en";
    }
}

}  // namespace platform
}  // namespace km
