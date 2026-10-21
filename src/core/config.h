// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/action.h"
#include "core/autoclick.h"
#include "core/binding.h"
#include "core/motion.h"

namespace km {

inline constexpr int kConfigSchemaVersion = 3;

// A named set of bindings, optionally activated automatically when a
// particular executable is in the foreground.
struct Profile {
    std::string name = "Default";
    // Executable name to match, e.g. "game.exe". Empty means manual only.
    std::string match_exe;
    std::vector<Binding> bindings;
};

// A second binding table that only works while mouse mode is on.
//
// This is the "turn the keyboard into a mouse" layer: the keys here are meant
// to be plain keys (no Ctrl, Alt, or Fn), so the hand rests on one cluster and
// every mouse action is one press. While the mode is off these keys type
// normally and are never swallowed.
struct MouseModeConfig {
    // Whether the mode comes up enabled when KeyMouse starts.
    bool start_enabled = false;
    // Toggle: press the key once to enter the mode, again to leave it.
    // Hold: the mode is only active while the key is held.
    bool hold_to_activate = false;
    // Short beep when the mode changes, so a hidden window is not the only
    // feedback.
    bool beep = true;
    Chord toggle_key{ModCtrl | ModAlt, key::KeyM};
    std::vector<Binding> bindings;
};

struct AutoClickConfig {
    // Master switch. Individual tasks also have their own enabled flag.
    bool enabled = false;
    // Always-active chord that turns every task on or off, like the panic key.
    Chord toggle_key{ModCtrl | ModAlt, key::KeyT};
    std::vector<AutoClickTask> tasks;
};

// One macro in the library. The id is also the .kmacro file stem, so it is kept
// to characters that are safe in a file name; the display name lives inside the
// file and in `name`.
struct MacroEntry {
    std::string id;
    std::string name;
    // Global playback hotkey. An invalid key means "no hotkey".
    Chord hotkey{ModNone, key::Invalid};
};

struct MacroConfig {
    std::vector<MacroEntry> entries;

    // Capture keyboard events while recording. Off by default (spec 16.2):
    // most recordings are mouse-only.
    bool record_keyboard = false;
    // Points closer than this are dropped from recorded movement. 0.5 to 16 px.
    double move_tolerance_px = 3.0;

    // Always-active chords handled on the hook thread, like the mouse-mode
    // toggle: record, play/pause, emergency stop.
    Chord record_key{ModCtrl | ModAlt, key::KeyR};
    Chord play_key{ModCtrl | ModAlt, key::KeyP};
    Chord stop_key{ModCtrl | ModAlt, key::KeyS};
};

inline constexpr double kMinMacroTolerancePx = 0.5;
inline constexpr double kMaxMacroTolerancePx = 16.0;

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

    // Mouse mode and scheduled clicking
    MouseModeConfig mouse_mode;
    AutoClickConfig auto_click;

    // Macro library metadata. The steps themselves live in .kmacro files.
    MacroConfig macros;

    // Profiles
    std::vector<Profile> profiles{Profile{}};
    size_t active_profile = 0;
    bool auto_switch_profiles = true;

    // Returns the active profile, or the first one if the index is stale.
    Profile& active();
    const Profile& active() const;

    // True when `chord` is used by any profile binding, by a mouse-mode
    // binding, or by a macro hotkey. Used to keep the panic key and the toggle
    // keys free. `ignore_profile` skips one profile; `ignore_macro_id` skips
    // one macro, so a macro's own hotkey does not count as a conflict with
    // itself.
    bool chord_in_use(const Chord& chord, const std::string& ignore_profile = {},
                      const std::string& ignore_macro_id = {}) const;

    // True when the chord is a mode control key (panic, mouse-mode toggle,
    // auto-click toggle, macro record/play/stop). Those are handled by the
    // engine outside the binding tables and must not be bound to an action.
    bool chord_is_reserved(const Chord& chord) const;

    // Identifies which control key a picker owns, so its own current value is
    // skipped when the chord is compared against the other control keys.
    // Comparing by value cannot express "every slot except this one": the
    // chord being checked is usually that slot's own value.
    enum class ControlKey {
        Panic,
        MouseToggle,
        AutoClickToggle,
        MacroRecord,
        MacroPlay,
        MacroStop,
    };

    // True when the chord collides with a control key other than `own`. A
    // control-key picker must compare against the *other* control keys only;
    // chord_is_reserved() would always report a collision with itself.
    bool chord_is_reserved_except(const Chord& chord, ControlKey own) const;
};

// Default configuration used on first run: the "desktop" preset.
AppConfig make_default_config();

// Presets selectable during onboarding.
enum class Preset { Desktop, Numpad, Game };
AppConfig make_preset(Preset preset);

// The plain-key mouse-mode table. Shared by every preset; it is a separate
// concept from the always-on chord bindings.
std::vector<Binding> make_mouse_mode_bindings();

// Serialization. to_json produces pretty-printed, stable key order.
std::string to_json(const AppConfig& config);

// Profile exchange format used by the Profiles tab. Wraps one or more
// profiles with a format tag so importing an unrelated JSON file fails with a
// clear error instead of silently producing an empty profile.
std::string profiles_to_json(const std::vector<Profile>& profiles);
std::optional<std::vector<Profile>> profiles_from_json(std::string_view text,
                                                       std::string& error);

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
