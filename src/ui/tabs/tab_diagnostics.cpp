// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/dashboard.h"

#include <cstdio>
#include <string>

#include <imgui.h>

#include "core/config.h"
#include "core/keycodes.h"
#include "core/version.h"
#include "platform/paths.h"
#include "ui/theme.h"

namespace km {
namespace ui {
namespace {

std::string format_count(uint64_t value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%llu",
                  static_cast<unsigned long long>(value));
    return buffer;
}

std::string format_ms(double value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.3f ms", value);
    return buffer;
}

}  // namespace

void draw_diagnostics_tab(DashboardContext& context) {
    I18n& tr = *context.i18n;
    const EngineStats& stats = context.stats;

    widgets::section(tr.tr("diagnostics.section.build").data());

    widgets::key_value(tr.tr("diagnostics.version").data(), version::kVersion);
    widgets::key_value(tr.tr("diagnostics.backend").data(),
                       context.engine ? "Win32 low-level hook" : "-");
    widgets::key_value(tr.tr("diagnostics.schema").data(),
                       std::to_string(kConfigSchemaVersion));
    widgets::key_value(tr.tr("diagnostics.bindings_loaded").data(),
        std::to_string(context.engine && context.engine->bindings()
                           ? context.engine->bindings()->count()
                           : 0));
    widgets::key_value(tr.tr("diagnostics.config_path").data(),
                       paths::config_file().string());

    widgets::section(tr.tr("diagnostics.section.events").data());

    widgets::key_value(tr.tr("diagnostics.tick_time").data(), format_ms(stats.last_tick_ms));
    widgets::key_value(tr.tr("diagnostics.input_events").data(),
                       format_count(stats.input_events));
    widgets::key_value(tr.tr("diagnostics.queued").data(),
                       format_count(stats.queued_events));
    widgets::key_value(tr.tr("diagnostics.dropped").data(),
                       format_count(stats.dropped_events));
    widgets::key_value(tr.tr("diagnostics.swallowed").data(),
                       format_count(stats.swallowed_events));
    widgets::key_value(tr.tr("diagnostics.moves").data(),
                       format_count(stats.injected_moves));
    widgets::key_value(tr.tr("diagnostics.buttons").data(),
                       format_count(stats.injected_buttons));
    widgets::key_value(tr.tr("diagnostics.scrolls").data(),
                       format_count(stats.injected_scrolls));
    widgets::key_value(tr.tr("diagnostics.focus_losses").data(),
                       format_count(stats.focus_losses));

    widgets::section(tr.tr("diagnostics.section.validation").data());

    const auto problems = validate(*context.config);
    if (problems.empty()) {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(colors::kAccent),
                           "%s", tr.tr("diagnostics.config_ok").data());
    } else {
        for (const auto& problem : problems) {
            ImGui::BulletText("%s", problem.c_str());
        }
    }

    widgets::section(tr.tr("diagnostics.section.actions").data());

    if (ImGui::Button(tr.tr("diagnostics.open_config").data())) {
        // Opening the folder happens in the application loop; ShellExecute from
        // inside the render pass would draw over a half-built frame.
        if (context.request_open_config) {
            *context.request_open_config = true;
        }
    }

    ImGui::Spacing();
    ImGui::TextDisabled("KeyMouse %s -- GPL-3.0-or-later", version::kVersion);
    ImGui::TextDisabled("https://github.com/thewoldaa/keymouse");

    ImGui::Spacing();
    ImGui::TextWrapped("%s", tr.tr("diagnostics.limits").data());
}

}  // namespace ui
}  // namespace km
