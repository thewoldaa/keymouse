// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/config.h"

#include <algorithm>
#include <unordered_set>

#include <nlohmann/json.hpp>

namespace km {
namespace {

using json = nlohmann::json;

json chord_to_json(const Chord& chord) {
    json mods = json::array();
    if (chord.mods & ModCtrl) mods.push_back("ctrl");
    if (chord.mods & ModAlt) mods.push_back("alt");
    if (chord.mods & ModShift) mods.push_back("shift");
    if (chord.mods & ModWin) mods.push_back("win");
    if (chord.mods & ModFn) mods.push_back("fn");
    return json{{"mods", mods}, {"key", std::string(key_name(chord.key))}};
}

Chord chord_from_json(const json& value) {
    Chord chord;
    if (value.contains("key") && value["key"].is_string()) {
        const auto id = key_from_name(value["key"].get<std::string>());
        if (id) {
            chord.key = *id;
        }
    }
    if (value.contains("mods") && value["mods"].is_array()) {
        for (const auto& mod : value["mods"]) {
            if (!mod.is_string()) {
                continue;
            }
            const std::string name = mod.get<std::string>();
            if (name == "ctrl") chord.mods |= ModCtrl;
            else if (name == "alt") chord.mods |= ModAlt;
            else if (name == "shift") chord.mods |= ModShift;
            else if (name == "win") chord.mods |= ModWin;
            else if (name == "fn") chord.mods |= ModFn;
        }
    }
    return chord;
}

json binding_to_json(const Binding& binding) {
    return json{
        {"action", std::string(action_name(binding.action))},
        {"chord", chord_to_json(binding.chord)},
        {"mode", std::string(bind_mode_name(binding.mode))},
        {"swallow", std::string(swallow_mode_name(binding.swallow))},
    };
}

Binding binding_from_json(const json& value) {
    Binding binding;
    if (value.contains("action") && value["action"].is_string()) {
        binding.action = action_from_name(value["action"].get<std::string>());
    }
    if (value.contains("chord") && value["chord"].is_object()) {
        binding.chord = chord_from_json(value["chord"]);
    }
    if (value.contains("mode") && value["mode"].is_string()) {
        binding.mode = bind_mode_from_name(value["mode"].get<std::string>());
    }
    if (value.contains("swallow") && value["swallow"].is_string()) {
        binding.swallow = swallow_mode_from_name(value["swallow"].get<std::string>());
    }
    return binding;
}

json profile_to_json(const Profile& profile) {
    json bindings = json::array();
    for (const auto& binding : profile.bindings) {
        bindings.push_back(binding_to_json(binding));
    }
    return json{
        {"name", profile.name},
        {"match_exe", profile.match_exe},
        {"bindings", bindings},
    };
}

Profile profile_from_json(const json& value) {
    Profile profile;
    if (value.contains("name") && value["name"].is_string()) {
        profile.name = value["name"].get<std::string>();
    }
    if (value.contains("match_exe") && value["match_exe"].is_string()) {
        profile.match_exe = value["match_exe"].get<std::string>();
    }
    if (value.contains("bindings") && value["bindings"].is_array()) {
        for (const auto& item : value["bindings"]) {
            if (item.is_object()) {
                profile.bindings.push_back(binding_from_json(item));
            }
        }
    }
    return profile;
}

json motion_to_json(const MotionConfig& motion) {
    return json{
        {"speed_px_s", motion.speed_px_s},
        {"accel_ms", motion.accel_ms},
        {"precision_mult", motion.precision_mult},
        {"turbo_mult", motion.turbo_mult},
        {"diagonal_normalize", motion.diagonal_normalize},
    };
}

MotionConfig motion_from_json(const json& value) {
    MotionConfig motion;
    auto read = [&value](const char* key, double& out) {
        if (value.contains(key) && value[key].is_number()) {
            out = value[key].get<double>();
        }
    };
    read("speed_px_s", motion.speed_px_s);
    read("accel_ms", motion.accel_ms);
    read("precision_mult", motion.precision_mult);
    read("turbo_mult", motion.turbo_mult);
    if (value.contains("diagonal_normalize") && value["diagonal_normalize"].is_boolean()) {
        motion.diagonal_normalize = value["diagonal_normalize"].get<bool>();
    }
    return motion;
}

Binding make_binding(Action action, ModMask mods, KeyId key,
                     BindMode mode = BindMode::Hold,
                     SwallowMode swallow = SwallowMode::Swallow) {
    Binding binding;
    binding.action = action;
    binding.chord = Chord{mods, key};
    binding.mode = mode;
    binding.swallow = swallow;
    return binding;
}

}  // namespace

Profile& AppConfig::active() {
    if (profiles.empty()) {
        profiles.emplace_back();
        active_profile = 0;
    }
    if (active_profile >= profiles.size()) {
        active_profile = 0;
    }
    return profiles[active_profile];
}

const Profile& AppConfig::active() const {
    if (profiles.empty()) {
        // Cannot mutate in a const method; callers that need an empty config
        // repaired should call sanitize() first.
        static const Profile kEmpty{};
        return kEmpty;
    }
    const size_t index = active_profile < profiles.size() ? active_profile : 0;
    return profiles[index];
}

bool AppConfig::chord_in_use(const Chord& chord, const std::string& ignore_profile) const {
    for (const auto& profile : profiles) {
        if (!ignore_profile.empty() && profile.name == ignore_profile) {
            continue;
        }
        for (const auto& binding : profile.bindings) {
            if (binding.chord == chord) {
                return true;
            }
        }
    }
    return false;
}

AppConfig make_default_config() {
    return make_preset(Preset::Desktop);
}

AppConfig make_preset(Preset preset) {
    AppConfig config;
    config.profiles.clear();

    Profile profile;
    profile.name = "Default";

    switch (preset) {
        case Preset::Desktop:
            profile.bindings = {
                make_binding(Action::MoveUp, ModNone, key::ArrowUp),
                make_binding(Action::MoveDown, ModNone, key::ArrowDown),
                make_binding(Action::MoveLeft, ModNone, key::ArrowLeft),
                make_binding(Action::MoveRight, ModNone, key::ArrowRight),
                make_binding(Action::ClickLeft, ModCtrl, key::Numpad0, BindMode::Tap),
                make_binding(Action::ClickRight, ModCtrl, key::NumpadDecimal, BindMode::Tap),
                make_binding(Action::ClickMiddle, ModCtrl, key::Numpad5, BindMode::Tap),
                make_binding(Action::ScrollUp, ModCtrl, key::NumpadAdd),
                make_binding(Action::ScrollDown, ModCtrl, key::NumpadSubtract),
                make_binding(Action::Precision, ModCtrl | ModShift, key::NumpadEnter),
                make_binding(Action::DragLock, ModFn, key::Numpad0, BindMode::Toggle),
            };
            break;

        case Preset::Numpad:
            profile.name = "Numpad";
            profile.bindings = {
                make_binding(Action::MoveUp, ModNone, key::Numpad8),
                make_binding(Action::MoveDown, ModNone, key::Numpad2),
                make_binding(Action::MoveLeft, ModNone, key::Numpad4),
                make_binding(Action::MoveRight, ModNone, key::Numpad6),
                make_binding(Action::ClickLeft, ModNone, key::Numpad5, BindMode::Tap),
                make_binding(Action::ClickRight, ModNone, key::NumpadDivide, BindMode::Tap),
                make_binding(Action::DragLock, ModNone, key::NumpadDecimal, BindMode::Toggle),
                make_binding(Action::ScrollUp, ModNone, key::NumpadAdd),
                make_binding(Action::ScrollDown, ModNone, key::NumpadSubtract),
            };
            break;

        case Preset::Game:
            profile.name = "Game";
            profile.bindings = {
                make_binding(Action::MoveUp, ModCtrl | ModAlt, key::ArrowUp),
                make_binding(Action::MoveDown, ModCtrl | ModAlt, key::ArrowDown),
                make_binding(Action::MoveLeft, ModCtrl | ModAlt, key::ArrowLeft),
                make_binding(Action::MoveRight, ModCtrl | ModAlt, key::ArrowRight),
                make_binding(Action::ClickLeft, ModCtrl | ModAlt, key::Numpad1, BindMode::Tap),
                make_binding(Action::ClickRight, ModCtrl | ModAlt, key::Numpad2, BindMode::Tap),
                make_binding(Action::DragLock, ModCtrl | ModAlt, key::Numpad3, BindMode::Toggle),
                make_binding(Action::CursorCenter, ModCtrl | ModAlt, key::Numpad5, BindMode::Tap),
                make_binding(Action::Precision, ModCtrl | ModAlt | ModShift, key::NumpadEnter),
            };
            config.motion.speed_px_s = 900.0;
            config.motion.accel_ms = 60.0;
            break;
    }

    config.profiles.push_back(std::move(profile));
    return config;
}

std::string to_json(const AppConfig& config) {
    json bindings_by_profile = json::array();
    for (const auto& profile : config.profiles) {
        bindings_by_profile.push_back(profile_to_json(profile));
    }

    const json root{
        {"schema", config.schema},
        {"locale", config.locale},
        {"enabled", config.enabled},
        {"start_with_windows", config.start_with_windows},
        {"start_minimized", config.start_minimized},
        {"show_tray_icon", config.show_tray_icon},
        {"panic_key", chord_to_json(config.panic_key)},
        {"fn_layer_key", std::string(key_name(config.fn_layer_key))},
        {"motion", motion_to_json(config.motion)},
        {"scroll_lines_per_second", config.scroll_lines_per_second},
        {"active_profile", config.active_profile},
        {"auto_switch_profiles", config.auto_switch_profiles},
        {"profiles", bindings_by_profile},
    };
    return root.dump(2);
}

std::optional<AppConfig> from_json(std::string_view text, std::string& error) {
    json root;
    try {
        root = json::parse(text.begin(), text.end());
    } catch (const std::exception& ex) {
        error = std::string("JSON parse error: ") + ex.what();
        return std::nullopt;
    }

    if (!root.is_object()) {
        error = "Config root must be a JSON object";
        return std::nullopt;
    }

    AppConfig config;
    if (root.contains("schema") && root["schema"].is_number_integer()) {
        config.schema = root["schema"].get<int>();
    }
    if (config.schema > kConfigSchemaVersion) {
        error = "Config schema " + std::to_string(config.schema) +
                " is newer than this build supports (" +
                std::to_string(kConfigSchemaVersion) + ")";
        return std::nullopt;
    }

    auto read_string = [&root](const char* key, std::string& out) {
        if (root.contains(key) && root[key].is_string()) {
            out = root[key].get<std::string>();
        }
    };
    auto read_bool = [&root](const char* key, bool& out) {
        if (root.contains(key) && root[key].is_boolean()) {
            out = root[key].get<bool>();
        }
    };

    read_string("locale", config.locale);
    read_bool("enabled", config.enabled);
    read_bool("start_with_windows", config.start_with_windows);
    read_bool("start_minimized", config.start_minimized);
    read_bool("show_tray_icon", config.show_tray_icon);
    read_bool("auto_switch_profiles", config.auto_switch_profiles);

    if (root.contains("panic_key") && root["panic_key"].is_object()) {
        config.panic_key = chord_from_json(root["panic_key"]);
    }

    if (root.contains("fn_layer_key") && root["fn_layer_key"].is_string()) {
        const auto id = key_from_name(root["fn_layer_key"].get<std::string>());
        if (id) {
            config.fn_layer_key = *id;
        }
    }

    if (root.contains("motion") && root["motion"].is_object()) {
        config.motion = motion_from_json(root["motion"]);
    }

    if (root.contains("scroll_lines_per_second") && root["scroll_lines_per_second"].is_number()) {
        config.scroll_lines_per_second = root["scroll_lines_per_second"].get<double>();
    }

    if (root.contains("active_profile") && root["active_profile"].is_number_unsigned()) {
        config.active_profile = root["active_profile"].get<size_t>();
    }

    if (root.contains("profiles") && root["profiles"].is_array()) {
        config.profiles.clear();
        for (const auto& item : root["profiles"]) {
            if (item.is_object()) {
                config.profiles.push_back(profile_from_json(item));
            }
        }
    }

    sanitize(config);
    return config;
}

void sanitize(AppConfig& config) {
    config.schema = kConfigSchemaVersion;

    if (config.profiles.empty()) {
        config.profiles.push_back(Profile{});
    }
    if (config.active_profile >= config.profiles.size()) {
        config.active_profile = 0;
    }

    if (config.locale != "en" && config.locale != "ru" &&
        config.locale != "ja" && config.locale != "id") {
        config.locale = "en";
    }

    config.motion.speed_px_s = std::clamp(config.motion.speed_px_s,
        MotionConfig::kMinSpeed, MotionConfig::kMaxSpeed);
    config.motion.accel_ms = std::clamp(config.motion.accel_ms,
        MotionConfig::kMinAccel, MotionConfig::kMaxAccel);
    config.motion.precision_mult = std::clamp(config.motion.precision_mult, 0.01, 1.0);
    config.motion.turbo_mult = std::clamp(config.motion.turbo_mult, 1.0, 20.0);
    config.scroll_lines_per_second = std::clamp(config.scroll_lines_per_second, 1.0, 60.0);

    if (config.panic_key.key == key::Invalid) {
        config.panic_key = Chord{ModNone, key::Pause};
    }

    // The Fn layer key must not be a real modifier: that would create two
    // different meanings for the same physical key.
    if (config.fn_layer_key == key::Invalid || is_modifier_key(config.fn_layer_key)) {
        config.fn_layer_key = key::Menu;
    }

    for (auto& profile : config.profiles) {
        profile.bindings.erase(
            std::remove_if(profile.bindings.begin(), profile.bindings.end(),
                [](const Binding& binding) { return !binding.valid(); }),
            profile.bindings.end());
        if (profile.name.empty()) {
            profile.name = "Default";
        }
    }
}

std::vector<std::string> validate(const AppConfig& config) {
    std::vector<std::string> problems;

    for (size_t i = 0; i < config.profiles.size(); ++i) {
        const auto& profile = config.profiles[i];
        std::unordered_set<uint32_t> seen;
        for (const auto& binding : profile.bindings) {
            const uint32_t key = (static_cast<uint32_t>(binding.chord.mods) << 16) | binding.chord.key;
            if (!seen.insert(key).second) {
                problems.push_back("Profile '" + profile.name + "' binds " +
                    format_chord(binding.chord) + " more than once");
            }
            if (binding.chord == config.panic_key) {
                problems.push_back("Profile '" + profile.name + "' binds the panic key " +
                    format_chord(config.panic_key));
            }
        }

        const bool has_movement = std::any_of(profile.bindings.begin(), profile.bindings.end(),
            [](const Binding& binding) {
                return binding.action == Action::MoveUp || binding.action == Action::MoveDown ||
                       binding.action == Action::MoveLeft || binding.action == Action::MoveRight;
            });
        if (!has_movement) {
            problems.push_back("Profile '" + profile.name + "' has no movement bindings");
        }
    }

    if (config.profiles.size() > 1) {
        std::unordered_set<std::string> names;
        for (const auto& profile : config.profiles) {
            if (!names.insert(profile.name).second) {
                problems.push_back("Duplicate profile name: " + profile.name);
            }
        }
    }

    return problems;
}

}  // namespace km
