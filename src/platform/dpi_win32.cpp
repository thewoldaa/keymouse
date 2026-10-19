// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/dpi.h"

#include <windows.h>

namespace km {
namespace dpi {

float scale_for_window(void* window) {
    const UINT dpi = GetDpiForWindow(static_cast<HWND>(window));
    return dpi == 0 ? 1.0f : static_cast<float>(dpi) / 96.0f;
}

void enable_awareness() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
}

}  // namespace dpi
}  // namespace km