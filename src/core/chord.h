// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "core/keycodes.h"

namespace km {

// Modifier set. Left and right variants collapse to the same bit: a user who
// binds "Ctrl + Numpad0" does not want the binding to stop working because
// they pressed the right-hand Ctrl. Per-side bindings are not a feature we
// need, and collapsing keeps the chord space small and predictable.
enum Modifier : uint8_t {
    ModNone = 0,
    ModCtrl = 1u << 0,
    ModAlt = 1u << 1,
    ModShift = 1u << 2,
    ModWin = 1u << 3,
    ModFn = 1u << 4,  // the user-selected Fn layer key
};

using ModMask = uint8_t;

inline constexpr ModMask kAllModifiers =
    ModCtrl | ModAlt | ModShift | ModWin | ModFn;

// Maximum number of modifiers in a chord. This is every modifier bit we
// define, which is the real limit: a chord cannot have more modifiers than
// exist.
inline constexpr int kMaxChordModifiers = 5;

// A key combination: zero or more modifiers plus exactly one base key.
//
// `key` is a canonical KeyId. A chord whose base key is itself a modifier is
// invalid; "Ctrl + Ctrl" is not a meaningful binding.
struct Chord {
    ModMask mods = ModNone;
    KeyId key = key::Invalid;

    bool valid() const {
        return key != key::Invalid && !key_is_modifier(key);
    }

    bool operator==(const Chord&) const = default;
};

// Formats a chord as "Ctrl + Alt + Numpad0", in a fixed modifier order:
// Ctrl, Alt, Shift, Win, Fn. Returns an empty string for invalid chords.
std::string format_chord(const Chord& chord);

// Formats just the modifiers, for example "Ctrl + Alt". Returns an empty
// string when no modifier is set. Used by the chord recorder, which shows the
// modifiers the user is holding before the base key is known.
std::string format_modifiers(ModMask mods);

// Parses "Ctrl+Alt+Numpad0" (whitespace around separators is ignored, the
// modifier names are case insensitive). Returns nullopt when the string does
// not describe a valid chord.
std::optional<Chord> parse_chord(std::string_view text);

// Returns the modifier mask bit for a modifier key, or 0 for non-modifiers.
// The Fn layer key is resolved by the caller before this is used, because it
// depends on user configuration.
ModMask modifier_bit(KeyId key);

// True when `key` is one of the physical keys that maps to a modifier bit.
bool is_modifier_key(KeyId key);

// Resolves the modifier bit for a pressed key, taking the configured Fn layer
// key into account.
ModMask modifier_bit_for(KeyId key, KeyId fn_layer_key);

}  // namespace km
