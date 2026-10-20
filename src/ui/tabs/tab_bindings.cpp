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

// Checks whether a chord collides with any other binding in the profile.
bool has_conflict(const Profile& profile, const Chord& chord, size_t ignore_index) {
    for (size_t i = 0; i < profile.bindings.size(); ++i) {
        if (i == ignore_index) {
            continue;
        }
        if (profile.bindings[i].chord == chord) {
            return true;
        }
    }
    return false;
}

}  // namespace

void draw_bindings_tab(DashboardContext& context) {
    AppConfig& config = *context.config;
    Profile& profile = config.active();
    I18n& tr = *context.i18n;

    ImGui::TextDisabled("%s", tr.tr("bindings.hint").data());
    ImGui::Spacing();

    // Any recorder that is open must stop the engine from interpreting keys,
    // otherwise the chord being recorded is swallowed by the very binding it is
    // meant to replace.
    bool capturing = false;

    if (ImGui::BeginTable("bindings", 5,
                          ImGuiTableFlags_SizingStretchProp |
                          ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn(tr.tr("bindings.col.action").data(),
                                ImGuiTableColumnFlags_WidthFixed, 170.0f);
        ImGui::TableSetupColumn(tr.tr("bindings.col.chord").data(),
                                ImGuiTableColumnFlags_WidthFixed, 220.0f);
        ImGui::TableSetupColumn(tr.tr("bindings.col.mode").data(),
                                ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupColumn(tr.tr("bindings.col.swallow").data(),
                                ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (size_t i = 0; i < profile.bindings.size(); ++i) {
            Binding& binding = profile.bindings[i];
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
                *context.config_dirty = true;
            }

            if (has_conflict(profile, binding.chord, i) ||
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
                *context.config_dirty = true;
            }

            ImGui::TableSetColumnIndex(3);
            bool swallow = binding.swallow == SwallowMode::Swallow;
            if (ImGui::Checkbox("##swallow", &swallow)) {
                binding.swallow = swallow ? SwallowMode::Swallow : SwallowMode::PassThrough;
                *context.config_dirty = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", tr.tr("bindings.swallow_hint").data());
            }

            ImGui::TableSetColumnIndex(4);
            if (ImGui::SmallButton(tr.tr("common.remove").data())) {
                profile.bindings.erase(profile.bindings.begin() +
                                       static_cast<std::ptrdiff_t>(i));
                *context.config_dirty = true;
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

    // Add a new binding. The pending chord survives frames because the recorder
    // keeps its own state in ImGui storage; only the chosen action has to be
    // held here.
    static int new_action = static_cast<int>(Action::ClickLeft);
    static Chord new_chord;

    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::BeginCombo("##newaction",
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
        chord_capture("newchord", new_chord, tr, config.fn_layer_key);
    capturing |= result.state == ChordCaptureResult::State::Capturing;
    if (result.state == ChordCaptureResult::State::Accepted) {
        new_chord = result.chord;
    }

    ImGui::SameLine();
    const bool can_add = new_chord.valid() &&
                         !has_conflict(profile, new_chord, profile.bindings.size()) &&
                         !config.chord_is_reserved(new_chord);
    ImGui::BeginDisabled(!can_add);
    if (ImGui::Button(tr.tr("bindings.add").data())) {
        Binding binding;
        binding.action = static_cast<Action>(new_action);
        binding.chord = new_chord;
        binding.mode = action_is_continuous(binding.action) ? BindMode::Hold : BindMode::Tap;
        profile.bindings.push_back(binding);
        new_chord = Chord{};
        *context.config_dirty = true;
    }
    ImGui::EndDisabled();

    if (new_chord.valid() &&
        (has_conflict(profile, new_chord, profile.bindings.size()) ||
         config.chord_is_reserved(new_chord))) {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(colors::kWarning),
                           "%s", tr.tr("bindings.conflict").data());
    }

    context.capture_active = capturing;
}

}  // namespace ui
}  // namespace km
