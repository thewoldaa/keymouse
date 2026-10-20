// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/dashboard.h"

#include <algorithm>
#include <string>

#include <imgui.h>

#include "core/autoclick.h"
#include "ui/theme.h"
#include "ui/widgets/chord_capture.h"

namespace km {
namespace ui {
namespace {

enum class TimeUnit { Seconds = 0, Minutes = 1, Hours = 2 };

double unit_scale(TimeUnit unit) {
    switch (unit) {
        case TimeUnit::Minutes: return 60.0;
        case TimeUnit::Hours: return 3600.0;
        case TimeUnit::Seconds: break;
    }
    return 1.0;
}

TimeUnit default_unit(double seconds) {
    if (seconds >= 3600.0) {
        return TimeUnit::Hours;
    }
    if (seconds >= 60.0) {
        return TimeUnit::Minutes;
    }
    return TimeUnit::Seconds;
}

const char* unit_label(I18n& tr, TimeUnit unit) {
    switch (unit) {
        case TimeUnit::Minutes: return tr.tr("time.minutes").data();
        case TimeUnit::Hours: return tr.tr("time.hours").data();
        case TimeUnit::Seconds: break;
    }
    return tr.tr("time.seconds").data();
}

// A duration editor: value in the chosen unit plus a unit combo. Returns true
// when the stored seconds changed. The unit is per-task UI state, not config:
// switching the combo must not change the stored seconds, only how they are
// displayed.
bool draw_duration(const char* id, double& seconds, I18n& tr, bool allow_zero) {
    bool changed = false;

    ImGui::PushID(id);

    // Keep the read and the write on the same ID. Reading before PushID and
    // writing after it produces two different storage slots, so the unit would
    // appear to revert as soon as the mouse button came up.
    const ImGuiID unit_id = ImGui::GetID("##unit");
    ImGuiStorage* storage = ImGui::GetStateStorage();
    int default_index = static_cast<int>(default_unit(seconds));
    int unit_index = storage->GetInt(unit_id, default_index);
    if (unit_index < 0 || unit_index > 2) {
        unit_index = default_index;
        storage->SetInt(unit_id, unit_index);
    }
    const TimeUnit unit = static_cast<TimeUnit>(unit_index);

    ImGui::SetNextItemWidth(110.0f);
    double value = seconds / unit_scale(unit);
    if (ImGui::InputDouble("##value", &value, 0.0, 0.0, "%.3f")) {
        const double minimum =
            allow_zero ? 0.0 : kMinAutoClickIntervalSeconds;
        const double maximum = allow_zero ? kMaxAutoClickDelaySeconds
                                          : kMaxAutoClickIntervalSeconds;
        seconds = std::clamp(value * unit_scale(unit), minimum, maximum);
        changed = true;
    }
    ImGui::SameLine(0.0f, 4.0f);
    ImGui::SetNextItemWidth(110.0f);
    const char* labels[] = {unit_label(tr, TimeUnit::Seconds),
                            unit_label(tr, TimeUnit::Minutes),
                            unit_label(tr, TimeUnit::Hours)};
    if (ImGui::Combo("##unit", &unit_index, labels, 3)) {
        storage->SetInt(unit_id, unit_index);
    }
    ImGui::PopID();
    return changed;
}

std::string next_task_id(const AutoClickConfig& config) {
    int number = static_cast<int>(config.tasks.size()) + 1;
    for (;;) {
        const std::string candidate = "task-" + std::to_string(number);
        const bool used = std::any_of(config.tasks.begin(), config.tasks.end(),
            [&candidate](const AutoClickTask& task) { return task.id == candidate; });
        if (!used) {
            return candidate;
        }
        ++number;
    }
}

std::string next_task_name(const AutoClickConfig& config, const I18n& tr) {
    const std::string base = tr.tr("autoclick.new_name").data();
    int number = static_cast<int>(config.tasks.size()) + 1;
    for (;;) {
        const std::string candidate = number == 1
            ? base
            : base + " " + std::to_string(number);
        const bool used = std::any_of(config.tasks.begin(), config.tasks.end(),
            [&candidate](const AutoClickTask& task) { return task.name == candidate; });
        if (!used) {
            return candidate;
        }
        ++number;
    }
}

}  // namespace

void draw_auto_click_tab(DashboardContext& context) {
    AppConfig& config = *context.config;
    AutoClickConfig& auto_click = config.auto_click;
    I18n& tr = *context.i18n;
    bool changed = false;

    ImGui::TextDisabled("%s", tr.tr("autoclick.hint").data());
    ImGui::Spacing();

    bool master = auto_click.enabled;
    if (ImGui::Checkbox(tr.tr("autoclick.master").data(), &master)) {
        auto_click.enabled = master;
        if (context.engine) {
            context.engine->set_auto_click_enabled(master);
        }
        changed = true;
    }

    ImGui::SameLine(260.0f);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr.tr("autoclick.next").data());
    ImGui::SameLine();
    char countdown[32];
    widgets::format_duration(context.auto_click_remaining_ms, countdown, sizeof(countdown));
    ImGui::TextUnformatted(countdown);
    ImGui::SameLine();
    ImGui::TextDisabled("(%s: %llu)", tr.tr("autoclick.fires").data(),
                        static_cast<unsigned long long>(context.auto_click_fires));

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr.tr("autoclick.toggle_key").data());
    ImGui::SameLine(260.0f);
    const ChordCaptureResult toggle = chord_capture("autoclick_toggle",
                                                    auto_click.toggle_key, tr,
                                                    config.fn_layer_key);
    bool capturing = toggle.state == ChordCaptureResult::State::Capturing;
    if (toggle.state == ChordCaptureResult::State::Accepted) {
        auto_click.toggle_key = toggle.chord;
        changed = true;
    }
    if (auto_click.toggle_key == config.panic_key ||
        auto_click.toggle_key == config.mouse_mode.toggle_key ||
        auto_click.toggle_key == config.macros.record_key ||
        auto_click.toggle_key == config.macros.play_key ||
        auto_click.toggle_key == config.macros.stop_key) {
        ImGui::SameLine();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(colors::kWarning),
                           "%s", tr.tr("mouse.reserved").data());
    }

    ImGui::Spacing();
    widgets::section(tr.tr("autoclick.tasks").data());

    if (auto_click.tasks.empty()) {
        ImGui::TextDisabled("%s", tr.tr("autoclick.empty").data());
    }

    for (size_t i = 0; i < auto_click.tasks.size(); ++i) {
        AutoClickTask& task = auto_click.tasks[i];
        ImGui::PushID(task.id.c_str());

        ImGui::BeginChild("task", ImVec2(0.0f, 158.0f), ImGuiChildFlags_Borders);
        bool enabled = task.enabled;
        if (ImGui::Checkbox("##enabled", &enabled)) {
            task.enabled = enabled;
            changed = true;
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(230.0f);
        if (widgets::input_text("##name", task.name)) {
            changed = true;
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(auto_click.tasks.size() <= 1);
        if (ImGui::SmallButton(tr.tr("common.remove").data())) {
            auto_click.tasks.erase(auto_click.tasks.begin() +
                                   static_cast<std::ptrdiff_t>(i));
            changed = true;
            ImGui::EndDisabled();
            ImGui::EndChild();
            ImGui::PopID();
            break;
        }
        ImGui::EndDisabled();

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(tr.tr("autoclick.action").data());
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150.0f);
        int action_index = static_cast<int>(task.action);
        const char* actions[] = {
            widgets::action_label(tr, Action::ClickLeft),
            widgets::action_label(tr, Action::ClickRight),
            widgets::action_label(tr, Action::ClickMiddle),
            widgets::action_label(tr, Action::DoubleClick),
        };
        if (ImGui::Combo("##action", &action_index, actions, 4)) {
            task.action = static_cast<AutoClickAction>(action_index);
            changed = true;
        }

        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(tr.tr("autoclick.every").data());
        ImGui::SameLine();
        if (draw_duration("interval", task.interval_seconds, tr, false)) {
            changed = true;
        }

        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(tr.tr("autoclick.first").data());
        ImGui::SameLine();
        if (draw_duration("delay", task.start_delay_seconds, tr, true)) {
            changed = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tr.tr("autoclick.first.hint").data());
        }

        bool forever = task.repeat_count == 0;
        if (ImGui::Checkbox(tr.tr("autoclick.repeat_forever").data(), &forever)) {
            task.repeat_count = forever ? 0 : 10;
            changed = true;
        }
        if (!forever) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120.0f);
            int count = task.repeat_count;
            if (ImGui::InputInt("##repeats", &count, 1, 10)) {
                task.repeat_count = std::clamp(count, 1, kMaxAutoClickRepeats);
                changed = true;
            }
        }

        ImGui::SameLine();
        bool missed = task.run_missed_once;
        if (ImGui::Checkbox(tr.tr("autoclick.run_missed").data(), &missed)) {
            task.run_missed_once = missed;
            changed = true;
        }

        // Keep the filter on its own row: stacked onto the previous one it runs
        // past the right edge in Russian and Japanese.
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(tr.tr("autoclick.only_app").data());
        ImGui::SameLine();
        ImGui::SetNextItemWidth(240.0f);
        if (widgets::input_text("##only_app", task.only_if_app)) {
            changed = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tr.tr("autoclick.only_app.hint").data());
        }

        ImGui::EndChild();
        ImGui::PopID();
    }

    ImGui::Spacing();
    if (ImGui::Button(tr.tr("autoclick.add").data())) {
        AutoClickTask task;
        task.id = next_task_id(auto_click);
        task.name = next_task_name(auto_click, tr);
        task.enabled = true;
        auto_click.tasks.push_back(std::move(task));
        changed = true;
    }

    if (changed) {
        *context.config_dirty = true;
    }
    context.capture_active = capturing;
}

}  // namespace ui
}  // namespace km
