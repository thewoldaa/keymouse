// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/mouse_hook.h"

#include <atomic>
#include <thread>

#include <windows.h>

#include "input/injection.h"
#include "platform/timer.h"

namespace km {
namespace platform {
namespace {

std::atomic<MouseEventSink> g_sink{nullptr};
std::atomic<bool> g_running{false};
std::atomic<bool> g_stop_requested{false};
std::atomic<bool> g_finished{true};
std::atomic<DWORD> g_thread_id{0};
HHOOK g_hook = nullptr;

MacroInputEvent make_event(MacroInputEvent::Type type,
                           const MSLLHOOKSTRUCT& info) {
    MacroInputEvent event;
    event.type = type;
    event.injected = info.dwExtraInfo == kInjectionTag;
    event.time_us = monotonic_us();
    event.x = info.pt.x;
    event.y = info.pt.y;
    return event;
}

LRESULT CALLBACK mouse_hook_proc(int code, WPARAM wparam, LPARAM lparam) {
    if (code != HC_ACTION) {
        return CallNextHookEx(nullptr, code, wparam, lparam);
    }
    MouseEventSink sink = g_sink.load(std::memory_order_acquire);
    if (!sink) {
        return CallNextHookEx(nullptr, code, wparam, lparam);
    }

    const auto* info = reinterpret_cast<const MSLLHOOKSTRUCT*>(lparam);
    switch (wparam) {
        case WM_MOUSEMOVE:
            sink(make_event(MacroInputEvent::Type::MouseMove, *info));
            break;
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP: {
            MacroInputEvent event = make_event(
                (wparam & 1) != 0 ? MacroInputEvent::Type::ButtonDown
                                  : MacroInputEvent::Type::ButtonUp,
                *info);
            switch (wparam) {
                case WM_LBUTTONDOWN:
                case WM_LBUTTONUP:
                    event.data = static_cast<int32_t>(MouseButton::Left);
                    break;
                case WM_RBUTTONDOWN:
                case WM_RBUTTONUP:
                    event.data = static_cast<int32_t>(MouseButton::Right);
                    break;
                case WM_MBUTTONDOWN:
                case WM_MBUTTONUP:
                    event.data = static_cast<int32_t>(MouseButton::Middle);
                    break;
                default: {
                    const WORD button = HIWORD(info->mouseData);
                    event.data = static_cast<int32_t>(
                        button == XBUTTON1 ? MouseButton::X1 : MouseButton::X2);
                    break;
                }
            }
            sink(event);
            break;
        }
        case WM_MOUSEWHEEL: {
            MacroInputEvent event = make_event(
                MacroInputEvent::Type::WheelVertical, *info);
            event.data = GET_WHEEL_DELTA_WPARAM(info->mouseData);
            sink(event);
            break;
        }
        case WM_MOUSEHWHEEL: {
            MacroInputEvent event = make_event(
                MacroInputEvent::Type::WheelHorizontal, *info);
            event.data = GET_WHEEL_DELTA_WPARAM(info->mouseData);
            sink(event);
            break;
        }
        default:
            break;
    }

    // The hook never swallows anything: recording and playback are observers.
    return CallNextHookEx(nullptr, code, wparam, lparam);
}

void hook_thread_main() {
    g_thread_id.store(GetCurrentThreadId());

    // SetWindowsHookEx needs a message queue.
    MSG msg{};
    PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);

    g_hook = SetWindowsHookExW(WH_MOUSE_LL, &mouse_hook_proc,
                               GetModuleHandleW(nullptr), 0);
    g_running.store(g_hook != nullptr);
    if (!g_hook) {
        g_thread_id.store(0);
        g_finished.store(true);
        return;
    }

    while (!g_stop_requested.load()) {
        const BOOL result = GetMessageW(&msg, nullptr, 0, 0);
        if (result <= 0) {
            break;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    UnhookWindowsHookEx(g_hook);
    g_hook = nullptr;
    g_running.store(false);
    g_thread_id.store(0);
    g_finished.store(true);
}

}  // namespace

bool start_mouse_hook(MouseEventSink sink) {
    if (g_running.load()) {
        g_sink.store(sink, std::memory_order_release);
        return true;
    }

    g_sink.store(sink, std::memory_order_release);
    g_stop_requested.store(false);
    g_finished.store(false);
    std::thread(hook_thread_main).detach();

    for (int i = 0; i < 200; ++i) {
        if (g_running.load()) {
            return true;
        }
        if (g_finished.load()) {
            break;
        }
        Sleep(5);
    }
    return g_running.load();
}

void stop_mouse_hook() {
    g_stop_requested.store(true);
    const DWORD thread_id = g_thread_id.load();
    if (thread_id != 0) {
        PostThreadMessageW(thread_id, WM_QUIT, 0, 0);
        for (int i = 0; i < 100 && g_running.load(); ++i) {
            Sleep(5);
        }
    }
    g_sink.store(nullptr, std::memory_order_release);
}

bool mouse_hook_running() {
    return g_running.load();
}

}  // namespace platform
}  // namespace km
