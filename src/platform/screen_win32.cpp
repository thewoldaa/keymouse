// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/screen.h"

#include <windows.h>
#include <shellapi.h>

namespace km {
namespace screen {
namespace {

MonitorRect from_rect(const RECT& rect) {
    return MonitorRect{rect.left, rect.top, rect.right, rect.bottom};
}

}  // namespace

MonitorRect virtual_desktop() {
    return MonitorRect{
        GetSystemMetrics(SM_XVIRTUALSCREEN),
        GetSystemMetrics(SM_YVIRTUALSCREEN),
        GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN),
        GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN),
    };
}

MonitorRect current_work_area() {
    POINT cursor{};
    if (!GetCursorPos(&cursor)) {
        return virtual_desktop();
    }

    const HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (monitor && GetMonitorInfoW(monitor, &info)) {
        return from_rect(info.rcWork);
    }
    return virtual_desktop();
}

bool center_cursor() {
    POINT cursor{};
    if (!GetCursorPos(&cursor)) {
        return false;
    }

    const HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!monitor || !GetMonitorInfoW(monitor, &info)) {
        return false;
    }

    const int x = info.rcWork.left + (info.rcWork.right - info.rcWork.left) / 2;
    const int y = info.rcWork.top + (info.rcWork.bottom - info.rcWork.top) / 2;
    return SetCursorPos(x, y) != FALSE;
}

bool cursor_position(int32_t& x, int32_t& y) {
    POINT cursor{};
    if (!GetCursorPos(&cursor)) {
        return false;
    }
    x = cursor.x;
    y = cursor.y;
    return true;
}

bool is_elevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return false;
    }

    TOKEN_ELEVATION elevation{};
    DWORD size = sizeof(elevation);
    const bool elevated = GetTokenInformation(token, TokenElevation, &elevation,
                                              size, &size) != FALSE &&
                          elevation.TokenIsElevated != 0;
    CloseHandle(token);
    return elevated;
}

std::string foreground_exe_name() {
    const HWND window = GetForegroundWindow();
    if (!window) {
        return {};
    }

    DWORD process_id = 0;
    GetWindowThreadProcessId(window, &process_id);
    if (process_id == 0) {
        return {};
    }

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id);
    if (!process) {
        return {};
    }

    wchar_t buffer[MAX_PATH]{};
    DWORD size = MAX_PATH;
    std::string result;
    if (QueryFullProcessImageNameW(process, 0, buffer, &size)) {
        const wchar_t* name = wcsrchr(buffer, L'\\');
        name = name ? name + 1 : buffer;

        const int needed = WideCharToMultiByte(CP_UTF8, 0, name, -1, nullptr, 0, nullptr, nullptr);
        if (needed > 1) {
            result.resize(static_cast<size_t>(needed - 1));
            WideCharToMultiByte(CP_UTF8, 0, name, -1, result.data(), needed, nullptr, nullptr);
            for (char& c : result) {
                c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
            }
        }
    }

    CloseHandle(process);
    return result;
}

void enable_dpi_awareness() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
}

}  // namespace screen
}  // namespace km
