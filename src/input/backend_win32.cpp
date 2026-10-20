// SPDX-License-Identifier: GPL-3.0-or-later
#include "input/backend_win32.h"

#include <atomic>
#include <mutex>
#include <thread>

#include <windows.h>

#include "core/chord.h"
#include "input/injection.h"
#include "input/keycodes_win32.h"
#include "platform/timer.h"

namespace km {
namespace {

// Set by the hook callback. A global is unavoidable here: the low-level hook
// is a C callback with no user data parameter, and only one hook can be
// installed per process for WH_KEYBOARD_LL.
std::atomic<InputBackend*> g_active_backend{nullptr};
std::atomic<EventSink*> g_active_sink{nullptr};

constexpr wchar_t kHookWindowClass[] = L"KeyMouseHookWindow";

}  // namespace

class Win32Backend final : public InputBackend {
public:
    ~Win32Backend() override { stop(); }

    bool start(EventSink sink) override {
        if (running_.load()) {
            return true;
        }

        sink_ = std::move(sink);
        g_active_sink.store(&sink_);

        // A restart after a session change reuses this object, so the previous
        // stop must not leak into the new thread.
        stop_requested_.store(false);
        hook_ready_.store(false);

        // The hook must be installed on a thread that pumps messages, so the
        // backend owns a dedicated thread rather than using the UI thread.
        hook_thread_ = std::thread([this] { hook_thread_main(); });

        // Wait for the hook to report success or failure before returning, so
        // the caller knows whether the app is actually usable.
        for (int i = 0; i < 200 && !hook_ready_.load(); ++i) {
            Sleep(5);
        }
        return running_.load();
    }

    void stop() override {
        if (!running_.load() && !hook_thread_.joinable()) {
            return;
        }

        stop_requested_.store(true);
        if (hook_thread_.joinable()) {
            // Nudge the message loop so it observes stop_requested_.
            PostThreadMessageW(hook_thread_id_.load(), WM_QUIT, 0, 0);
            hook_thread_.join();
        }

        fn_held_.store(false);
        g_active_sink.store(nullptr);
        g_active_backend.store(nullptr);
        running_.store(false);
        hook_ready_.store(false);
    }

    bool running() const override { return running_.load(); }

    void set_fn_layer_key(KeyId key) override {
        if (fn_layer_key_.exchange(key) != key) {
            // The key that used to be the Fn layer is probably not held any
            // more, and a stale "held" flag would make every other key look
            // like it had Fn pressed.
            fn_held_.store(false);
        }
    }

    void move(int32_t dx, int32_t dy) override {
        INPUT input{};
        input.type = INPUT_MOUSE;
        input.mi.dx = dx;
        input.mi.dy = dy;
        input.mi.dwFlags = MOUSEEVENTF_MOVE;
        input.mi.dwExtraInfo = injection_extra();
        SendInput(1, &input, sizeof(INPUT));
    }

    void button(MouseButton button, bool down) override {
        INPUT input{};
        input.type = INPUT_MOUSE;

        switch (button) {
            case MouseButton::Left:
                input.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
                break;
            case MouseButton::Right:
                input.mi.dwFlags = down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
                break;
            case MouseButton::Middle:
                input.mi.dwFlags = down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
                break;
            case MouseButton::X1:
            case MouseButton::X2:
                input.mi.dwFlags = down ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP;
                input.mi.mouseData = button == MouseButton::X1 ? XBUTTON1 : XBUTTON2;
                break;
        }
        input.mi.dwExtraInfo = injection_extra();
        SendInput(1, &input, sizeof(INPUT));
    }

    void scroll(Axis axis, int32_t delta) override {
        INPUT input{};
        input.type = INPUT_MOUSE;
        if (axis == Axis::Vertical) {
            input.mi.dwFlags = MOUSEEVENTF_WHEEL;
        } else {
            input.mi.dwFlags = MOUSEEVENTF_HWHEEL;
        }
        input.mi.mouseData = static_cast<DWORD>(delta);
        input.mi.dwExtraInfo = injection_extra();
        SendInput(1, &input, sizeof(INPUT));
    }

