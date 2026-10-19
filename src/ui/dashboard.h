// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <string>
#include <vector>

#include <imgui.h>

#include "core/action.h"
#include "core/binding.h"
#include "core/config.h"
#include "core/engine.h"
#include "core/i18n.h"
#include "platform/screen.h"

namespace km {
namespace ui {

// Shared state handed to every tab. The dashboard owns it; tabs only read and
// request changes through the callbacks.
struct DashboardContext {
    AppConfig* config = nullptr;
    Engine* engine = nullptr;
    I18n* i18n = nullptr;

    // Set when the user changed something that must be persisted and applied.
    bool* config_dirty = nullptr;
    // Set when the user asked to close the window to the tray.
    bool* request_hide = nullptr;
    // Set when the user asked the application to exit.
    bool* request_exit = nullptr;
    // Set when the language changed and the translation table must reload.
    bool* request_locale_reload = nullptr;
    // Set when the user asked to open the configuration folder.
    bool* request_open_config = nullptr;

    // Live status, refreshed by the dashboard each frame.
    bool hook_running = false;
    bool elevated = false;
    std::string foreground_exe;
    EngineStats stats;
    uint32_t active_mask = 0;
    // Set by a tab while a chord recorder is open. The application publishes it
    // to the engine as capture mode.
    bool capture_active = false;
};

// Each tab renders into the current ImGui window.
void draw_status_tab(DashboardContext& context);
void draw_bindings_tab(DashboardContext& context);
void draw_movement_tab(DashboardContext& context);
void draw_profiles_tab(DashboardContext& context);
void draw_general_tab(DashboardContext& context);
void draw_diagnostics_tab(DashboardContext& context);

// Small shared helpers used by more than one tab.
namespace widgets {

// Section header with consistent spacing.
void section(const char* title);

// A label/value row in a two-column table.
void key_value(const char* key, const std::string& value);

// Centred accent-coloured "active" / muted "inactive" badge.
void status_badge(bool active, const char* active_text, const char* inactive_text);

// Draws a slider with a fixed-width label column and a reset button.
bool slider_row(I18n& i18n, const char* label, float* value, float min, float max,
                const char* format, float default_value);

// Localised name of an action, for example "Left Click". Actions are the one
// list that grows with the program, so a locale that has not translated an
// action yet falls back to its English name rather than showing a raw key.
const char* action_label(I18n& i18n, Action action);

// Localised name of a bind mode ("Hold" / "Toggle" / "Tap").
const char* bind_mode_label(I18n& i18n, BindMode mode);

// True while Action(i) is set in an engine active-action mask.
bool mask_has(uint32_t mask, Action action);

}  // namespace widgets

}  // namespace ui
}  // namespace km
