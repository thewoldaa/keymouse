// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/chord_capture.h"

#include <string>

#include <imgui.h>

#include "core/keycodes.h"

namespace km {
namespace ui {
namespace {

// Maps an ImGuiKey to a canonical KeyId. ImGui's key enum covers the keys we
// care about; anything unmapped is ignored so a stray key does not silently
// become a binding.
struct ImGuiKeyMapping {
    ImGuiKey imgui;
    KeyId key;
};

constexpr ImGuiKeyMapping kMappings[] = {
    {ImGuiKey_A, key::KeyA}, {ImGuiKey_B, key::KeyA + 1}, {ImGuiKey_C, key::KeyA + 2},
    {ImGuiKey_D, key::KeyA + 3}, {ImGuiKey_E, key::KeyA + 4}, {ImGuiKey_F, key::KeyA + 5},
    {ImGuiKey_G, key::KeyA + 6}, {ImGuiKey_H, key::KeyA + 7}, {ImGuiKey_I, key::KeyA + 8},
    {ImGuiKey_J, key::KeyA + 9}, {ImGuiKey_K, key::KeyA + 10}, {ImGuiKey_L, key::KeyA + 11},
    {ImGuiKey_M, key::KeyA + 12}, {ImGuiKey_N, key::KeyA + 13}, {ImGuiKey_O, key::KeyA + 14},
    {ImGuiKey_P, key::KeyA + 15}, {ImGuiKey_Q, key::KeyA + 16}, {ImGuiKey_R, key::KeyA + 17},
    {ImGuiKey_S, key::KeyA + 18}, {ImGuiKey_T, key::KeyA + 19}, {ImGuiKey_U, key::KeyA + 20},
    {ImGuiKey_V, key::KeyA + 21}, {ImGuiKey_W, key::KeyA + 22}, {ImGuiKey_X, key::KeyA + 23},
    {ImGuiKey_Y, key::KeyA + 24}, {ImGuiKey_Z, key::KeyA + 25},

    {ImGuiKey_1, key::Key1}, {ImGuiKey_2, key::Key1 + 1}, {ImGuiKey_3, key::Key1 + 2},
    {ImGuiKey_4, key::Key1 + 3}, {ImGuiKey_5, key::Key1 + 4}, {ImGuiKey_6, key::Key1 + 5},
    {ImGuiKey_7, key::Key1 + 6}, {ImGuiKey_8, key::Key1 + 7}, {ImGuiKey_9, key::Key1 + 8},
    {ImGuiKey_0, key::Key0},

    {ImGuiKey_Enter, key::Enter},
    {ImGuiKey_Escape, key::Escape},
    {ImGuiKey_Backspace, key::Backspace},
    {ImGuiKey_Tab, key::Tab},
    {ImGuiKey_Space, key::Space},
    {ImGuiKey_Minus, key::Minus},
    {ImGuiKey_Equal, key::Equal},
    {ImGuiKey_LeftBracket, key::BracketLeft},
    {ImGuiKey_RightBracket, key::BracketRight},
    {ImGuiKey_Backslash, key::Backslash},
    {ImGuiKey_Semicolon, key::Semicolon},
    {ImGuiKey_Apostrophe, key::Quote},
    {ImGuiKey_GraveAccent, key::Backquote},
    {ImGuiKey_Comma, key::Comma},
    {ImGuiKey_Period, key::Period},
    {ImGuiKey_Slash, key::Slash},
    {ImGuiKey_CapsLock, key::CapsLock},

    {ImGuiKey_F1, key::F1}, {ImGuiKey_F2, key::F1 + 1}, {ImGuiKey_F3, key::F1 + 2},
    {ImGuiKey_F4, key::F1 + 3}, {ImGuiKey_F5, key::F1 + 4}, {ImGuiKey_F6, key::F1 + 5},
    {ImGuiKey_F7, key::F1 + 6}, {ImGuiKey_F8, key::F1 + 7}, {ImGuiKey_F9, key::F1 + 8},
    {ImGuiKey_F10, key::F1 + 9}, {ImGuiKey_F11, key::F1 + 10}, {ImGuiKey_F12, key::F1 + 11},
    {ImGuiKey_F13, key::F13}, {ImGuiKey_F14, key::F13 + 1}, {ImGuiKey_F15, key::F13 + 2},
    {ImGuiKey_F16, key::F13 + 3}, {ImGuiKey_F17, key::F13 + 4}, {ImGuiKey_F18, key::F13 + 5},
    {ImGuiKey_F19, key::F13 + 6}, {ImGuiKey_F20, key::F13 + 7}, {ImGuiKey_F21, key::F13 + 8},
    {ImGuiKey_F22, key::F13 + 9}, {ImGuiKey_F23, key::F13 + 10}, {ImGuiKey_F24, key::F13 + 11},

    {ImGuiKey_PrintScreen, key::PrintScreen},
    {ImGuiKey_ScrollLock, key::ScrollLock},
    {ImGuiKey_Pause, key::Pause},
    {ImGuiKey_Insert, key::Insert},
    {ImGuiKey_Home, key::Home},
    {ImGuiKey_PageUp, key::PageUp},
    {ImGuiKey_Delete, key::Delete},
    {ImGuiKey_End, key::End},
    {ImGuiKey_PageDown, key::PageDown},
    {ImGuiKey_RightArrow, key::ArrowRight},
    {ImGuiKey_LeftArrow, key::ArrowLeft},
    {ImGuiKey_DownArrow, key::ArrowDown},
    {ImGuiKey_UpArrow, key::ArrowUp},

    {ImGuiKey_NumLock, key::NumLock},
    {ImGuiKey_KeypadDivide, key::NumpadDivide},
    {ImGuiKey_KeypadMultiply, key::NumpadMultiply},
    {ImGuiKey_KeypadSubtract, key::NumpadSubtract},
    {ImGuiKey_KeypadAdd, key::NumpadAdd},
    {ImGuiKey_KeypadEnter, key::NumpadEnter},
    {ImGuiKey_Keypad1, key::Numpad1}, {ImGuiKey_Keypad2, key::Numpad1 + 1},
    {ImGuiKey_Keypad3, key::Numpad1 + 2}, {ImGuiKey_Keypad4, key::Numpad1 + 3},
    {ImGuiKey_Keypad5, key::Numpad1 + 4}, {ImGuiKey_Keypad6, key::Numpad1 + 5},
    {ImGuiKey_Keypad7, key::Numpad1 + 6}, {ImGuiKey_Keypad8, key::Numpad1 + 7},
    {ImGuiKey_Keypad9, key::Numpad1 + 8}, {ImGuiKey_Keypad0, key::Numpad0},
    {ImGuiKey_KeypadDecimal, key::NumpadDecimal},

    {ImGuiKey_LeftCtrl, key::ControlLeft},
    {ImGuiKey_LeftShift, key::ShiftLeft},
    {ImGuiKey_LeftAlt, key::AltLeft},
    {ImGuiKey_LeftSuper, key::MetaLeft},
    {ImGuiKey_RightCtrl, key::ControlRight},
    {ImGuiKey_RightShift, key::ShiftRight},
    {ImGuiKey_RightAlt, key::AltRight},
    {ImGuiKey_RightSuper, key::MetaRight},
    {ImGuiKey_Menu, key::Menu},

    // Keys that exist in the canonical table but have no virtual-key mapping on
    // the Windows side (International keys, media keys, the Fn layer sentinel)
    // are deliberately absent here: a binding that can never fire is worse than
    // no binding at all.
};

ModMask mods_from_io(const ImGuiIO& io) {
    ModMask mods = ModNone;
    if (io.KeyCtrl) mods |= ModCtrl;
    if (io.KeyAlt) mods |= ModAlt;
    if (io.KeyShift) mods |= ModShift;
    if (io.KeySuper) mods |= ModWin;
    return mods;
}

// Storage keys for the recorder state, resolved against the widget's ID so
// every recorder on screen has its own.
struct CaptureState {
    int* open = nullptr;
    int* mods = nullptr;
    int* key = nullptr;
};

CaptureState state_for() {
    ImGuiStorage* storage = ImGui::GetStateStorage();
    // GetID() hashes the current ID stack, so these three are unique per
    // widget even though the literals repeat.
    CaptureState state;
    state.open = storage->GetIntRef(ImGui::GetID("##capture_open"), 0);
    state.mods = storage->GetIntRef(ImGui::GetID("##capture_mods"), 0);
    state.key = storage->GetIntRef(ImGui::GetID("##capture_key"), 0);
    return state;
}

}  // namespace

std::string chord_label(const Chord& chord, const I18n& tr) {
    if (!chord.valid()) {
        return std::string(tr.tr("capture.unset"));
    }
    return format_chord(chord);
}

ChordCaptureResult chord_capture(const char* id, const Chord& current, const I18n& tr) {
    ChordCaptureResult result;
    result.chord = current;

    ImGui::PushID(id);
    const CaptureState state = state_for();

    const bool open = *state.open != 0;
    const std::string label = open
        ? std::string(tr.tr("capture.button_prompt"))
        : chord_label(current, tr);

    if (open) {
        // The recorder stays highlighted while it is waiting, so it is obvious
        // which row is listening.
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    }
    if (ImGui::Button(label.c_str(), ImVec2(190.0f, 0.0f)) && !open) {
        *state.open = 1;
        *state.mods = 0;
        *state.key = key::Invalid;
        ImGui::OpenPopup("##capture");
    }
    if (open) {
        ImGui::PopStyleColor();
    }

    if (*state.open != 0) {
        ImGui::SetNextWindowSize(ImVec2(320.0f, 0.0f));
        if (ImGui::BeginPopupModal("##capture", nullptr,
                                   ImGuiWindowFlags_NoTitleBar |
                                   ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoSavedSettings)) {
            result.state = ChordCaptureResult::State::Capturing;

            ImGui::TextUnformatted(tr.tr("capture.instruction").data());
            ImGui::TextDisabled("%s", tr.tr("capture.escape_hint").data());
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            const Chord pending{static_cast<ModMask>(*state.mods),
                                static_cast<KeyId>(*state.key)};
            std::string preview = format_chord(pending);
            if (preview.empty()) {
                // Only modifiers so far: show them, so the user can see the
                // recorder is picking them up.
                preview = format_modifiers(static_cast<ModMask>(*state.mods));
            }
            if (preview.empty()) {
                preview = tr.tr("capture.waiting");
            }
            ImGui::TextUnformatted(preview.c_str());

            ImGui::Spacing();
            if (ImGui::Button(tr.tr("common.cancel").data(), ImVec2(90.0f, 0.0f))) {
                *state.open = 0;
                result.state = ChordCaptureResult::State::Cancelled;
                ImGui::CloseCurrentPopup();
            }

            // Read key state directly. Checking every mapped key is cheap and
            // avoids depending on the input queue's format.
            for (const auto& mapping : kMappings) {
                if (!ImGui::IsKeyPressed(mapping.imgui, false)) {
                    continue;
                }

                const ModMask mods = mods_from_io(ImGui::GetIO());

                if (mapping.imgui == ImGuiKey_Escape) {
                    *state.open = 0;
                    result.state = ChordCaptureResult::State::Cancelled;
                    ImGui::CloseCurrentPopup();
                    break;
                }

                if (key_is_modifier(mapping.key)) {
                    // A modifier on its own is not a chord. Remember it and
                    // keep waiting for the base key.
                    *state.mods = mods;
                    continue;
                }

                *state.mods = mods;
                *state.key = mapping.key;

                const Chord captured{mods, mapping.key};
                if (captured.valid()) {
                    *state.open = 0;
                    result.state = ChordCaptureResult::State::Accepted;
                    result.chord = captured;
                    ImGui::CloseCurrentPopup();
                }
                break;
            }

            ImGui::EndPopup();
        } else if (*state.open != 0) {
            // The popup went away without this function closing it (Escape is
            // handled by ImGui for modal popups). Treat that as a cancel so the
            // button does not stay stuck in "press a key" forever.
            *state.open = 0;
            result.state = ChordCaptureResult::State::Cancelled;
        }
    }

    ImGui::PopID();
    return result;
}

}  // namespace ui
}  // namespace km
