// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/mouse_hook.h"

#include <atomic>
#include <mutex>
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
std::atomic<DWORD> g_thread_id{0};
HHOOK g_hook = nullptr;

// Serializes the hook lifecycle. Starting and stopping happen on the UI thread,
// but the thread's own cleanup writes g_hook and g_running, so a start that
// follows a stop must not observe the previous thread half torn down.
std::mutex g_lifecycle_mutex;
std::thread g_thread;
std::atomic<bool> g_installed{false};

// Button state read from the OS at the moment of the event, not inferred from
// the stream the hook has seen. The recorder needs this because a recording can
// start while a button is already held — pressing LMB to look around and then
// pressing Record is the normal way to capture a camera drag — and a stream
// that never saw the press cannot tell that the drag is in progress.
//
// GetAsyncKeyState is cheap enough for a low-level hook and is the only source
// that includes buttons whose press predates the hook. The low bit ("pressed
// since the last call") is ignored; 0x8000 is "currently down".
uint32_t physical_buttons() {
    uint32_t mask = 0;
    const auto bit = [&mask](MouseButton button) {
        mask |= 1u << static_cast<unsigned>(button);
    };
    if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) bit(MouseButton::Left);
    if (GetAsyncKeyState(VK_RBUTTON) & 0x8000) bit(MouseButton::Right);
    if (GetAsyncKeyState(VK_MBUTTON) & 0x8000) bit(MouseButton::Middle);
    if (GetAsyncKeyState(VK_XBUTTON1) & 0x8000) bit(MouseButton::X1);
    if (GetAsyncKeyState(VK_XBUTTON2) & 0x8000) bit(MouseButton::X2);
    return mask;
}

MacroInputEvent make_event(MacroInputEvent::Type type,
                           const MSLLHOOKSTRUCT& info) {
    MacroInputEvent event;
    event.type = type;
    event.injected = info.dwExtraInfo == kInjectionTag;
    event.time_us = monotonic_us();
    event.x = info.pt.x;
    event.y = info.pt.y;
    // Only movement needs the snapshot: a button event names its own button and
    // the recorder tracks the rest from the stream, so sampling five keys on
    // every click and wheel notch would be work without a reader.
    if (type == MacroInputEvent::Type::MouseMove) {
        event.buttons_down = physical_buttons();
        event.buttons_known = true;
    }
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
    g_installed.store(g_hook != nullptr);
    g_running.store(g_hook != nullptr);
    if (!g_hook) {
        g_thread_id.store(0);
        return;
    }

    while (!g_stop_requested.load()) {
        // A timed wait rather than a blocking GetMessage: stop_mouse_hook()
        // joins this thread, and a WM_QUIT posted before the message queue
        // existed is lost. Waking up a few times a second guarantees the stop
        // request is observed no matter when it arrives. The thread is idle
        // otherwise, and the hook callback itself runs on Windows' hook
        // dispatch, not on this loop.
        const DWORD wait = MsgWaitForMultipleObjectsEx(
            0, nullptr, 100, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (wait != WAIT_OBJECT_0) {
            continue;
        }
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                g_stop_requested.store(true);
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    UnhookWindowsHookEx(g_hook);
    g_hook = nullptr;
    g_running.store(false);
    g_thread_id.store(0);
}

}  // namespace

bool start_mouse_hook(MouseEventSink sink) {
    std::lock_guard<std::mutex> lock(g_lifecycle_mutex);

    if (g_running.load()) {
        g_sink.store(sink, std::memory_order_release);
        return true;
    }

    // A previous run that exited on its own (the hook failed to install, or
    // the message loop ended) still owns a joinable thread. Reap it before
    // spawning a new one.
    if (g_thread.joinable()) {
        g_thread.join();
    }

    g_sink.store(sink, std::memory_order_release);
    g_stop_requested.store(false);
    g_installed.store(false);
    g_thread = std::thread(hook_thread_main);

    for (int i = 0; i < 200; ++i) {
        if (g_running.load()) {
            return true;
        }
        if (g_installed.load() && !g_running.load()) {
            // The thread ran and failed to install the hook.
            break;
        }
        Sleep(5);
    }
    return g_running.load();
}

void stop_mouse_hook() {
    std::lock_guard<std::mutex> lock(g_lifecycle_mutex);

    g_stop_requested.store(true);
    const DWORD thread_id = g_thread_id.load();
    if (thread_id != 0) {
        PostThreadMessageW(thread_id, WM_QUIT, 0, 0);
    }
    // Join rather than poll: the thread clears g_hook and g_running as its
    // last act, so after the join the globals are settled and a subsequent
    // start cannot race with the old thread's cleanup.
    if (g_thread.joinable()) {
        g_thread.join();
    }
    g_sink.store(nullptr, std::memory_order_release);
}

bool mouse_hook_running() {
    return g_running.load();
}

}  // namespace platform
}  // namespace km
