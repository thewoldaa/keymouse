// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/dashboard.h"

#include <string>

#include <imgui.h>

#include "core/keycodes.h"
#include "platform/startup.h"
#include "ui/theme.h"
#include "ui/widgets/chord_capture.h"

namespace km {
namespace ui {
namespace {

// Draws the Fn layer key picker. The user can choose any non-modifier key;
// real Fn is invisible to the OS, so this is how "Fn + X" works.
bool draw_fn_layer_picker(AppConfig& config, I18n& tr) {
    bool changed = false;

    ImGui::TextUnformatted(tr.tr("general.fn_layer").data());
    ImGui::SameLine(200.0f);
    ImGui::SetNextItemWidth(190.0f);

    if (ImGui::BeginCombo("##fnkey", key_name(config.fn_layer_key).data())) {
        for (const auto& info : all_keys()) {
            if (info.id == key::Invalid || info.is_modifier) {
                continue;
            }
            if (ImGui::Selectable(info.name.data(), info.id == config.fn_layer_key)) {
                config.fn_layer_key = info.id;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }

    ImGui::TextDisabled("%s", tr.tr("general.fn_hint").data());

    return changed;
}

}  // namespace

void draw_general_tab(DashboardContext& context) {
    AppConfig& config = *context.config;
    I18n& tr = *context.i18n;
    bool changed = false;
    bool reload_pending = false;
    bool capture_active = false;

    widgets::section(tr.tr("general.section.language").data());

    ImGui::TextUnformatted(tr.tr("general.language").data());
    ImGui::SameLine(200.0f);
    ImGui::SetNextItemWidth(190.0f);

    static const Language languages[] = {
        Language::English, Language::Russian, Language::Japanese, Language::Indonesian
    };
    const Language current = language_from_code(config.locale);
    if (ImGui::BeginCombo("##language", language_native_name(current).data())) {
        for (Language language : languages) {
            const bool selected = language == current;
            if (ImGui::Selectable(language_native_name(language).data(), selected)) {
                config.locale = std::string(language_code(language));
                changed = true;
                reload_pending = true;
            }
        }
        ImGui::EndCombo();
    }

    widgets::section(tr.tr("general.section.startup").data());

    bool startup_enabled = startup::is_enabled();
    if (ImGui::Checkbox(tr.tr("general.start_with_windows").data(),
                        &startup_enabled)) {
        startup::set_enabled(startup_enabled);
        config.start_with_windows = startup_enabled;
        changed = true;
    }

    if (ImGui::Checkbox(tr.tr("general.start_minimized").data(),
                        &config.start_minimized)) {
        changed = true;
    }

    if (ImGui::Checkbox(tr.tr("general.tray_icon").data(),
                        &config.show_tray_icon)) {
        changed = true;
    }

    widgets::section(tr.tr("general.section.safety").data());

    ImGui::TextUnformatted(tr.tr("general.panic_key").data());
    ImGui::SameLine(200.0f);
    const ChordCaptureResult result = chord_capture("panic", config.panic_key, tr);
    capture_active |= result.state == ChordCaptureResult::State::Capturing;
    if (result.state == ChordCaptureResult::State::Accepted) {
        if (config.chord_in_use(result.chord)) {
            // A panic key that also performs an action is worse than no panic
            // key, so a conflicting chord is refused rather than accepted.
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(colors::kWarning),
                               "%s", tr.tr("bindings.conflict").data());
        } else {
            config.panic_key = result.chord;
            changed = true;
        }
    }
    ImGui::TextDisabled("%s", tr.tr("general.panic_hint").data());

    ImGui::Spacing();
    changed |= draw_fn_layer_picker(config, tr);

    widgets::section(tr.tr("general.section.background").data());

    ImGui::TextDisabled("%s", tr.tr("general.quit_hint").data());
    if (ImGui::Button(tr.tr("general.quit").data())) {
        if (context.request_exit) {
            *context.request_exit = true;
        }
    }

    if (changed) {
        *context.config_dirty = true;
    }
    if (reload_pending && context.request_locale_reload) {
        *context.request_locale_reload = true;
    }
    context.capture_active = capture_active;
}

}  // namespace ui
}  // namespace km