    void key(KeyId target, bool down) override {
        INPUT input{};
        input.type = INPUT_KEYBOARD;

        const win32::KeyTranslation translation = win32::to_scan_code(target);
        if (translation.key != key::Invalid) {
            input.ki.wScan = static_cast<WORD>(translation.scan);
            input.ki.dwFlags = KEYEVENTF_SCANCODE;
            if (translation.extended) {
                input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
            }
        } else if (const auto vk = win32::to_virtual_key(target); vk.has_value()) {
            // Media keys and the extended function keys have no scan code in
            // our table; a virtual-key injection still reproduces them.
            input.ki.wVk = static_cast<WORD>(*vk);
        } else {
            return;
        }

        if (!down) {
            input.ki.dwFlags |= KEYEVENTF_KEYUP;
        }
        input.ki.dwExtraInfo = injection_extra();
        SendInput(1, &input, sizeof(INPUT));
    }

    void set_injection_tag(bool tagged) override { tag_injections_ = tagged; }

    void move_to(int32_t x, int32_t y) override {
        SetCursorPos(x, y);
    }

    bool cursor_position(int32_t& x, int32_t& y) override {
        POINT point{};
        if (!GetCursorPos(&point)) {
            return false;
        }
        x = point.x;
        y = point.y;
        return true;
    }

    bool screen_bounds(int32_t& left, int32_t& top, int32_t& right,
                       int32_t& bottom) override {
        left = GetSystemMetrics(SM_XVIRTUALSCREEN);
        top = GetSystemMetrics(SM_YVIRTUALSCREEN);
        right = left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
        bottom = top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
        return right > left && bottom > top;
    }

    uint64_t dropped_events() const override { return dropped_.load(); }

    ULONG_PTR injection_extra() const {
        return tag_injections_ ? static_cast<ULONG_PTR>(kInjectionTag) : 0;
    }

    std::atomic<bool> tag_injections_{false};

    const char* name() const override { return "Win32 low-level hook"; }

    // Called from the hook callback.
    bool dispatch(const InputEvent& event) {
        EventSink* sink = g_active_sink.load(std::memory_order_acquire);
        if (!sink || !*sink) {
            return false;
        }
        return (*sink)(event);
    }

private:
    void hook_thread_main() {
        hook_thread_id_.store(GetCurrentThreadId());

        // A message queue must exist before SetWindowsHookEx can post to it.
        MSG msg{};
        PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);

        hook_ = SetWindowsHookExW(WH_KEYBOARD_LL, &Win32Backend::hook_proc,
                                  GetModuleHandleW(nullptr), 0);

        if (!hook_) {
            hook_ready_.store(true);
            running_.store(false);
            return;
        }

        g_active_backend.store(this);
        running_.store(true);
        hook_ready_.store(true);

