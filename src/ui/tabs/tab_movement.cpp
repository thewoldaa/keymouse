// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/dashboard.h"

#include <imgui.h>

#include "ui/theme.h"

namespace km {
namespace ui {

void draw_movement_tab(DashboardContext& context) {
    AppConfig& config = *context.config;
    MotionConfig& motion = config.motion;
    I18n& tr = *context.i18n;
    bool changed = false;

    widgets::section(tr.tr("movement.section.speed").data());

    float speed = static_cast<float>(motion.speed_px_s);
    if (widgets::slider_row(tr, tr.tr("movement.speed").data(), &speed,
                            static_cast<float>(MotionConfig::kMinSpeed),
                            static_cast<float>(MotionConfig::kMaxSpeed),
                            "%.0f px/s", 600.0f)) {
        motion.speed_px_s = speed;
        changed = true;
    }

    float accel = static_cast<float>(motion.accel_ms);
    if (widgets::slider_row(tr, tr.tr("movement.accel").data(), &accel,
                            static_cast<float>(MotionConfig::kMinAccel),
                            static_cast<float>(MotionConfig::kMaxAccel),
                            "%.0f ms", 600.0f)) {
        motion.accel_ms = accel;
        changed = true;
    }

    float accel_max = static_cast<float>(motion.accel_max);
    if (widgets::slider_row(tr, tr.tr("movement.accel_max").data(), &accel_max,
                            static_cast<float>(MotionConfig::kMinAccelMax),
                            static_cast<float>(MotionConfig::kMaxAccelMax),
                            "%.1fx", 3.0f)) {
        motion.accel_max = accel_max;
        changed = true;
    }

    float curve = static_cast<float>(motion.accel_curve);
    if (widgets::slider_row(tr, tr.tr("movement.accel_curve").data(), &curve,
                            static_cast<float>(MotionConfig::kMinCurve),
                            static_cast<float>(MotionConfig::kMaxCurve),
                            "%.2f", 1.5f)) {
        motion.accel_curve = curve;
        changed = true;
    }
    ImGui::TextDisabled("%s", tr.tr("movement.accel_hint").data());

    widgets::section(tr.tr("movement.section.method").data());

    int method = static_cast<int>(motion.method);
    const char* methods[] = {tr.tr("movement.method.relative").data(),
                             tr.tr("movement.method.absolute").data()};
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::Combo(tr.tr("movement.method").data(), &method, methods, 2)) {
        motion.method = static_cast<MotionMethod>(method);
        changed = true;
    }
    ImGui::TextDisabled("%s", tr.tr("movement.method_hint").data());

    widgets::section(tr.tr("movement.section.modifiers").data());

    float precision = static_cast<float>(motion.precision_mult);
    if (widgets::slider_row(tr, tr.tr("movement.precision").data(), &precision,
                            0.01f, 1.0f, "%.2fx", 0.25f)) {
        motion.precision_mult = precision;
        changed = true;
    }

    float turbo = static_cast<float>(motion.turbo_mult);
    if (widgets::slider_row(tr, tr.tr("movement.turbo").data(), &turbo,
                            1.0f, 20.0f, "%.1fx", 3.0f)) {
        motion.turbo_mult = turbo;
        changed = true;
    }

    widgets::section(tr.tr("movement.section.options").data());

    if (ImGui::Checkbox(tr.tr("movement.diagonal").data(),
                        &motion.diagonal_normalize)) {
        changed = true;
    }
    ImGui::TextDisabled("%s", tr.tr("movement.diagonal_hint").data());

    float scroll = static_cast<float>(config.scroll_lines_per_second);
    if (widgets::slider_row(tr, tr.tr("movement.scroll_speed").data(), &scroll,
                            1.0f, 60.0f, "%.0f lines/s", 8.0f)) {
        config.scroll_lines_per_second = scroll;
        changed = true;
    }

    widgets::section(tr.tr("movement.section.preview").data());

    // Effective speed readout at the start and at the end of the ramp, so the
    // effect of every slider is visible instead of being three abstract
    // numbers.
    const MotionInput plain{};
    MotionInput precision_input;
    precision_input.precision = true;
    MotionInput turbo_input;
    turbo_input.turbo = true;

    ImGui::Text("%s: %.0f px/s", tr.tr("movement.base_speed").data(),
                Motion::speed_at(motion, plain, 0.0));
    ImGui::Text("%s: %.0f px/s", tr.tr("movement.max_speed").data(),
                Motion::speed_at(motion, plain, motion.accel_ms));
    ImGui::Text("%s: %.0f px/s", tr.tr("movement.precision_speed").data(),
                Motion::speed_at(motion, precision_input, motion.accel_ms));
    ImGui::Text("%s: %.0f px/s", tr.tr("movement.turbo_speed").data(),
                Motion::speed_at(motion, turbo_input, motion.accel_ms));

    ImGui::Spacing();
    ImGui::TextDisabled("%s", tr.tr("movement.speed_keys_hint").data());

    if (changed) {
        *context.config_dirty = true;
    }
}

}  // namespace ui
}  // namespace km
