// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/dashboard.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include <imgui.h>

#include "core/macro.h"
#include "platform/screen.h"
#include "ui/theme.h"
#include "ui/widgets/chord_capture.h"

namespace km {
namespace ui {
namespace {

// ImGui scales its style metrics with the monitor DPI but not literal pixel
// widths, so a fixed field size clips its value on a scaled display: at 125% a
// 100 px InputInt has about 25 px left for text after its two step buttons.
// Sizing the fields in ems instead keeps the value readable at any DPI.
float em(float value) {
    return ImGui::GetFontSize() * value;
}

const char* state_label(I18n& tr, MacroPlayerState state) {
    switch (state) {
        case MacroPlayerState::Playing: return tr.tr("macro.state.playing").data();
        case MacroPlayerState::Paused: return tr.tr("macro.state.paused").data();
        case MacroPlayerState::Idle: break;
    }
    return tr.tr("macro.state.idle").data();
}

const char* step_type_label(I18n& tr, MacroStepType type) {
    switch (type) {
        case MacroStepType::Move: return tr.tr("macro.step.move").data();
        case MacroStepType::MouseDelta: return tr.tr("macro.step.mouse_delta").data();
        case MacroStepType::ButtonDown: return tr.tr("macro.step.button_down").data();
        case MacroStepType::ButtonUp: return tr.tr("macro.step.button_up").data();
        case MacroStepType::Scroll: return tr.tr("macro.step.scroll").data();
        case MacroStepType::ScrollH: return tr.tr("macro.step.scroll_h").data();
        case MacroStepType::KeyDown: return tr.tr("macro.step.key_down").data();
        case MacroStepType::KeyUp: return tr.tr("macro.step.key_up").data();
        case MacroStepType::Wait: break;
    }
    return tr.tr("macro.step.wait").data();
}

const char* button_label(I18n& tr, MouseButton button) {
    switch (button) {
        case MouseButton::Right: return tr.tr("macro.button.right").data();
        case MouseButton::Middle: return tr.tr("macro.button.middle").data();
        case MouseButton::X1: return tr.tr("macro.button.x1").data();
        case MouseButton::X2: return tr.tr("macro.button.x2").data();
        case MouseButton::Left: break;
    }
    return tr.tr("macro.button.left").data();
}

MacroStep default_step(MacroStepType type) {
    MacroStep step;
    step.type = type;
    step.dt_ms = 50;
    switch (type) {
        case MacroStepType::Move: {
            int32_t x = 0;
            int32_t y = 0;
            if (screen::cursor_position(x, y)) {
                step.x = x;
                step.y = y;
            }
            break;
        }
        case MacroStepType::MouseDelta:
            // A camera nudge is a small relative move; zero would be a step
            // that plays as nothing at all.
            step.x = 40;
            step.y = 0;
            break;
        case MacroStepType::ButtonDown:
        case MacroStepType::ButtonUp:
            step.data = static_cast<int32_t>(MouseButton::Left);
            break;
        case MacroStepType::Scroll:
            step.data = 120;
            break;
        case MacroStepType::KeyDown:
        case MacroStepType::KeyUp:
            step.key = key::Enter;
            break;
        case MacroStepType::ScrollH:
        case MacroStepType::Wait:
            break;
    }
    return step;
}

// Toolbar action collected during the table loop; the vector must not be
// modified while the rows are still being drawn.
enum class RowAction { None, Up, Down, Duplicate, Delete };

struct PendingRowAction {
    RowAction action = RowAction::None;
    size_t index = 0;
};

void apply_row_action(std::vector<MacroStep>& steps, const PendingRowAction& pending) {
    if (pending.action == RowAction::None || pending.index >= steps.size()) {
        return;
    }
    const size_t index = pending.index;
    switch (pending.action) {
        case RowAction::Up:
            if (index > 0) {
                std::swap(steps[index], steps[index - 1]);
            }
            break;
        case RowAction::Down:
            if (index + 1 < steps.size()) {
                std::swap(steps[index], steps[index + 1]);
            }
            break;
        case RowAction::Duplicate:
            steps.insert(steps.begin() + static_cast<std::ptrdiff_t>(index + 1),
                         steps[index]);
            break;
        case RowAction::Delete:
            steps.erase(steps.begin() + static_cast<std::ptrdiff_t>(index));
            break;
        case RowAction::None:
            break;
    }
}

void multiply_delays(std::vector<MacroStep>& steps, double factor) {
    for (MacroStep& step : steps) {
        const double scaled = static_cast<double>(step.dt_ms) * factor;
        step.dt_ms = std::clamp<int64_t>(static_cast<int64_t>(scaled), 0,
                                         kMaxMacroStepDelayMs);
    }
}

}  // namespace

void draw_macros_tab(DashboardContext& context) {
    AppConfig& config = *context.config;
    MacroConfig& macros = config.macros;
    I18n& tr = *context.i18n;
    Engine* engine = context.engine;

    bool config_changed = false;
    bool capturing = false;

    const bool recording = engine != nullptr && engine->macro_recording();
    const MacroPlayerState state =
        engine ? engine->macro_player_state() : MacroPlayerState::Idle;
    DashboardContext::MacroEditState& edit = context.macro_edit;

    ImGui::TextDisabled("%s", tr.tr("macro.hint").data());
    ImGui::Spacing();

    // ---- Transport ---------------------------------------------------------
    if (recording) {
        ImGui::PushStyleColor(ImGuiCol_Button,
                              ImGui::ColorConvertU32ToFloat4(colors::kAccent));
    }
    if (ImGui::Button(recording ? tr.tr("macro.stop_record").data()
                                : tr.tr("macro.record").data())) {
        if (context.macro_toggle_record) {
            context.macro_toggle_record();
        }
    }
    if (recording) {
        ImGui::PopStyleColor();
    }

    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    if (recording) {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(colors::kAccent), "%s",
                           tr.tr("macro.recording").data());
        ImGui::SameLine();
        ImGui::TextDisabled("%llu %s",
                            engine ? static_cast<unsigned long long>(
                                         engine->macro_recording_steps())
                                   : 0ull,
                            tr.tr("macro.steps").data());
        if (engine) {
            // Camera steps and key steps are counted separately because a
            // recording can look busy and still contain neither: mouse mode or
            // a game reading raw input shows up here immediately.
            ImGui::SameLine();
            ImGui::TextDisabled("| %llu %s | %llu %s",
                                static_cast<unsigned long long>(
                                    engine->macro_recording_camera_steps()),
                                tr.tr("macro.recording.camera").data(),
                                static_cast<unsigned long long>(
                                    engine->macro_recording_key_steps()),
                                tr.tr("macro.recording.keys").data());
        }
    } else {
        const bool loaded = edit.loaded && !edit.macro.steps.empty();
        ImGui::BeginDisabled(!loaded);
        if (ImGui::Button(tr.tr("macro.play").data()) && context.macro_play) {
            context.macro_play();
        }
        ImGui::SameLine();
        if (state == MacroPlayerState::Paused) {
            if (ImGui::Button(tr.tr("macro.resume").data()) && context.macro_resume) {
                context.macro_resume();
            }
        } else {
            if (ImGui::Button(tr.tr("macro.pause").data()) && context.macro_pause) {
                context.macro_pause();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(tr.tr("macro.stop").data()) && context.macro_stop) {
            context.macro_stop();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(edit.selected_step <= 0);
        if (ImGui::Button(tr.tr("macro.play_from").data()) &&
            context.macro_play_from_step) {
            context.macro_play_from_step();
        }
        ImGui::EndDisabled();
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s: %s", tr.tr("macro.state").data(), state_label(tr, state));
        if (state != MacroPlayerState::Idle && engine) {
            ImGui::SameLine();
            ImGui::TextDisabled("| %zu/%zu %s | %llu %s", engine->macro_player_step(),
                                edit.macro.steps.size(), tr.tr("macro.steps").data(),
                                static_cast<unsigned long long>(engine->macro_player_loops()),
                                tr.tr("macro.loops_short").data());
        }
    }

    // Playback options, applied by every play request including the hotkey.
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr.tr("macro.loops").data());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(em(6.0f));
    int loops = context.macro_options.loops;
    if (ImGui::InputInt("##loops", &loops, 1, 10)) {
        context.macro_options.loops = std::clamp(loops, 0, 100000);
    }

    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr.tr("macro.loop_delay").data());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(em(6.0f));
    int loop_delay = static_cast<int>(context.macro_options.loop_delay_ms);
    if (ImGui::InputInt("##loop_delay", &loop_delay, 10, 100)) {
        context.macro_options.loop_delay_ms = std::clamp(loop_delay, 0, 3600000);
    }

    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr.tr("macro.speed").data());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    float speed = static_cast<float>(context.macro_options.speed);
    if (ImGui::SliderFloat("##speed", &speed, 0.25f, 4.0f, "%.2fx")) {
        context.macro_options.speed = std::clamp(static_cast<double>(speed), 0.25, 4.0);
    }

