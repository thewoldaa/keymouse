// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/dashboard.h"

#include <string>

#include <imgui.h>

#include "core/keycodes.h"
#include "ui/theme.h"
#include "ui/widgets/chord_capture.h"

namespace km {
namespace ui {
namespace {

bool mouse_conflict(const MouseModeConfig& mouse_mode, const Chord& chord,
                    const size_t ignore_index) {
    for (size_t i = 0; i < mouse_mode.bindings.size(); ++i) {
        if (i != ignore_index && mouse_mode.bindings[i].chord == chord) {
            return true;
        }
    }
    return false;
}

}  // namespace

void draw_mouse_mode_tab(DashboardContext& context) {
    AppConfig& config = *context.config;
    MouseModeConfig& mouse = config.mouse_mode;
    I18n& tr = *context.i18n;
    bool changed = false;
    bool capturing = false;

    ImGui::TextDisabled("%s", tr.tr("mouse.hint").data());
    ImGui::Spacing();

    // The big switch, so entering and leaving the mode never requires finding a
    // checkbox among settings.
    widgets::status_badge(context.mouse_mode,
        tr.tr("mouse.on").data(), tr.tr("mouse.off").data());

    ImGui::Spacing();
    const char* button_label = context.mouse_mode
        ? tr.tr("mouse.turn_off").data()
        : tr.tr("mouse.turn_on").data();
    if (ImGui::Button(button_label, ImVec2(220.0f, 0.0f))) {
        if (context.engine) {
            context.engine->set_mouse_mode(!context.mouse_mode);
        }
    }

    ImGui::SameLine();
    bool start_enabled = mouse.start_enabled;
    if (ImGui::Checkbox(tr.tr("mouse.start_enabled").data(), &start_enabled)) {
        mouse.start_enabled = start_enabled;
        changed = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tr.tr("mouse.start_enabled.hint").data());
    }

