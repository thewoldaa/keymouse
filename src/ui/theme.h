// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <imgui.h>

namespace km {
namespace ui {

// Applies the KeyMouse colour scheme and metrics. Called once after the ImGui
// context is created and again whenever the DPI scale changes.
void apply_theme(float dpi_scale);

// Colour constants used outside the theme (tray status, charts).
namespace colors {
constexpr ImU32 kAccent = IM_COL32(0x4C, 0xC2, 0xA6, 0xFF);
constexpr ImU32 kWarning = IM_COL32(0xE8, 0xB3, 0x39, 0xFF);
constexpr ImU32 kDanger = IM_COL32(0xE5, 0x6B, 0x6B, 0xFF);
constexpr ImU32 kMuted = IM_COL32(0x8A, 0x92, 0xA0, 0xFF);
}  // namespace colors

}  // namespace ui
}  // namespace km
