// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/dashboard.h"

#include <cstdio>
#include <string>

#include <imgui.h>

#include "core/keycodes.h"
#include "ui/theme.h"

namespace km {
namespace ui {

void draw_status_tab(DashboardContext& context) {
    I18n& tr = *context.i18n;

    widgets::section(tr.tr("status.section.state").data());

    widgets::status_badge(context.hook_running,
        tr.tr("status.active").data(), tr.tr("status.inactive").data());

    if (!context.hook_running) {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(colors::kDanger),
                           "%s", tr.tr("status.hook_failed").data());
    }

    ImGui::Spacing();
    widgets::key_value(tr.tr("status.enabled").data(),
        context.config->enabled ? tr.tr("common.yes").data() : tr.tr("common.no").data());
    widgets::key_value(tr.tr("status.mouse_mode").data(),
        context.mouse_mode ? tr.tr("common.yes").data() : tr.tr("common.no").data());
    widgets::key_value(tr.tr("status.auto_click").data(),
        context.config->auto_click.enabled ? tr.tr("common.yes").data()
                                           : tr.tr("common.no").data());
    if (context.auto_click_remaining_ms >= 0) {
        char countdown[32];
        widgets::format_duration(context.auto_click_remaining_ms, countdown,
                                 sizeof(countdown));
        widgets::key_value(tr.tr("status.auto_click_next").data(), countdown);
    }
    widgets::key_value(tr.tr("status.foreground").data(),
        context.foreground_exe.empty() ? "-" : context.foreground_exe);
    widgets::key_value(tr.tr("status.profile").data(), context.config->active().name);
    widgets::key_value(tr.tr("status.elevated").data(),
        context.elevated ? tr.tr("common.yes").data() : tr.tr("common.no").data());

    if (!context.elevated) {
        ImGui::Spacing();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(colors::kWarning),
                           "%s", tr.tr("status.elevation_hint").data());
    }

    // Which actions are live right now. A binding that does not fire is
    // otherwise invisible, which is the hardest failure to diagnose.
    widgets::section(tr.tr("status.section.active_actions").data());
    bool any_action = false;
    for (int a = 1; a < static_cast<int>(Action::Count); ++a) {
        const auto action = static_cast<Action>(a);
        if (!widgets::mask_has(context.active_mask, action)) {
            continue;
        }
        if (any_action) {
            ImGui::SameLine();
        }
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(colors::kAccent), "%s",
                           widgets::action_label(tr, action));
        any_action = true;
    }
    if (!any_action) {
        ImGui::TextDisabled("%s", tr.tr("status.no_actions").data());
    }

    widgets::section(tr.tr("status.section.activity").data());

    char buffer[128];
    std::snprintf(buffer, sizeof(buffer), "%llu",
                  static_cast<unsigned long long>(context.stats.ticks));
    widgets::key_value(tr.tr("status.ticks").data(), buffer);

    std::snprintf(buffer, sizeof(buffer), "%llu",
                  static_cast<unsigned long long>(context.stats.input_events));
    widgets::key_value(tr.tr("status.input_events").data(), buffer);

    std::snprintf(buffer, sizeof(buffer), "%llu",
                  static_cast<unsigned long long>(context.stats.injected_moves));
    widgets::key_value(tr.tr("status.injected_moves").data(), buffer);

    std::snprintf(buffer, sizeof(buffer), "%llu",
                  static_cast<unsigned long long>(context.stats.swallowed_events));
    widgets::key_value(tr.tr("status.swallowed").data(), buffer);

    std::snprintf(buffer, sizeof(buffer), "%llu",
                  static_cast<unsigned long long>(context.stats.dropped_events));
    widgets::key_value(tr.tr("status.dropped").data(), buffer);

    if (context.stats.dropped_events > 0) {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(colors::kWarning),
                           "%s", tr.tr("status.dropped_hint").data());
    }

    widgets::section(tr.tr("status.section.cursor").data());

    int32_t x = 0;
    int32_t y = 0;
    if (screen::cursor_position(x, y)) {
        std::snprintf(buffer, sizeof(buffer), "%d, %d", x, y);
        widgets::key_value(tr.tr("status.cursor_position").data(), buffer);
    }

    const MonitorRect area = screen::current_work_area();
    std::snprintf(buffer, sizeof(buffer), "%d x %d", area.width(), area.height());
    widgets::key_value(tr.tr("status.work_area").data(), buffer);
}

}  // namespace ui
}  // namespace km
