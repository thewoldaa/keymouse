// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/profiles.h"

#include <algorithm>
#include <cctype>

namespace km {

std::string exe_name_from_path(std::string_view path) {
    const size_t sep = path.find_last_of("\\/");
    std::string_view name = sep == std::string_view::npos ? path : path.substr(sep + 1);

    std::string out;
    out.reserve(name.size());
    for (char c : name) {
        out.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

ProfileMatch match_profile(const AppConfig& config, std::string_view exe_name) {
    if (exe_name.empty()) {
        return {};
    }

    const std::string needle = exe_name_from_path(exe_name);
    for (size_t i = 0; i < config.profiles.size(); ++i) {
        const auto& profile = config.profiles[i];
        if (profile.match_exe.empty()) {
            continue;
        }
        if (exe_name_from_path(profile.match_exe) == needle) {
            return ProfileMatch{i, true};
        }
    }
    return {};
}

void apply_preset(AppConfig& config, Preset preset) {
    // Keep everything that is not a binding: locale, autostart, panic key,
    // Fn layer key, motion tuning.
    AppConfig fresh = make_preset(preset);
    if (config.profiles.empty()) {
        config.profiles.push_back(Profile{});
    }

    Profile& target = config.active();
    const std::string preserved_name = target.name;
    const std::string preserved_exe = target.match_exe;

    target.bindings = fresh.active().bindings;
    target.name = preserved_name;
    target.match_exe = preserved_exe;

    config.motion = fresh.motion;
    sanitize(config);
}

}  // namespace km
