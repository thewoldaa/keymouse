// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/dashboard.h"

#include <string>
#include <string_view>
#include <cstdio>

#include <imgui.h>

#include "ui/theme.h"

// The dashboard is assembled from the per-tab translation units. This file
// exists so the UI library has a single, stable entry point and so the shared
// widget helpers have one home rather than being duplicated per tab.
//
// Tab implementations live in src/ui/tabs/.

namespace km {
namespace ui {
namespace widgets {
namespace {

// English names used when a locale does not translate an action.
const char* action_fallback(Action action) {
    switch (action) {
        case Action::MoveUp: return "Move Up";
        case Action::MoveDown: return "Move Down";
        case Action::MoveLeft: return "Move Left";
        case Action::MoveRight: return "Move Right";
        case Action::ClickLeft: return "Left Click";
        case Action::ClickRight: return "Right Click";
        case Action::ClickMiddle: return "Middle Click";
        case Action::ClickX1: return "Back Button";
        case Action::ClickX2: return "Forward Button";
        case Action::DoubleClick: return "Double Click";
        case Action::DragLock: return "Drag Lock";
        case Action::ScrollUp: return "Scroll Up";
        case Action::ScrollDown: return "Scroll Down";
        case Action::ScrollLeft: return "Scroll Left";
        case Action::ScrollRight: return "Scroll Right";
        case Action::Precision: return "Precision Mode";
        case Action::Turbo: return "Turbo Mode";
        case Action::CursorCenter: return "Centre Cursor";
        case Action::ToggleEnabled: return "Toggle KeyMouse";
        case Action::OpenDashboard: return "Open Dashboard";
        case Action::SpeedUp: return "Speed Up";
        case Action::SpeedDown: return "Speed Down";
        case Action::None:
        case Action::Count:
            return "None";
    }
    return "None";
}

// Locale key for an action, for example "action.click_left".
std::string action_key(Action action) {
    std::string key = "action.";
    for (const char c : action_name(action)) {
        key += c == '-' ? '_' : c;
    }
    return key;
}

}  // namespace

void section(const char* title) {
    ImGui::Spacing();
    ImGui::SeparatorText(title);
    ImGui::Spacing();
}

void key_value(const char* key, const std::string& value) {
    ImGui::TextDisabled("%s", key);
    ImGui::SameLine(170.0f);
    ImGui::TextUnformatted(value.c_str());
}

void status_badge(bool active, const char* active_text, const char* inactive_text) {
    const ImU32 color = active ? colors::kAccent : colors::kMuted;
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(color));
    ImGui::TextUnformatted(active ? active_text : inactive_text);
    ImGui::PopStyleColor();
}

bool slider_row(I18n& i18n, const char* label, float* value, float min, float max,
                const char* format, float default_value) {
    ImGui::PushID(label);
    ImGui::TextUnformatted(label);
    ImGui::SameLine(200.0f);
    ImGui::SetNextItemWidth(220.0f);
    bool changed = ImGui::SliderFloat("##value", value, min, max, format);
    ImGui::SameLine();
    if (ImGui::SmallButton(i18n.tr("common.reset").data())) {
        *value = default_value;
        changed = true;
    }
    ImGui::PopID();
    return changed;
}

const char* action_label(I18n& i18n, Action action) {
    const std::string key = action_key(action);
    const std::string_view text = i18n.tr(key);
    // I18n::tr returns the key itself when nothing matches, which is the signal
    // that this locale has no translation for it. The returned view points into
    // the loaded table, so the pointer stays valid.
    return text == key ? action_fallback(action) : text.data();
}

const char* bind_mode_label(I18n& i18n, BindMode mode) {
    const auto lookup = [&i18n](std::string_view key, const char* fallback) {
        const std::string_view text = i18n.tr(key);
        return text == key ? fallback : text.data();
    };
    switch (mode) {
        case BindMode::Hold: return lookup("bindings.mode.hold", "Hold");
        case BindMode::Toggle: return lookup("bindings.mode.toggle", "Toggle");
        case BindMode::Tap: return lookup("bindings.mode.tap", "Tap");
    }
    return "Hold";
}

bool mask_has(uint32_t mask, Action action) {
    const auto bit = static_cast<unsigned>(action);
    return bit < 32 && (mask & (1u << bit)) != 0;
}

bool input_text(const char* label, std::string& value, ImGuiInputTextFlags flags) {
    return ImGui::InputText(label, value.data(), value.capacity() + 1,
                            flags | ImGuiInputTextFlags_CallbackResize,
                            [](ImGuiInputTextCallbackData* data) {
                                if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
                                    auto* text = static_cast<std::string*>(data->UserData);
                                    text->resize(static_cast<size_t>(data->BufTextLen));
                                    data->Buf = text->data();
                                }
                                return 0;
                            },
                            &value);
}

void format_duration(int64_t milliseconds, char* out, size_t out_size) {
    if (milliseconds < 0) {
        std::snprintf(out, out_size, "-");
        return;
    }
    const int64_t total_seconds = milliseconds / 1000;
    const int64_t hours = total_seconds / 3600;
    const int64_t minutes = (total_seconds / 60) % 60;
    const int64_t seconds = total_seconds % 60;
    if (hours > 0) {
        std::snprintf(out, out_size, "%lld:%02lld:%02lld",
                      static_cast<long long>(hours),
                      static_cast<long long>(minutes),
                      static_cast<long long>(seconds));
    } else {
        std::snprintf(out, out_size, "%lld:%02lld",
                      static_cast<long long>(total_seconds / 60),
                      static_cast<long long>(seconds));
    }
}

}  // namespace widgets
}  // namespace ui
}  // namespace km
