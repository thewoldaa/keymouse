// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/config.h"

#include <algorithm>
#include <set>
#include <unordered_set>

#include <nlohmann/json.hpp>

#include "core/macro.h"

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

json bindings_to_json(const std::vector<Binding>& bindings) {
    json out = json::array();
    for (const auto& binding : bindings) {
        out.push_back(binding_to_json(binding));
    }
    return out;
}

std::vector<Binding> bindings_from_json(const json& value) {
    std::vector<Binding> out;
    if (value.is_array()) {
        for (const auto& item : value) {
            if (item.is_object()) {
                out.push_back(binding_from_json(item));
            }
        }
    }
    return out;
}

json profile_to_json(const Profile& profile) {
    return json{
        {"name", profile.name},
        {"match_exe", profile.match_exe},
        {"bindings", bindings_to_json(profile.bindings)},
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
    if (value.contains("bindings")) {
        profile.bindings = bindings_from_json(value["bindings"]);
    }
    return profile;
}

json motion_to_json(const MotionConfig& motion) {
    return json{
        {"speed_px_s", motion.speed_px_s},
        {"accel_ms", motion.accel_ms},
        {"accel_max", motion.accel_max},
        {"accel_curve", motion.accel_curve},
        {"precision_mult", motion.precision_mult},
        {"turbo_mult", motion.turbo_mult},
        {"diagonal_normalize", motion.diagonal_normalize},
        {"method", motion_method_name(motion.method)},
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
    read("accel_max", motion.accel_max);
    read("accel_curve", motion.accel_curve);
    read("precision_mult", motion.precision_mult);
    read("turbo_mult", motion.turbo_mult);
    if (value.contains("diagonal_normalize") && value["diagonal_normalize"].is_boolean()) {
        motion.diagonal_normalize = value["diagonal_normalize"].get<bool>();
    }
    if (value.contains("method") && value["method"].is_string()) {
        motion.method = motion_method_from_name(value["method"].get<std::string>());
    }
    return motion;
}

json mouse_mode_to_json(const MouseModeConfig& mouse_mode) {
    return json{
        {"start_enabled", mouse_mode.start_enabled},
        {"hold_to_activate", mouse_mode.hold_to_activate},
        {"beep", mouse_mode.beep},
        {"toggle_key", chord_to_json(mouse_mode.toggle_key)},
        {"bindings", bindings_to_json(mouse_mode.bindings)},
    };
}

MouseModeConfig mouse_mode_from_json(const json& value) {
    MouseModeConfig out;
    auto read_bool = [&value](const char* key, bool& target) {
        if (value.contains(key) && value[key].is_boolean()) {
            target = value[key].get<bool>();
        }
    };
    read_bool("start_enabled", out.start_enabled);
    read_bool("hold_to_activate", out.hold_to_activate);
    read_bool("beep", out.beep);
    if (value.contains("toggle_key") && value["toggle_key"].is_object()) {
        out.toggle_key = chord_from_json(value["toggle_key"]);
    }
    if (value.contains("bindings")) {
        out.bindings = bindings_from_json(value["bindings"]);
    }
    return out;
}

json auto_click_task_to_json(const AutoClickTask& task) {
    return json{
        {"id", task.id},
        {"name", task.name},
        {"enabled", task.enabled},
        {"action", std::string(auto_click_action_name(task.action))},
        {"interval_seconds", task.interval_seconds},
        {"start_delay_seconds", task.start_delay_seconds},
        {"repeat_count", task.repeat_count},
        {"run_missed_once", task.run_missed_once},
        {"only_if_app", task.only_if_app},
    };
}

AutoClickTask auto_click_task_from_json(const json& value) {
    AutoClickTask task;
    auto read_string = [&value](const char* key, std::string& out) {
        if (value.contains(key) && value[key].is_string()) {
            out = value[key].get<std::string>();
        }
    };
    auto read_bool = [&value](const char* key, bool& out) {
        if (value.contains(key) && value[key].is_boolean()) {
            out = value[key].get<bool>();
        }
    };
    auto read_number = [&value](const char* key, double& out) {
        if (value.contains(key) && value[key].is_number()) {
            out = value[key].get<double>();
        }
    };
    read_string("id", task.id);
    read_string("name", task.name);
    read_bool("enabled", task.enabled);
    read_bool("run_missed_once", task.run_missed_once);
    read_string("only_if_app", task.only_if_app);
    read_number("interval_seconds", task.interval_seconds);
    read_number("start_delay_seconds", task.start_delay_seconds);
    if (value.contains("repeat_count") && value["repeat_count"].is_number_integer()) {
        task.repeat_count = value["repeat_count"].get<int>();
    }
    if (value.contains("action") && value["action"].is_string()) {
        task.action = auto_click_action_from_name(value["action"].get<std::string>());
    }
    return task;
}

json auto_click_to_json(const AutoClickConfig& auto_click) {
    json tasks = json::array();
    for (const auto& task : auto_click.tasks) {
        tasks.push_back(auto_click_task_to_json(task));
    }
    return json{
        {"enabled", auto_click.enabled},
        {"toggle_key", chord_to_json(auto_click.toggle_key)},
        {"tasks", tasks},
    };
}

AutoClickConfig auto_click_from_json(const json& value) {
    AutoClickConfig out;
    if (value.contains("enabled") && value["enabled"].is_boolean()) {
        out.enabled = value["enabled"].get<bool>();
    }
    if (value.contains("toggle_key") && value["toggle_key"].is_object()) {
        out.toggle_key = chord_from_json(value["toggle_key"]);
    }
    if (value.contains("tasks") && value["tasks"].is_array()) {
        for (const auto& item : value["tasks"]) {
            if (item.is_object()) {
                out.tasks.push_back(auto_click_task_from_json(item));
            }
        }
    }
    return out;
}

json macro_entry_to_json(const MacroEntry& entry) {
    json item{
        {"id", entry.id},
        {"name", entry.name},
    };
    if (entry.hotkey.valid()) {
        item["hotkey"] = chord_to_json(entry.hotkey);
    }
    return item;
}

MacroEntry macro_entry_from_json(const json& value) {
    MacroEntry entry;
    if (value.contains("id") && value["id"].is_string()) {
        entry.id = value["id"].get<std::string>();
    }
    if (value.contains("name") && value["name"].is_string()) {
        entry.name = value["name"].get<std::string>();
    }
    if (value.contains("hotkey") && value["hotkey"].is_object()) {
        entry.hotkey = chord_from_json(value["hotkey"]);
    }
    return entry;
}

json macro_config_to_json(const MacroConfig& macros) {
    json entries = json::array();
    for (const auto& entry : macros.entries) {
        entries.push_back(macro_entry_to_json(entry));
    }
    return json{
        {"entries", entries},
        {"record_keyboard", macros.record_keyboard},
        {"move_tolerance_px", macros.move_tolerance_px},
        {"record_key", chord_to_json(macros.record_key)},
        {"play_key", chord_to_json(macros.play_key)},
        {"stop_key", chord_to_json(macros.stop_key)},
    };
}

MacroConfig macro_config_from_json(const json& value) {
    MacroConfig out;
    if (value.contains("record_keyboard") && value["record_keyboard"].is_boolean()) {
        out.record_keyboard = value["record_keyboard"].get<bool>();
    }
    if (value.contains("move_tolerance_px") && value["move_tolerance_px"].is_number()) {
        out.move_tolerance_px = value["move_tolerance_px"].get<double>();
    }
    if (value.contains("record_key") && value["record_key"].is_object()) {
        out.record_key = chord_from_json(value["record_key"]);
    }
    if (value.contains("play_key") && value["play_key"].is_object()) {
        out.play_key = chord_from_json(value["play_key"]);
    }
    if (value.contains("stop_key") && value["stop_key"].is_object()) {
        out.stop_key = chord_from_json(value["stop_key"]);
    }
    if (value.contains("entries") && value["entries"].is_array()) {
        for (const auto& item : value["entries"]) {
            if (item.is_object()) {
                out.entries.push_back(macro_entry_from_json(item));
            }
        }
    }
    return out;
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

bool is_movement(Action action) {
    return action == Action::MoveUp || action == Action::MoveDown ||
           action == Action::MoveLeft || action == Action::MoveRight;
}

// Adds the mouse-mode table to a preset. The plain keys are the point: the
// user turns the mode on and every mouse action is a single press under one
// hand, with no modifiers to hold.
void add_mouse_mode_defaults(AppConfig& config) {
    config.mouse_mode.bindings = make_mouse_mode_bindings();
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
    for (const auto& binding : mouse_mode.bindings) {
        if (binding.chord == chord) {
            return true;
        }
    }
    return false;
}

bool AppConfig::chord_is_reserved(const Chord& chord) const {
    return chord == panic_key || chord == mouse_mode.toggle_key ||
           chord == auto_click.toggle_key || chord == macros.record_key ||
           chord == macros.play_key || chord == macros.stop_key;
}

bool AppConfig::chord_is_reserved_except(const Chord& chord,
                                         const Chord& ignore) const {
    const auto collides = [&chord, &ignore](const Chord& other) {
        return other != ignore && chord == other;
    };
    return collides(panic_key) || collides(mouse_mode.toggle_key) ||
           collides(auto_click.toggle_key) || collides(macros.record_key) ||
           collides(macros.play_key) || collides(macros.stop_key);
}

std::vector<Binding> make_mouse_mode_bindings() {
    return {
        make_binding(Action::MoveUp, ModNone, key::KeyI),
        make_binding(Action::MoveDown, ModNone, key::KeyK),
        make_binding(Action::MoveLeft, ModNone, key::KeyJ),
        make_binding(Action::MoveRight, ModNone, key::KeyL),
        make_binding(Action::ClickLeft, ModNone, key::KeyU, BindMode::Tap),
        make_binding(Action::ClickRight, ModNone, key::KeyO, BindMode::Tap),
        make_binding(Action::ClickMiddle, ModNone, key::KeyP, BindMode::Tap),
        make_binding(Action::ScrollUp, ModNone, key::KeyY),
        make_binding(Action::ScrollDown, ModNone, key::KeyH),
        make_binding(Action::Precision, ModNone, key::KeyF),
        make_binding(Action::Turbo, ModNone, key::KeyG),
        make_binding(Action::DragLock, ModNone, key::Space, BindMode::Toggle),
    };
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
            config.motion.accel_ms = 300.0;
            break;
    }

    config.profiles.push_back(std::move(profile));
    add_mouse_mode_defaults(config);
    return config;
}

std::string to_json(const AppConfig& config) {
    json profiles = json::array();
    for (const auto& profile : config.profiles) {
        profiles.push_back(profile_to_json(profile));
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
        {"mouse_mode", mouse_mode_to_json(config.mouse_mode)},
        {"auto_click", auto_click_to_json(config.auto_click)},
        {"macros", macro_config_to_json(config.macros)},
        {"active_profile", config.active_profile},
        {"auto_switch_profiles", config.auto_switch_profiles},
        {"profiles", profiles},
    };
    return root.dump(2);
}

std::string profiles_to_json(const std::vector<Profile>& profiles) {
    json items = json::array();
    for (const auto& profile : profiles) {
        items.push_back(profile_to_json(profile));
    }
    const json root{
        {"format", "keymouse-profiles"},
        {"version", 1},
        {"profiles", items},
    };
    return root.dump(2);
}

std::optional<std::vector<Profile>> profiles_from_json(std::string_view text,
                                                       std::string& error) {
    json root;
    try {
        root = json::parse(text.begin(), text.end());
    } catch (const std::exception& ex) {
        error = std::string("JSON parse error: ") + ex.what();
        return std::nullopt;
    }

    if (!root.is_object() || !root.contains("format") ||
        root["format"] != "keymouse-profiles") {
        error = "This is not a KeyMouse profile file";
        return std::nullopt;
    }
    if (!root.contains("profiles") || !root["profiles"].is_array()) {
        error = "The profile file has no profiles array";
        return std::nullopt;
    }

    std::vector<Profile> profiles;
    for (const auto& item : root["profiles"]) {
        if (!item.is_object()) {
            error = "A profile entry is not an object";
            return std::nullopt;
        }
        Profile profile = profile_from_json(item);
        profile.bindings.erase(
            std::remove_if(profile.bindings.begin(), profile.bindings.end(),
                [](const Binding& binding) { return !binding.valid(); }),
            profile.bindings.end());
        if (profile.bindings.empty()) {
            error = "Profile '" + profile.name + "' has no valid bindings";
            return std::nullopt;
        }
        profiles.push_back(std::move(profile));
    }
    if (profiles.empty()) {
        error = "The profile file contains no profiles";
        return std::nullopt;
    }
    return profiles;
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
    const int loaded_schema = config.schema;

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

        // Schema 1 had a single exponential "time to reach speed" and no
        // maximum multiplier. Folding those configs into a constant speed keeps
        // the feel they were tuned for; new installs get the ramp.
        const bool has_accel_max = root["motion"].contains("accel_max");
        if (!has_accel_max && loaded_schema < 2) {
            config.motion.accel_max = 1.0;
            config.motion.accel_curve = 1.0;
        }
    }

    if (root.contains("scroll_lines_per_second") && root["scroll_lines_per_second"].is_number()) {
        config.scroll_lines_per_second = root["scroll_lines_per_second"].get<double>();
    }

    if (root.contains("mouse_mode") && root["mouse_mode"].is_object()) {
        config.mouse_mode = mouse_mode_from_json(root["mouse_mode"]);
        if (config.mouse_mode.bindings.empty() && loaded_schema < 2) {
            // A schema 1 file has no mouse mode at all. Seed the default table
            // so the feature is usable right after upgrading.
            config.mouse_mode.bindings = make_mouse_mode_bindings();
        }
    }

    if (root.contains("auto_click") && root["auto_click"].is_object()) {
        config.auto_click = auto_click_from_json(root["auto_click"]);
    }

    if (root.contains("macros") && root["macros"].is_object()) {
        config.macros = macro_config_from_json(root["macros"]);
        if (config.macros.entries.empty() && loaded_schema < 3) {
            // Schema 2 and older had no macro library; nothing to migrate. The
            // empty list is the correct starting point.
            config.macros.entries.clear();
        }
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
    config.motion.accel_max = std::clamp(config.motion.accel_max,
        MotionConfig::kMinAccelMax, MotionConfig::kMaxAccelMax);
    config.motion.accel_curve = std::clamp(config.motion.accel_curve,
        MotionConfig::kMinCurve, MotionConfig::kMaxCurve);
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

    if (!config.mouse_mode.toggle_key.valid()) {
        config.mouse_mode.toggle_key = Chord{ModCtrl | ModAlt, key::KeyM};
    }
    if (config.mouse_mode.bindings.empty()) {
        config.mouse_mode.bindings = make_mouse_mode_bindings();
    }
    config.mouse_mode.bindings.erase(
        std::remove_if(config.mouse_mode.bindings.begin(),
                       config.mouse_mode.bindings.end(),
                       [](const Binding& binding) { return !binding.valid(); }),
        config.mouse_mode.bindings.end());

    if (!config.auto_click.toggle_key.valid()) {
        config.auto_click.toggle_key = Chord{ModCtrl | ModAlt, key::KeyT};
    }
    for (size_t i = 0; i < config.auto_click.tasks.size(); ++i) {
        AutoClickTask& task = config.auto_click.tasks[i];
        if (task.id.empty()) {
            task.id = "task-" + std::to_string(i + 1);
        }
        if (task.name.empty()) {
            task.name = "Task " + std::to_string(i + 1);
        }
        task.interval_seconds = std::clamp(task.interval_seconds,
            kMinAutoClickIntervalSeconds, kMaxAutoClickIntervalSeconds);
        task.start_delay_seconds = std::clamp(task.start_delay_seconds,
            0.0, kMaxAutoClickDelaySeconds);
        task.repeat_count = std::clamp(task.repeat_count, 0, kMaxAutoClickRepeats);
    }

    config.macros.move_tolerance_px = std::clamp(config.macros.move_tolerance_px,
        kMinMacroTolerancePx, kMaxMacroTolerancePx);
    if (!config.macros.record_key.valid()) {
        config.macros.record_key = Chord{ModCtrl | ModAlt, key::KeyR};
    }
    if (!config.macros.play_key.valid()) {
        config.macros.play_key = Chord{ModCtrl | ModAlt, key::KeyP};
    }
    if (!config.macros.stop_key.valid()) {
        config.macros.stop_key = Chord{ModCtrl | ModAlt, key::KeyS};
    }
    {
        // Ids double as file names, so they are normalized here rather than at
        // every call site. Duplicates get a numeric suffix.
        std::unordered_set<std::string> macro_ids;
        std::vector<MacroEntry> entries;
        entries.reserve(config.macros.entries.size());
        for (auto& entry : config.macros.entries) {
            const std::string base = macro_file_stem(
                entry.id.empty() ? entry.name : entry.id);
            std::string id = base;
            for (int suffix = 2; !macro_ids.insert(id).second; ++suffix) {
                id = base + '-' + std::to_string(suffix);
            }
            entry.id = id;
            if (entry.name.empty()) {
                entry.name = entry.id;
            }
            if (!entry.hotkey.valid()) {
                entry.hotkey = Chord{ModNone, key::Invalid};
            }
            entries.push_back(std::move(entry));
        }
        config.macros.entries = std::move(entries);
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
            if (binding.chord == config.mouse_mode.toggle_key) {
                problems.push_back("Profile '" + profile.name + "' binds the mouse-mode key " +
                    format_chord(config.mouse_mode.toggle_key));
            }
            if (binding.chord == config.auto_click.toggle_key) {
                problems.push_back("Profile '" + profile.name + "' binds the auto-click key " +
                    format_chord(config.auto_click.toggle_key));
            }
            if (binding.chord == config.macros.record_key ||
                binding.chord == config.macros.play_key ||
                binding.chord == config.macros.stop_key) {
                problems.push_back("Profile '" + profile.name + "' binds the macro key " +
                    format_chord(binding.chord));
            }
        }

        const bool has_movement = std::any_of(profile.bindings.begin(), profile.bindings.end(),
            [](const Binding& binding) { return is_movement(binding.action); });
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

    // Mouse mode
    std::unordered_set<uint32_t> mouse_seen;
    for (const auto& binding : config.mouse_mode.bindings) {
        const uint32_t key = (static_cast<uint32_t>(binding.chord.mods) << 16) | binding.chord.key;
        if (!mouse_seen.insert(key).second) {
            problems.push_back("Mouse mode binds " + format_chord(binding.chord) +
                " more than once");
        }
        if (binding.chord == config.panic_key) {
            problems.push_back("Mouse mode binds the panic key " +
                format_chord(config.panic_key));
        }
        if (binding.chord == config.mouse_mode.toggle_key) {
            problems.push_back("Mouse mode binds its own toggle key " +
                format_chord(config.mouse_mode.toggle_key));
        }
        if (binding.chord == config.auto_click.toggle_key) {
            problems.push_back("Mouse mode binds the auto-click key " +
                format_chord(config.auto_click.toggle_key));
        }
        if (binding.chord == config.macros.record_key ||
            binding.chord == config.macros.play_key ||
            binding.chord == config.macros.stop_key) {
            problems.push_back("Mouse mode binds the macro key " +
                format_chord(binding.chord));
        }
    }
    const bool mouse_has_movement = std::any_of(
        config.mouse_mode.bindings.begin(), config.mouse_mode.bindings.end(),
        [](const Binding& binding) { return is_movement(binding.action); });
    if (config.mouse_mode.start_enabled && !mouse_has_movement) {
        problems.push_back("Mouse mode starts enabled but has no movement bindings");
    }

    // Mode control keys must be distinct, or pressing one would do two things.
    if (config.mouse_mode.toggle_key == config.auto_click.toggle_key) {
        problems.push_back("The mouse-mode key and the auto-click key are the same");
    }
    if (config.mouse_mode.toggle_key == config.panic_key) {
        problems.push_back("The mouse-mode key is also the panic key");
    }
    if (config.auto_click.toggle_key == config.panic_key) {
        problems.push_back("The auto-click key is also the panic key");
    }

    // Macro control keys must be distinct from each other and from every other
    // always-active key.
    const Chord macro_keys[] = {config.macros.record_key, config.macros.play_key,
                                config.macros.stop_key};
    for (size_t i = 0; i < 3; ++i) {
        for (size_t j = i + 1; j < 3; ++j) {
            if (macro_keys[i] == macro_keys[j]) {
                problems.push_back("The macro record, play, and stop keys must be different");
                i = 3;
                break;
            }
        }
    }
    for (const Chord& chord : macro_keys) {
        if (chord == config.panic_key || chord == config.mouse_mode.toggle_key ||
            chord == config.auto_click.toggle_key) {
            problems.push_back("A macro key is already used by another mode key: " +
                format_chord(chord));
            break;
        }
    }

    // Auto-click tasks
    std::unordered_set<std::string> task_ids;
    std::unordered_set<std::string> task_names;
    for (const auto& task : config.auto_click.tasks) {
        if (!task_ids.insert(task.id).second) {
            problems.push_back("Auto-click task id '" + task.id + "' is used more than once");
        }
        if (!task_names.insert(task.name).second) {
            problems.push_back("Auto-click task name '" + task.name + "' is used more than once");
        }
        if (task.interval_seconds < kMinAutoClickIntervalSeconds) {
            problems.push_back("Auto-click task '" + task.name +
                "' has an interval below one second");
        }
    }

    // Macro library
    std::unordered_set<std::string> macro_ids;
    std::vector<Chord> macro_hotkeys;
    for (const auto& entry : config.macros.entries) {
        if (!macro_ids.insert(entry.id).second) {
            problems.push_back("Macro id '" + entry.id + "' is used more than once");
        }
        if (!entry.hotkey.valid()) {
            continue;
        }
        for (const Chord& other : macro_hotkeys) {
            if (other == entry.hotkey) {
                problems.push_back("Macro '" + entry.name + "' reuses the hotkey " +
                    format_chord(entry.hotkey));
                break;
            }
        }
        macro_hotkeys.push_back(entry.hotkey);
        if (config.chord_is_reserved(entry.hotkey)) {
            problems.push_back("Macro '" + entry.name + "' uses the reserved key " +
                format_chord(entry.hotkey));
        } else if (config.chord_in_use(entry.hotkey)) {
            problems.push_back("Macro '" + entry.name + "' hotkey " +
                format_chord(entry.hotkey) + " is already bound");
        }
    }

    return problems;
}

}  // namespace km
