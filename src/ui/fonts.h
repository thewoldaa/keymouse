// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <imgui.h>

namespace km {
namespace ui {

// Builds the ImGui font atlas.
//
// Fonts are loaded from the system rather than bundled. Bundling Segoe UI plus
// a Japanese font would add roughly 8 MB to a 3 MB binary; loading from
// C:\Windows\Fonts costs nothing and every supported Windows install has them.
//
// Segoe UI covers Latin and Cyrillic. Yu Gothic covers Japanese and is merged
// into the same atlas so a mixed-language UI renders correctly.
void load_fonts(float dpi_scale);

// Rebuilds the atlas after a DPI change. Must be called between frames.
void rebuild_fonts(float dpi_scale);

}  // namespace ui
}  // namespace km
