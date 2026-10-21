// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

#include <imgui.h>

#include "core/chord.h"
#include "core/i18n.h"

namespace km {
namespace ui {

// Result of one frame of a chord recorder.
struct ChordCaptureResult {
    enum class State {
        Idle,       // nothing happened this frame
        Capturing,  // this recorder is open and waiting for input
        Accepted,   // a chord was recorded this frame
        Cancelled,  // the recorder was closed without recording
    };

    State state = State::Idle;
    Chord chord;
};

// Draws a button that turns into a chord recorder when clicked.
//
// The recorder reads ImGui's key state rather than a text field, so the user
// can press Ctrl + Alt + Numpad0 and see it rendered exactly as it appears in
// the bindings table. Escape cancels, which is also how the user backs out of a
// binding they cannot complete.
//
// The recorder's state lives in ImGui's per-window storage, keyed by `id` and
// the current ID stack. The caller therefore does not have to keep a widget
// object alive between frames; calling this once per row is correct.
//
// While a recorder is open the caller must put the engine in capture mode
// (Engine::set_capture_mode), otherwise a key that is already bound is
// swallowed by that binding before the recorder can see it.
//
// `id` must be unique within the window. `tr` supplies the button and
// instruction text.
//
// `fn_layer_key` is the physical key configured as the Fn layer. Passing it
// lets the user record "Fn + X" chords: the key is treated like a modifier
// instead of being accepted as the base key. Pass key::Invalid for pickers
// where the Fn layer would be meaningless (such as the Fn layer picker itself).
ChordCaptureResult chord_capture(const char* id, const Chord& current, const I18n& tr,
                                 KeyId fn_layer_key = key::Invalid);

// True when `key` can be picked as the Fn layer key: a non-modifier key that
// the chord recorder can actually recognize. Key pickers must use this so the
// user cannot choose a key that no recorder will ever see (media keys, CJK
// keys, Escape, which cancels the recorder).
bool is_capturable_fn_key(KeyId key);

// Draws a button that turns into a single-key recorder. Unlike chord_capture()
// this accepts modifier keys, which is what the macro step editor needs: a
// KeyDown step for "Shift" is a real physical key, not a chord base.
//
// Escape cancels. `current` may be key::Invalid, which renders as "(unset)".
struct KeyCaptureResult {
    ChordCaptureResult::State state = ChordCaptureResult::State::Idle;
    KeyId key = key::Invalid;
};
KeyCaptureResult key_capture(const char* id, KeyId current, const I18n& tr);

// Renders a chord as a readable label. Returns the translated "(unset)" text
// for invalid chords so the UI never shows an empty cell.
std::string chord_label(const Chord& chord, const I18n& tr);

}  // namespace ui
}  // namespace km
