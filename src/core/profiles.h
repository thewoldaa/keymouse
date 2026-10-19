// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "core/binding.h"
#include "core/config.h"

namespace km {

struct ProfileMatch {
    size_t index = 0;
    bool matched = false;
};

// Selects a profile for the given foreground executable.
//
// Matching is case insensitive on the file name only ("game.exe"), not on the
// full path, so a user does not have to re-pick a profile after moving an
// installation. Profiles without a match_exe are never auto-selected.
ProfileMatch match_profile(const AppConfig& config, std::string_view exe_name);

// Returns the lowercased file name portion of a path. Handles both separators
// because configs are hand-editable and users copy paths from anywhere.
std::string exe_name_from_path(std::string_view path);

// Applies a preset to an existing config, preserving general settings such as
// locale and autostart. Replaces the bindings of the active profile.
void apply_preset(AppConfig& config, Preset preset);

}  // namespace km