        while (!stop_requested_.load()) {
            const BOOL result = GetMessageW(&msg, nullptr, 0, 0);
            if (result <= 0) {
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        UnhookWindowsHookEx(hook_);
        hook_ = nullptr;
        running_.store(false);
    }

    static LRESULT CALLBACK hook_proc(int code, WPARAM wparam, LPARAM lparam) {
        if (code != HC_ACTION) {
            return CallNextHookEx(nullptr, code, wparam, lparam);
        }

        Win32Backend* backend = static_cast<Win32Backend*>(
            g_active_backend.load(std::memory_order_acquire));
        if (!backend) {
            return CallNextHookEx(nullptr, code, wparam, lparam);
        }

        const auto* info = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lparam);

        // KeyMouse's own injected keyboard events (macro playback, keyboard
        // steps) carry a tag and are passed straight through: interpreting them
        // would let a macro fire bindings or record itself. Third-party
        // injected keystrokes are still interpreted on purpose, so a remapper,
        // a macro pad, or the on-screen keyboard keeps working.
        if (info->dwExtraInfo == kInjectionTag) {
            return CallNextHookEx(nullptr, code, wparam, lparam);
        }

        // Ctrl+Alt+Del and friends are delivered on the secure desktop and are
        // never routed through this hook, but Win+L, Win+D, and Win+Tab are.
        // Let the OS keep them: the user holds Win first and then presses the
        // second key, so the decision has to be made when that second key
        // arrives, not when Win itself does. GetAsyncKeyState must be masked
        // with 0x8000; the low bit only means "pressed since the last call".
        const bool win_down =
            (GetAsyncKeyState(VK_LWIN) & 0x8000) != 0 ||
            (GetAsyncKeyState(VK_RWIN) & 0x8000) != 0;
        if (win_down &&
            (info->vkCode == VK_TAB || info->vkCode == 'L' || info->vkCode == 'D')) {
            return CallNextHookEx(nullptr, code, wparam, lparam);
        }

        const bool is_down = wparam == WM_KEYDOWN || wparam == WM_SYSKEYDOWN;
        const bool extended = (info->flags & LLKHF_EXTENDED) != 0;

        const win32::KeyTranslation translated =
            win32::from_scan_code(info->scanCode, extended, info->vkCode);

        if (translated.key == key::Invalid) {
            return CallNextHookEx(nullptr, code, wparam, lparam);
        }

        // The Fn layer key is an ordinary key the user nominated to act as a
        // modifier. Its state is tracked from these events rather than read
        // with GetAsyncKeyState, because which key it is only known at runtime.
        if (translated.key == backend->fn_layer_key()) {
            backend->set_fn_held(is_down);
        }

        InputEvent event;
        event.type = is_down ? InputEvent::Type::KeyDown : InputEvent::Type::KeyUp;
        event.key = translated.key;
        event.mods = current_modifiers();
        if (backend->fn_held() && translated.key != backend->fn_layer_key()) {
            event.mods |= ModFn;
        }
        event.extra_info = info->dwExtraInfo;
        event.time_us = platform::monotonic_us();

        const bool swallow = backend->dispatch(event);
        if (swallow) {
            return 1;
        }
        return CallNextHookEx(nullptr, code, wparam, lparam);
    }

    // Reads modifier state directly from the OS. Using GetAsyncKeyState rather
    // than tracking key events means the mask stays correct even if the user
    // pressed Ctrl before the app started.
    static ModMask current_modifiers() {
        ModMask mods = ModNone;
        if (GetAsyncKeyState(VK_CONTROL) & 0x8000) mods |= ModCtrl;
        if (GetAsyncKeyState(VK_MENU) & 0x8000) mods |= ModAlt;
        if (GetAsyncKeyState(VK_SHIFT) & 0x8000) mods |= ModShift;
        if (GetAsyncKeyState(VK_LWIN) & 0x8000) mods |= ModWin;
        if (GetAsyncKeyState(VK_RWIN) & 0x8000) mods |= ModWin;
        return mods;
    }

    // The Fn layer key is an ordinary key the user nominated to act as a
    // modifier. Read by the hook callback on the hook thread.
    KeyId fn_layer_key() const {
        return static_cast<KeyId>(fn_layer_key_.load(std::memory_order_relaxed));
    }
    bool fn_held() const { return fn_held_.load(std::memory_order_relaxed); }
    void set_fn_held(bool held) { fn_held_.store(held, std::memory_order_relaxed); }

    EventSink sink_;
    std::thread hook_thread_;
    std::atomic<DWORD> hook_thread_id_{0};
    HHOOK hook_ = nullptr;
    std::atomic<bool> running_{false};
    std::atomic<bool> hook_ready_{false};
    std::atomic<bool> stop_requested_{false};
    std::atomic<uint64_t> dropped_{0};
    std::atomic<uint16_t> fn_layer_key_{key::Invalid};
    std::atomic<bool> fn_held_{false};
};

std::unique_ptr<InputBackend> make_win32_backend() {
    return std::make_unique<Win32Backend>();
}

}  // namespace km
