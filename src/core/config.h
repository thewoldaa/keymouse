// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/action.h"
#include "core/binding.h"
#include "core/motion.h"

namespace km {

inline constexpr int kConfigSchemaVersion = 1;

// A named set of bindings, optionally activated automatically when a
// particular executable is in the foreground.
struct Profile {
    std::string name = "Default";
    // Executable name to match, e.g. "game.exe". Empty means manual only.
    std::string match_exe;
    std::vector<Binding> bindings;
};

struct AppConfig {
    int schema = kConfigSchemaVersion;

    // General
    std::string locale = "en";
    bool enabled = true;
    bool start_with_windows = true;
    bool start_minimized = true;
    bool show_tray_icon = true;

    // Safety
    // Key that disables every mapping. Must remain reachable even if the user
    // misconfigures a binding, so it is validated against being bound.
    Chord panic_key{ModNone, key::Pause};
    // Physical key used as the Fn layer modifier.
    KeyId fn_layer_key = key::Menu;

    // Movement
    MotionConfig motion;
    double scroll_lines_per_second = 8.0;

    // Profiles
    std::vector<Profile> profiles{Profile{}};
    size_t active_profile = 0;
    bool auto_switch_profiles = true;

    // Returns the active profile, or the first one if the index is stale.
    Profile& active();
    const Profile& active() const;

    // True when `chord` is already used by any binding in any profile.
    bool chord_in_use(const Chord& chord, const std::string& ignore_profile = {}) const;
};

// Default configuration used on first run: the "desktop" preset.
AppConfig make_default_config();

// Presets selectable during onboarding.
enum class Preset { Desktop, Numpad, Game };
AppConfig make_preset(Preset preset);

// Serialization. to_json produces pretty-printed, stable key order.
std::string to_json(const AppConfig& config);

// Returns nullopt and fills `error` on malformed input. Unknown keys are
// ignored so that configs written by a newer version still load.
std::optional<AppConfig> from_json(std::string_view text, std::string& error);

// Clamps and repairs out-of-range values. Called after every load so a
// hand-edited file cannot put the engine into a bad state.
void sanitize(AppConfig& config);

// Validates hard constraints (panic key not bound, no duplicate chords within
// a profile, at least one movement binding). Returns human readable problems.
std::vector<std::string> validate(const AppConfig& config);

}  // namespace km
