// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/keycodes.h"
#include "input/input_backend.h"

namespace km {

// One recorded or hand-edited macro step.
//
// Timing is stored as a delay from the previous step, exactly like the recorded
// stream, so editing one delay shifts everything after it without touching the
// other rows.
enum class MacroStepType : uint8_t {
    Move = 0,        // move the cursor to (x, y)
    ButtonDown = 1,  // press a mouse button
    ButtonUp = 2,    // release a mouse button
    Scroll = 3,      // vertical wheel step
    ScrollH = 4,     // horizontal wheel step
    KeyDown = 5,     // press a keyboard key
    KeyUp = 6,       // release a keyboard key
    Wait = 7,        // pure delay with no action
};

std::string_view macro_step_type_name(MacroStepType type);
MacroStepType macro_step_type_from_name(std::string_view name);

std::string_view macro_button_name(MouseButton button);
std::optional<MouseButton> macro_button_from_name(std::string_view name);

struct MacroStep {
    // Delay since the previous step, in milliseconds. The first step's delay is
    // relative to the moment playback starts.
    int64_t dt_ms = 0;

    MacroStepType type = MacroStepType::Wait;

    // Screen position for Move. Unused by the other step types.
    int32_t x = 0;
    int32_t y = 0;

    // Button id for ButtonDown/ButtonUp (MouseButton), or wheel delta for
    // Scroll/ScrollH.
    int32_t data = 0;

    // Physical key for KeyDown/KeyUp.
    KeyId key = key::Invalid;
};

struct Macro {
    std::string id;
    std::string name;

    // Screen the macro was recorded on. Zero means "unknown"; playback then
    // never scales.
    int32_t recorded_width = 0;
    int32_t recorded_height = 0;

    std::vector<MacroStep> steps;
};

// Sum of all step delays.
int64_t macro_duration_ms(const Macro& macro);

// True when the macro has no actionable step (Wait-only macros are useless but
// still valid to store).
bool macro_has_action(const Macro& macro);

// JSON document used by the .kmacro files. `id` is optional: the library stores
// it in the file name, but keeping it inside makes a shared file self-describing.
std::string macro_to_json(const Macro& macro);
std::optional<Macro> macro_from_json(std::string_view text, std::string& error);

// Sanitizes a macro name into a file-name-safe stem (ASCII letters, digits,
// dash, underscore). Empty input becomes "macro".
std::string macro_file_stem(std::string_view name);

inline constexpr size_t kMaxMacroSteps = 2000000;
inline constexpr int64_t kMaxMacroStepDelayMs = 24LL * 60 * 60 * 1000;  // one day
inline constexpr int32_t kMaxMacroCoordinate = 100000;

}  // namespace km