    ImGui::Spacing();
    widgets::section(tr.tr("mouse.section.activation").data());

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr.tr("mouse.toggle_key").data());
    ImGui::SameLine(220.0f);
    const ChordCaptureResult toggle =
        chord_capture("mouse_toggle", mouse.toggle_key, tr, config.fn_layer_key);
    capturing |= toggle.state == ChordCaptureResult::State::Capturing;
    if (toggle.state == ChordCaptureResult::State::Accepted) {
        mouse.toggle_key = toggle.chord;
        changed = true;
    }
    if (config.chord_is_reserved_except(mouse.toggle_key,
                                        AppConfig::ControlKey::MouseToggle) ||
        config.chord_in_use(mouse.toggle_key)) {
        ImGui::SameLine();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(colors::kWarning),
                           "%s", tr.tr("mouse.reserved").data());
    }

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr.tr("mouse.behavior").data());
    ImGui::SameLine(220.0f);
    ImGui::SetNextItemWidth(180.0f);
    int behavior = mouse.hold_to_activate ? 1 : 0;
    const char* behaviors[] = {
        tr.tr("mouse.behavior.toggle").data(),
        tr.tr("mouse.behavior.hold").data(),
    };
    if (ImGui::Combo("##mouse_behavior", &behavior, behaviors, 2)) {
        mouse.hold_to_activate = behavior == 1;
        changed = true;
    }

    bool beep = mouse.beep;
    if (ImGui::Checkbox(tr.tr("mouse.beep").data(), &beep)) {
        mouse.beep = beep;
        changed = true;
    }

    ImGui::Spacing();
    widgets::section(tr.tr("mouse.section.keys").data());
    ImGui::TextDisabled("%s", tr.tr("mouse.keys.hint").data());
    ImGui::Spacing();

    if (ImGui::BeginTable("mouse_bindings", 4,
                          ImGuiTableFlags_SizingStretchProp |
                          ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn(tr.tr("bindings.col.action").data(),
                                ImGuiTableColumnFlags_WidthFixed, 180.0f);
        ImGui::TableSetupColumn(tr.tr("bindings.col.chord").data(),
                                ImGuiTableColumnFlags_WidthFixed, 220.0f);
        ImGui::TableSetupColumn(tr.tr("bindings.col.mode").data(),
                                ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (size_t i = 0; i < mouse.bindings.size(); ++i) {
            Binding& binding = mouse.bindings[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(widgets::action_label(tr, binding.action));

            ImGui::TableSetColumnIndex(1);
            const ChordCaptureResult result =
                chord_capture("chord", binding.chord, tr, config.fn_layer_key);
            capturing |= result.state == ChordCaptureResult::State::Capturing;
            if (result.state == ChordCaptureResult::State::Accepted) {
                binding.chord = result.chord;
                changed = true;
            }
            if (mouse_conflict(mouse, binding.chord, i) ||
                config.chord_is_reserved(binding.chord)) {
                ImGui::SameLine();
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(colors::kWarning), "!");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", tr.tr("bindings.conflict").data());
                }
            }

            ImGui::TableSetColumnIndex(2);
            ImGui::SetNextItemWidth(100.0f);
            int mode = static_cast<int>(binding.mode);
            const char* modes[] = {widgets::bind_mode_label(tr, BindMode::Hold),
                                   widgets::bind_mode_label(tr, BindMode::Toggle),
                                   widgets::bind_mode_label(tr, BindMode::Tap)};
            if (ImGui::Combo("##mode", &mode, modes, 3)) {
                binding.mode = static_cast<BindMode>(mode);
                changed = true;
            }

            ImGui::TableSetColumnIndex(3);
            if (ImGui::SmallButton(tr.tr("common.remove").data())) {
                mouse.bindings.erase(mouse.bindings.begin() +
                                     static_cast<std::ptrdiff_t>(i));
                changed = true;
                ImGui::PopID();
                break;
            }

            ImGui::PopID();
        }

        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    static int new_action = static_cast<int>(Action::MoveUp);
    static Chord new_chord;

    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::BeginCombo("##mouse_new_action",
                          widgets::action_label(tr, static_cast<Action>(new_action)))) {
        for (int a = 1; a < static_cast<int>(Action::Count); ++a) {
            const auto action = static_cast<Action>(a);
            if (!action_is_bindable(action)) {
                continue;
            }
            if (ImGui::Selectable(widgets::action_label(tr, action), a == new_action)) {
                new_action = a;
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    const ChordCaptureResult result =
        chord_capture("mouse_new_chord", new_chord, tr, config.fn_layer_key);
    capturing |= result.state == ChordCaptureResult::State::Capturing;
    if (result.state == ChordCaptureResult::State::Accepted) {
        new_chord = result.chord;
    }

    ImGui::SameLine();
    const bool can_add = new_chord.valid() &&
                         !mouse_conflict(mouse, new_chord, mouse.bindings.size()) &&
                         !config.chord_is_reserved(new_chord);
    ImGui::BeginDisabled(!can_add);
    if (ImGui::Button(tr.tr("bindings.add").data())) {
        Binding binding;
        binding.action = static_cast<Action>(new_action);
        binding.chord = new_chord;
        binding.mode = action_is_continuous(binding.action) ? BindMode::Hold
                                                            : BindMode::Tap;
        mouse.bindings.push_back(binding);
        new_chord = Chord{};
        changed = true;
    }
    ImGui::EndDisabled();

    if (new_chord.valid() && (mouse_conflict(mouse, new_chord, mouse.bindings.size()) ||
                              config.chord_is_reserved(new_chord))) {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(colors::kWarning),
                           "%s", tr.tr("bindings.conflict").data());
    }

    ImGui::Spacing();
    if (ImGui::Button(tr.tr("mouse.reset_defaults").data())) {
        mouse.bindings = make_mouse_mode_bindings();
        changed = true;
    }

    if (changed) {
        *context.config_dirty = true;
    }
    context.capture_active = capturing;
}

}  // namespace ui
}  // namespace km
