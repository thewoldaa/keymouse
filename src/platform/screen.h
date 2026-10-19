// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

namespace km {

struct MonitorRect {
    int32_t left = 0;
    int32_t top = 0;
    int32_t right = 0;
    int32_t bottom = 0;

    int32_t width() const { return right - left; }
    int32_t height() const { return bottom - top; }
    int32_t center_x() const { return left + width() / 2; }
    int32_t center_y() const { return top + height() / 2; }
};

namespace screen {

// Bounding box of the virtual desktop across all monitors, in physical pixels.
MonitorRect virtual_desktop();

// Work area of the monitor containing the cursor, excluding the taskbar.
MonitorRect current_work_area();

// Moves the cursor to the centre of the monitor it is currently on.
bool center_cursor();

// Cursor position in virtual desktop coordinates.
bool cursor_position(int32_t& x, int32_t& y);

// True when the process is running with an elevated token. Input injection
// into elevated windows is blocked by UIPI for non-elevated processes, so the
// dashboard surfaces this as a hint.
bool is_elevated();

// Name of the executable owning the foreground window, lowercased. Empty when
// there is no foreground window.
std::string foreground_exe_name();

// Per-monitor DPI awareness. Must be called before any window is created.
void enable_dpi_awareness();

}  // namespace screen

}  // namespace km