    // ---- Library -----------------------------------------------------------
    widgets::section(tr.tr("macro.library").data());

    ImGui::BeginChild("macro_library", ImVec2(0.0f, 112.0f), ImGuiChildFlags_Borders);
    if (macros.entries.empty()) {
        ImGui::TextDisabled("%s", tr.tr("macro.empty").data());
    }
    for (size_t i = 0; i < macros.entries.size(); ++i) {
        MacroEntry& entry = macros.entries[i];
        ImGui::PushID(entry.id.c_str());

        const bool selected =
            edit.loaded && edit.macro.id == entry.id;
        if (ImGui::Selectable(entry.name.c_str(), selected, ImGuiSelectableFlags_None,
                              ImVec2(240.0f, 0.0f))) {
            if (context.macro_select) {
                context.macro_select(entry.id);
            }
        }

        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", tr.tr("macro.hotkey").data());
        ImGui::SameLine();
        const ChordCaptureResult captured = chord_capture("hotkey", entry.hotkey, tr,
                                                          config.fn_layer_key);
        if (captured.state == ChordCaptureResult::State::Capturing) {
            capturing = true;
        }
        if (captured.state == ChordCaptureResult::State::Accepted &&
            context.macro_set_hotkey) {
            context.macro_set_hotkey(entry.id, captured.chord);
        }
        // A hotkey that is also a control key or a binding never fires: the
        // hook thread resolves control keys and bindings first. Warn while the
        // user is picking one instead of leaving a silently dead hotkey.
        if (entry.hotkey.valid() &&
            (config.chord_is_reserved(entry.hotkey) ||
             config.chord_in_use(entry.hotkey, {}, entry.id))) {
            ImGui::SameLine();
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(colors::kWarning), "!");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", tr.tr("bindings.conflict").data());
            }
        }
        // A hotkey is optional, so it needs a way back to "unset".
        if (entry.hotkey.valid()) {
            ImGui::SameLine();
            if (ImGui::SmallButton(tr.tr("common.clear").data()) &&
                context.macro_set_hotkey) {
                context.macro_set_hotkey(entry.id, Chord{ModNone, key::Invalid});
            }
        }

        ImGui::PopID();
    }
    ImGui::EndChild();

    if (ImGui::Button(tr.tr("macro.new").data())) {
        if (context.macro_new) {
            context.macro_new();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(tr.tr("macro.import").data())) {
        if (context.macro_import) {
            context.macro_import();
        }
    }

    const bool loaded = edit.loaded;
    ImGui::SameLine();
    ImGui::BeginDisabled(!loaded);
    if (ImGui::Button(tr.tr("macro.duplicate").data()) && context.macro_duplicate) {
        context.macro_duplicate(edit.macro.id);
    }
    ImGui::SameLine();
    if (ImGui::Button(tr.tr("macro.export").data()) && context.macro_export) {
        context.macro_export(edit.macro.id);
    }
    ImGui::SameLine();
    if (ImGui::Button(tr.tr("macro.delete").data()) && context.macro_delete) {
        context.macro_delete(edit.macro.id);
    }
    ImGui::EndDisabled();

    if (!context.macro_message.empty()) {
        ImGui::TextDisabled("%s", context.macro_message.c_str());
    }

    // ---- Editor ------------------------------------------------------------
    widgets::section(tr.tr("macro.editor").data());

    if (!edit.loaded) {
        ImGui::TextDisabled("%s", tr.tr("macro.editor.empty").data());
    } else {
        Macro& macro = edit.macro;

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(tr.tr("macro.name").data());
        ImGui::SameLine();
        ImGui::SetNextItemWidth(280.0f);
        if (widgets::input_text("##macro_name", macro.name)) {
            edit.dirty = true;
        }

        ImGui::SameLine();
        char duration[32];
        widgets::format_duration(macro_duration_ms(macro), duration, sizeof(duration));
        ImGui::TextDisabled("%s: %s | %zu %s", tr.tr("macro.duration").data(), duration,
                            macro.steps.size(), tr.tr("macro.steps").data());

        if (macro.steps.empty()) {
            ImGui::TextDisabled("%s", tr.tr("macro.editor.empty").data());
        } else {
            PendingRowAction pending;

            const ImGuiTableFlags table_flags =
                ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg |
                ImGuiTableFlags_ScrollY;
            if (ImGui::BeginTable("macro_steps", 5, table_flags, ImVec2(0.0f, 240.0f))) {
                ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 46.0f);
                ImGui::TableSetupColumn(tr.tr("macro.col.time").data(),
                                        ImGuiTableColumnFlags_WidthFixed, em(8.0f) + 14.0f);
                ImGui::TableSetupColumn(tr.tr("macro.col.type").data(),
                                        ImGuiTableColumnFlags_WidthFixed, em(8.0f) + 14.0f);
                ImGui::TableSetupColumn(tr.tr("macro.col.details").data(),
                                        ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, em(9.0f));
                ImGui::TableHeadersRow();

                for (size_t i = 0; i < macro.steps.size(); ++i) {
                    MacroStep& step = macro.steps[i];
                    ImGui::PushID(static_cast<int>(i));
                    ImGui::TableNextRow();

                    ImGui::TableNextColumn();
                    char label[24];
                    std::snprintf(label, sizeof(label), "#%zu", i);
                    if (ImGui::Selectable(label,
                                          edit.selected_step == static_cast<int>(i))) {
                        edit.selected_step = static_cast<int>(i);
                    }

                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(em(8.0f));
                    int delay = static_cast<int>(step.dt_ms);
                    if (ImGui::InputInt("##delay", &delay, 1, 25)) {
                        step.dt_ms = std::clamp<int64_t>(delay, 0, kMaxMacroStepDelayMs);
                        edit.dirty = true;
                    }

                    // Display order is not enum order, so the selection index
                    // is mapped through this table instead of being cast.
                    ImGui::TableNextColumn();
                    static constexpr MacroStepType kStepTypes[] = {
                        MacroStepType::Move,
                        MacroStepType::MouseDelta,
                        MacroStepType::ButtonDown,
                        MacroStepType::ButtonUp,
                        MacroStepType::Scroll,
                        MacroStepType::ScrollH,
                        MacroStepType::KeyDown,
                        MacroStepType::KeyUp,
                        MacroStepType::Wait,
                    };
                    constexpr int kStepTypeCount =
                        static_cast<int>(std::size(kStepTypes));
                    const char* types[kStepTypeCount];
                    int type = 0;
                    for (int t = 0; t < kStepTypeCount; ++t) {
                        types[t] = step_type_label(tr, kStepTypes[t]);
                        if (kStepTypes[t] == step.type) {
                            type = t;
                        }
                    }
                    ImGui::SetNextItemWidth(em(8.0f));
                    if (ImGui::Combo("##type", &type, types, kStepTypeCount)) {
                        step.type = kStepTypes[type];
                        edit.dirty = true;
                    }

                    ImGui::TableNextColumn();
                    switch (step.type) {
                        case MacroStepType::MouseDelta: {
                            // Relative camera movement: the pair is a delta,
                            // not a screen position, so there is no "from
                            // cursor" button here.
                            ImGui::SetNextItemWidth(em(6.0f));
                            if (ImGui::InputInt("##dx", &step.x, 1, 10)) {
                                edit.dirty = true;
                            }
                            ImGui::SameLine();
                            ImGui::SetNextItemWidth(em(6.0f));
                            if (ImGui::InputInt("##dy", &step.y, 1, 10)) {
                                edit.dirty = true;
                            }
                            break;
                        }
                        case MacroStepType::Move: {
                            ImGui::SetNextItemWidth(em(6.0f));
                            if (ImGui::InputInt("##x", &step.x, 1, 10)) {
                                edit.dirty = true;
                            }
                            ImGui::SameLine();
                            ImGui::SetNextItemWidth(em(6.0f));
                            if (ImGui::InputInt("##y", &step.y, 1, 10)) {
                                edit.dirty = true;
                            }
                            ImGui::SameLine();
                            if (ImGui::SmallButton(tr.tr("macro.set_cursor").data())) {
                                int32_t x = 0;
                                int32_t y = 0;
                                if (screen::cursor_position(x, y)) {
                                    step.x = x;
                                    step.y = y;
                                    edit.dirty = true;
                                }
                            }
                            break;
                        }
                        case MacroStepType::ButtonDown:
                        case MacroStepType::ButtonUp: {
                            ImGui::SetNextItemWidth(em(8.0f));
                            int button = std::clamp(
                                step.data, 0, static_cast<int>(MouseButton::X2));
                            const char* buttons[] = {
                                button_label(tr, MouseButton::Left),
                                button_label(tr, MouseButton::Right),
                                button_label(tr, MouseButton::Middle),
                                button_label(tr, MouseButton::X1),
                                button_label(tr, MouseButton::X2),
                            };
                            if (ImGui::Combo("##button", &button, buttons, 5)) {
                                step.data = button;
                                edit.dirty = true;
                            }
                            break;
                        }
                        case MacroStepType::Scroll:
                        case MacroStepType::ScrollH: {
                            ImGui::SetNextItemWidth(em(8.0f));
                            if (ImGui::InputInt("##delta", &step.data, 120, 120)) {
                                edit.dirty = true;
                            }
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("%s",
                                                  tr.tr("macro.scroll_hint").data());
                            }
                            break;
                        }
                        case MacroStepType::KeyDown:
                        case MacroStepType::KeyUp: {
                            // Raw key, not a chord: modifier keys are valid
                            // steps, so this uses the single-key recorder.
                            const KeyCaptureResult captured_key =
                                key_capture("key", step.key, tr);
                            if (captured_key.state ==
                                ChordCaptureResult::State::Capturing) {
                                capturing = true;
                            }
                            if (captured_key.state == ChordCaptureResult::State::Accepted) {
                                step.key = captured_key.key;
                                edit.dirty = true;
                            }
                            break;
                        }
                        case MacroStepType::Wait:
                            ImGui::TextDisabled("--");
                            break;
                    }

                    ImGui::TableNextColumn();
                    if (ImGui::SmallButton(tr.tr("macro.row.up").data())) {
                        pending = {RowAction::Up, i};
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton(tr.tr("macro.row.down").data())) {
                        pending = {RowAction::Down, i};
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton(tr.tr("macro.row.dup").data())) {
                        pending = {RowAction::Duplicate, i};
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton(tr.tr("macro.row.del").data())) {
                        pending = {RowAction::Delete, i};
                    }

                    ImGui::PopID();
                }
                ImGui::EndTable();
            }

            apply_row_action(macro.steps, pending);
            if (pending.action != RowAction::None) {
                edit.dirty = true;
                if (edit.selected_step >= static_cast<int>(macro.steps.size())) {
                    edit.selected_step = static_cast<int>(macro.steps.size()) - 1;
                }
            }
        }

        if (ImGui::Button(tr.tr("macro.add_step").data())) {
            macro.steps.push_back(default_step(MacroStepType::Move));
            edit.dirty = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tr.tr("macro.add_step.hint").data());
        }
        ImGui::SameLine();
        if (ImGui::Button(tr.tr("macro.halve").data())) {
            multiply_delays(macro.steps, 0.5);
            edit.dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button(tr.tr("macro.double").data())) {
            multiply_delays(macro.steps, 2.0);
            edit.dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button(tr.tr("macro.clear").data())) {
            macro.steps.clear();
            edit.selected_step = -1;
            edit.dirty = true;
        }

        // ---- Settings ------------------------------------------------------
        if (ImGui::CollapsingHeader(tr.tr("macro.settings").data())) {
            bool keyboard = macros.record_keyboard;
            if (ImGui::Checkbox(tr.tr("macro.record_keyboard").data(), &keyboard)) {
                macros.record_keyboard = keyboard;
                config_changed = true;
            }            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", tr.tr("macro.record_keyboard.hint").data());
            }

            bool pov_drag = macros.record_pov_drag;
            if (ImGui::Checkbox(tr.tr("macro.record_pov_drag").data(), &pov_drag)) {
                macros.record_pov_drag = pov_drag;
                config_changed = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", tr.tr("macro.record_pov_drag.hint").data());
            }

            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(tr.tr("macro.tolerance").data());
            ImGui::SameLine();
            ImGui::SetNextItemWidth(140.0f);
            float tolerance = static_cast<float>(macros.move_tolerance_px);
            if (ImGui::SliderFloat("##tolerance", &tolerance,
                                   static_cast<float>(kMinMacroTolerancePx),
                                   static_cast<float>(kMaxMacroTolerancePx), "%.1f px")) {
                macros.move_tolerance_px = std::clamp(
                    static_cast<double>(tolerance), kMinMacroTolerancePx,
                    kMaxMacroTolerancePx);
                config_changed = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", tr.tr("macro.tolerance.hint").data());
            }

            const auto draw_control_key = [&](const char* id, const char* label,
                                              Chord& chord, AppConfig::ControlKey own) {
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(label);
                ImGui::SameLine();
                const ChordCaptureResult result =
                    chord_capture(id, chord, tr, config.fn_layer_key);
                if (result.state == ChordCaptureResult::State::Capturing) {
                    capturing = true;
                }
                if (result.state == ChordCaptureResult::State::Accepted) {
                    chord = result.chord;
                    config_changed = true;
                }
                // Compare against every control key except this one's own slot,
                // and against ordinary bindings too: a macro control chord that
                // is also a binding can never fire, because the hook thread
                // handles control keys first.
                if (!chord.valid() ||
                    config.chord_is_reserved_except(chord, own) ||
                    config.chord_in_use(chord)) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(colors::kWarning),
                                       "%s", tr.tr("macro.reserved").data());
                }
            };
            // macro.key has to stay distinct from the other two, otherwise the
            // first chord checked on the hook thread wins silently.
            draw_control_key("macro_record_key", tr.tr("macro.record_key").data(),
                             macros.record_key, AppConfig::ControlKey::MacroRecord);
            draw_control_key("macro_play_key", tr.tr("macro.play_key").data(),
                             macros.play_key, AppConfig::ControlKey::MacroPlay);
            draw_control_key("macro_stop_key", tr.tr("macro.stop_key").data(),
                             macros.stop_key, AppConfig::ControlKey::MacroStop);
            if (macros.record_key == macros.play_key ||
                macros.record_key == macros.stop_key ||
                macros.play_key == macros.stop_key) {
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(colors::kWarning),
                                   "%s", tr.tr("macro.keys_clash").data());
            }
        }
    }

    if (capturing) {
        context.capture_active = true;
    }
    if (config_changed) {
        *context.config_dirty = true;
    }
}

}  // namespace ui
}  // namespace km
