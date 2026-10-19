// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace km {
namespace dpi {

// Scale factor for a window's monitor (1.0 at 96 DPI, 1.5 at 144 DPI).
float scale_for_window(void* window_handle);

// Enables per-monitor v2 awareness. Must be called before any window exists.
void enable_awareness();

}  // namespace dpi
}  // namespace km
