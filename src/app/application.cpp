// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/application.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <d3d11.h>
#include <dwmapi.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <shellapi.h>
#include <wtsapi32.h>

#include "app/single_instance.h"
#include "core/macro_store.h"
#include "core/profiles.h"
#include "core/version.h"
#include "input/backend_win32.h"
#include "platform/beep.h"
#include "platform/dpi.h"
#include "platform/file_dialog.h"
#include "platform/locale.h"
#include "platform/mouse_hook.h"
#include "platform/paths.h"
#include "platform/screen.h"
#include "platform/startup.h"
#include "platform/timer.h"
#include "ui/fonts.h"
#include "ui/theme.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg,
                                                             WPARAM wparam, LPARAM lparam);

namespace km {
namespace {

constexpr wchar_t kWindowClass[] = L"KeyMouseDashboard";
constexpr wchar_t kWindowTitle[] = L"KeyMouse";

// The engine thread runs at 500 Hz: the period it aims for, and the longest
// step it will integrate in one go. The latter bounds the jump produced when
// the loop is starved, for example across a resume from standby.
constexpr auto kEnginePeriod = std::chrono::microseconds(2000);
constexpr double kMaxTickSeconds = 0.05;

// Icon resource id, defined in resources/keymouse.rc.
constexpr int kIconResourceId = 101;

// Polling the foreground window costs a process query, so it runs at a rate a
// human can perceive rather than once per frame.
constexpr auto kForegroundPollPeriod = std::chrono::milliseconds(200);

Application* g_app = nullptr;

bool register_window_class(HINSTANCE instance) {
    // Falls back to the generic application icon if the resource is missing,
    // rather than handing Windows a null icon.
    HICON icon = LoadIconW(instance, MAKEINTRESOURCEW(kIconResourceId));
    if (!icon) {
        icon = LoadIconW(nullptr, IDI_APPLICATION);
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = Application::window_proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kWindowClass;
    wc.hIcon = icon;
    wc.hIconSm = icon;

    if (RegisterClassExW(&wc) == 0) {
        return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }
    return true;
}

}  // namespace

Application::Application() = default;

Application::~Application() {
    shutdown();
}

int Application::run(int argc, wchar_t** argv) {
    if (!initialize(argc, argv)) {
        return 1;
    }

    // Render loop. When the dashboard is hidden the loop still runs so the
    // engine thread can be fed, the tray keeps responding, and a hotkey can
    // bring the window back; it just renders nothing.
    MSG msg{};
    bool running = true;

    while (running) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!running) {
            break;
        }

        if (request_exit_) {
            break;
        }
        if (request_hide_) {
            hide_dashboard();
            request_hide_ = false;
        }

        // Engine state first: a binding may have toggled KeyMouse or chosen a
        // new speed, and the UI must show what is actually active.
        sync_from_engine();

        if (window_visible_) {
            render_frame();
            // ~60 FPS cap. A swap chain with vsync stalls the message loop when
            // the window is occluded, so the pacing is done here.
            Sleep(16);
        } else {
            Sleep(50);
        }

        apply_pending_requests();
        poll_foreground();
        sync_macro_mouse_hook();

        // A macro edited in the dashboard is written back once per iteration,
        // never per keystroke.
        if (dashboard_.macro_edit.dirty) {
            dashboard_.macro_edit.dirty = false;
            save_macro_edit();
        }

        // Persist at most once per iteration, never per widget edit.
        if (config_dirty_) {
            config_dirty_ = false;
            apply_config_to_engine();
            save_config();
        }
    }

    shutdown();
    return 0;
}

bool Application::initialize(int argc, wchar_t** argv) {
    g_app = this;
    screen::enable_dpi_awareness();

    std::string error;
    if (!CliOptions::parse(argc, argv, options_, error)) {
        MessageBoxA(nullptr, error.c_str(), "KeyMouse", MB_ICONERROR);
        return false;
    }
    if (options_.show_help) {
        MessageBoxA(nullptr, usage_text().c_str(), "KeyMouse", MB_OK);
        return false;
    }
    if (options_.show_version) {
        const std::string text = std::string("KeyMouse ") + version::kVersion;
        MessageBoxA(nullptr, text.c_str(), "KeyMouse", MB_OK);
        return false;
    }

    // A second instance must not install a second hook: two hooks would both
    // swallow keys and every action would fire twice. Signal the first
    // instance and exit instead.
    if (!single_instance::acquire()) {
        single_instance::send_command("show");
        return false;
    }
    single_instance::set_command_handler([](const std::string& command) {
        if (g_app) {
            g_app->on_command(command);
        }
    });

    paths::ensure_data_dir();
    load_config();

    if (options_.safe_mode) {
        // Safe mode ignores the saved configuration entirely, including
        // bindings that might be what made the user reach for it.
        config_ = make_default_config();
        config_.enabled = false;
    }

    if (first_run_) {
        // Pick up the language the machine is set to, or the one the installer
        // wizard was run in, rather than always starting in English. Only on a
        // first run: a language chosen in the dashboard is never overridden.
        // language_code() normalises an unknown code to "en" so a mistyped
        // --locale cannot be stored and then silently ignored on every load.
        config_.locale =
            options_.locale.empty()
                ? platform::system_locale()
                : std::string(language_code(language_from_code(options_.locale)));
    }

    if (options_.no_autostart) {
        // Passed by the installer when the user declined "start with Windows".
        // The registry is left untouched and the answer is remembered, so the
        // General tab and the saved config agree with what the installer did.
        config_.start_with_windows = false;
        config_dirty_ = true;
    } else {
        if (first_run_) {
            // The default configuration asks to run at login, so register that
            // once, now. Afterwards the registry is the source of truth and the
            // config only mirrors it: a user who deletes the Run entry by hand
            // has made a decision, and the next launch should not silently undo
            // it.
            startup::set_enabled(config_.start_with_windows);

            // Write the file out at shutdown, so the next launch is an ordinary
            // one instead of a first run again.
            config_dirty_ = true;
        }
        config_.start_with_windows = startup::is_enabled();
    }

    i18n_.load(language_from_code(config_.locale), paths::locale_dir().string());

    backend_ = make_win32_backend();
    engine_ = std::make_unique<Engine>(*backend_);
    engine_->apply_config(config_);
    backend_->set_fn_layer_key(config_.fn_layer_key);

    // The saved "start enabled" preference arms mouse mode before the first
    // key event. Starting the app must not beep, so drop the feedback request.
    engine_->set_mouse_mode(config_.mouse_mode.start_enabled);
    engine_->consume_beep_request();

    if (!options_.safe_mode && !install_hook()) {
        MessageBoxW(nullptr,
            L"KeyMouse could not install its keyboard hook.\n"
            L"Another application may be blocking it, or the desktop is locked.",
            L"KeyMouse", MB_ICONWARNING);
    }

    if (!create_window()) {
        return false;
    }

    // The hook is dropped by Windows while the workstation is locked, so the
    // application must be told when the user comes back.
    session::subscribe(window_, &Application::session_callback);

    tray_ = std::make_unique<ui::TrayIcon>();
    ui::TrayIcon::Callbacks callbacks;
    callbacks.on_show = [this] { show_dashboard(); };
    callbacks.on_toggle = [this] { toggle_enabled(); };
    callbacks.on_mouse_mode = [this] {
        if (engine_) {
            engine_->set_mouse_mode(!engine_->mouse_mode());
        }
    };
    callbacks.on_exit = [this] { request_exit_ = true; PostQuitMessage(0); };
    callbacks.on_startup_changed = [this](bool enabled) {
        startup::set_enabled(enabled);
        config_.start_with_windows = enabled;
        config_dirty_ = true;
    };
    callbacks.get_startup = [] { return startup::is_enabled(); };
    callbacks.get_enabled = [this] { return config_.enabled; };
    callbacks.get_mouse_mode = [this] { return engine_ && engine_->mouse_mode(); };
    tray_->set_callbacks(callbacks);
    tray_->set_i18n(&i18n_);
    sync_tray_visibility();

    dashboard_.config = &config_;
    dashboard_.engine = engine_.get();
    dashboard_.i18n = &i18n_;
    dashboard_.config_dirty = &config_dirty_;
    dashboard_.request_hide = &request_hide_;
    dashboard_.request_exit = &request_exit_;
    dashboard_.request_locale_reload = &request_locale_reload_;
    dashboard_.request_open_config = &request_open_config_;

    // Macro commands. The tab never touches files or the engine's playback
    // state directly; everything goes through the application.
    dashboard_.macro_toggle_record = [this] { toggle_macro_recording(); };
    dashboard_.macro_play = [this] { play_loaded_macro(false); };
    dashboard_.macro_play_from_step = [this] { play_loaded_macro(true); };
    dashboard_.macro_pause = [this] {
        if (engine_) {
            engine_->request_macro_pause();
        }
    };
    dashboard_.macro_resume = [this] {
        if (engine_) {
            engine_->request_macro_resume();
        }
    };
    dashboard_.macro_stop = [this] {
        if (engine_) {
            engine_->request_macro_stop();
        }
    };
    dashboard_.macro_select = [this](const std::string& id) { load_macro_into_editor(id); };
    dashboard_.macro_new = [this] { new_macro(); };
    dashboard_.macro_duplicate = [this](const std::string& id) { duplicate_macro(id); };
    dashboard_.macro_delete = [this](const std::string& id) { delete_macro(id); };
    dashboard_.macro_import = [this] { import_macro(); };
    dashboard_.macro_export = [this](const std::string& id) { export_macro(id); };
    dashboard_.macro_set_hotkey = [this](const std::string& id, const Chord& chord) {
        set_macro_hotkey(id, chord);
    };

    // Open the first macro so Play and the playback hotkey work without a
    // detour through the library.
    if (!config_.macros.entries.empty()) {
        load_macro_into_editor(config_.macros.entries.front().id);
    }

    engine_stop_.store(false);
    engine_thread_ = std::thread([this] { engine_thread_main(); });

    // A first launch that vanished straight into the tray would look like a
    // failed install. Show the dashboard once; from the second launch on, the
    // saved preference wins.
    if (options_.minimized || (config_.start_minimized && !first_run_)) {
        hide_dashboard();
    } else {
        show_dashboard();
    }

    initialized_ = true;
    return true;
}

void Application::shutdown() {
    if (!initialized_) {
        return;
    }
    initialized_ = false;

    engine_stop_.store(true);
    if (engine_thread_.joinable()) {
        engine_thread_.join();
    }

    // The mouse hook feeds the engine, so it must be gone before the engine is
    // released.
    platform::stop_mouse_hook();

    // The engine thread is gone, so the engine's state is ours to touch again.
    if (engine_) {
        engine_->release_all();
    }
    if (backend_) {
        backend_->stop();
    }
    if (config_dirty_) {
        save_config();
    }

    if (window_) {
        session::unsubscribe(window_);
    }

    if (tray_) {
        tray_->destroy();
        tray_.reset();
        tray_visible_ = false;
    }

    destroy_device();
    if (window_) {
        DestroyWindow(window_);
        window_ = nullptr;
    }

    single_instance::release();
    g_app = nullptr;
}

bool Application::create_window() {
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    if (!register_window_class(instance)) {
        return false;
    }

    // Start hidden; show_dashboard() positions and reveals it. Creating the
    // window hidden avoids a visible flash at the default position.
    window_ = CreateWindowExW(
        WS_EX_APPWINDOW,
        kWindowClass, kWindowTitle,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 980, 700,
        nullptr, nullptr, instance, this);

    if (!window_) {
        return false;
    }

    // Size for the monitor's DPI and keep the window inside the work area. On a
    // small screen or a scaled display the nominal 980x700 would otherwise be
    // either too small for the fonts or partly below the taskbar.
    dpi_scale_ = dpi::scale_for_window(window_);
    RECT work{};
    if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0)) {
        const int max_width = work.right - work.left - 32;
        const int max_height = work.bottom - work.top - 32;
        const int width = std::clamp(
            static_cast<int>(980.0f * dpi_scale_), 320, std::max(max_width, 320));
        const int height = std::clamp(
            static_cast<int>(700.0f * dpi_scale_), 240, std::max(max_height, 240));
        const int x = work.left + (work.right - work.left - width) / 2;
        const int y = work.top + (work.bottom - work.top - height) / 2;
        SetWindowPos(window_, nullptr, x, y, width, height,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }

    // Dark title bar, which is otherwise white and jarring next to the theme.
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(window_, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */,
                          &dark, sizeof(dark));

    return create_device();
}

bool Application::create_device() {
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferCount = 2;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferDesc.RefreshRate.Numerator = 60;
    desc.BufferDesc.RefreshRate.Denominator = 1;
    desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = window_;
    desc.SampleDesc.Count = 1;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    const D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_0,
    };
    D3D_FEATURE_LEVEL obtained{};

    if (D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
            levels, static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION,
            &desc, &swap_chain_, &device_, &obtained, &context_) != S_OK) {
        return false;
    }

    ID3D11Texture2D* back_buffer = nullptr;
    if (swap_chain_->GetBuffer(0, IID_PPV_ARGS(&back_buffer)) != S_OK) {
        return false;
    }
    const HRESULT hr = device_->CreateRenderTargetView(back_buffer, nullptr, &render_target_);
    back_buffer->Release();
    if (hr != S_OK) {
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;  // no imgui.ini next to the exe
    ImGui::GetIO().LogFilename = nullptr;

    ui::apply_theme(dpi_scale_);
    ui::load_fonts(dpi_scale_);

    ImGui_ImplWin32_Init(window_);
    ImGui_ImplDX11_Init(device_, context_);
    return true;
}

void Application::destroy_device() {
    if (ImGui::GetCurrentContext()) {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }
    if (render_target_) {
        render_target_->Release();
        render_target_ = nullptr;
    }
    if (swap_chain_) {
        swap_chain_->Release();
        swap_chain_ = nullptr;
    }
    if (context_) {
        context_->Release();
        context_ = nullptr;
    }
    if (device_) {
        device_->Release();
        device_ = nullptr;
    }
}

void Application::render_frame() {
    if (!window_visible_ || !swap_chain_) {
        return;
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    // Refresh live status once per frame rather than per tab, so all tabs show
    // a consistent snapshot.
    dashboard_.hook_running = backend_ && backend_->running();
    dashboard_.elevated = screen::is_elevated();
    dashboard_.foreground_exe = foreground_exe_;
    dashboard_.stats = engine_ ? engine_->stats() : EngineStats{};
    dashboard_.active_mask = engine_ ? engine_->active_mask() : 0u;
    dashboard_.mouse_mode = engine_ && engine_->mouse_mode();
    dashboard_.auto_click_remaining_ms =
        engine_ && engine_->enabled() ? engine_->auto_click_remaining_ms() : -1;
    dashboard_.auto_click_fires = engine_ ? engine_->auto_click_fires() : 0;
    dashboard_.capture_active = false;

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("##root", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoBringToFrontOnFocus |
                 ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoScrollWithMouse);

    // Header: title, version, and the enable switch.
    ImGui::TextUnformatted(i18n_.tr("app.name").data());
    ImGui::SameLine();
    ImGui::TextDisabled("| %s", version::kVersion);

    ImGui::SameLine();
    const bool mouse_mode = engine_ && engine_->mouse_mode();
    const std::string mouse_label = std::string(i18n_.tr("status.mouse_mode").data()) +
                                    ": " +
                                    (mouse_mode ? i18n_.tr("common.yes").data()
                                                : i18n_.tr("common.no").data());
    if (mouse_mode) {
        ImGui::PushStyleColor(ImGuiCol_Button,
                              ImGui::ColorConvertU32ToFloat4(ui::colors::kAccent));
    }
    if (ImGui::SmallButton(mouse_label.c_str())) {
        if (engine_) {
            engine_->set_mouse_mode(!mouse_mode);
        }
    }
    if (mouse_mode) {
        ImGui::PopStyleColor();
    }

    const char* enabled_label = i18n_.tr("status.enabled").data();
    const float switch_width = ImGui::GetFrameHeight() +
                               ImGui::CalcTextSize(enabled_label).x +
                               ImGui::GetStyle().ItemInnerSpacing.x;
    ImGui::SameLine(ImGui::GetWindowWidth() - switch_width -
                    ImGui::GetStyle().WindowPadding.x);
    bool enabled = config_.enabled;
    if (ImGui::Checkbox(enabled_label, &enabled)) {
        set_enabled(enabled);
    }

    ImGui::Separator();

    // Keep the header and the tab bar fixed. A tab that is taller than the
    // window scrolls inside this child instead of moving the whole dashboard,
    // which otherwise hides the only way back to another tab.
    const auto draw_tab_body = [](const char* id, const auto& draw) {
        ImGui::BeginChild(id, ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
                          ImGuiWindowFlags_None);
        draw();
        ImGui::EndChild();
    };

    if (ImGui::BeginTabBar("tabs")) {
        if (ImGui::BeginTabItem(i18n_.tr("tab.status").data())) {
            draw_tab_body("status_body", [&] { ui::draw_status_tab(dashboard_); });
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(i18n_.tr("tab.bindings").data())) {
            draw_tab_body("bindings_body", [&] { ui::draw_bindings_tab(dashboard_); });
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(i18n_.tr("tab.mouse_mode").data())) {
            draw_tab_body("mouse_mode_body", [&] { ui::draw_mouse_mode_tab(dashboard_); });
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(i18n_.tr("tab.movement").data())) {
            draw_tab_body("movement_body", [&] { ui::draw_movement_tab(dashboard_); });
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(i18n_.tr("tab.auto_click").data())) {
            draw_tab_body("auto_click_body", [&] { ui::draw_auto_click_tab(dashboard_); });
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(i18n_.tr("tab.macros").data())) {
            draw_tab_body("macros_body", [&] { ui::draw_macros_tab(dashboard_); });
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(i18n_.tr("tab.profiles").data())) {
            draw_tab_body("profiles_body", [&] { ui::draw_profiles_tab(dashboard_); });
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(i18n_.tr("tab.general").data())) {
            draw_tab_body("general_body", [&] { ui::draw_general_tab(dashboard_); });
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(i18n_.tr("tab.diagnostics").data())) {
            draw_tab_body("diagnostics_body",
                          [&] { ui::draw_diagnostics_tab(dashboard_); });
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();

    // Recorded after the tabs have drawn: a recorder that is open must stop the
    // engine from interpreting keys, or the chord being recorded would be
    // swallowed by the binding it is replacing.
    if (engine_) {
        engine_->set_capture_mode(dashboard_.capture_active);
    }

    ImGui::Render();
    const float clear[4] = {0.082f, 0.094f, 0.114f, 1.0f};
    context_->OMSetRenderTargets(1, &render_target_, nullptr);
    context_->ClearRenderTargetView(render_target_, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    swap_chain_->Present(1, 0);
}

void Application::engine_thread_main() {
    auto next = std::chrono::steady_clock::now();
    auto last = next;

    while (!engine_stop_.load()) {
        const auto now = std::chrono::steady_clock::now();
        std::chrono::duration<double> elapsed = now - last;
        last = now;

        // Integrate the time that actually passed rather than the nominal
        // period. A sleeping machine, a busy CPU, or a coarse system timer
        // would otherwise make the cursor move slower than the configured
        // pixels per second. The clamp keeps a long stall (a resume from
        // standby, a debugger break) from turning into one huge jump.
        double dt = elapsed.count();
        if (dt > kMaxTickSeconds) {
            dt = kMaxTickSeconds;
        }

        if (engine_) {
            engine_->pump();
            engine_->tick(dt);

            if (engine_->consume_center_request()) {
                screen::center_cursor();
            }
        }

        next += kEnginePeriod;

        // If the thread fell behind, resync instead of spinning to catch up.
        // The missed time was already accounted for in dt above.
        const auto after_tick = std::chrono::steady_clock::now();
        if (next < after_tick) {
            next = after_tick;
            continue;
        }
        platform::sleep_for(next - after_tick);
    }
}

void Application::load_config() {
    const std::filesystem::path path = options_.config_path.empty()
        ? paths::config_file()
        : std::filesystem::path(options_.config_path);

    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        // No file yet: the defaults are what the user gets, and a few of them
        // (run at login, start minimized) are applied differently on a first
        // launch. See initialize().
        first_run_ = true;
        config_ = make_default_config();
        return;
    }

    const std::string text((std::istreambuf_iterator<char>(stream)),
                           std::istreambuf_iterator<char>());

    std::string error;
    const auto parsed = from_json(text, error);
    if (!parsed) {
        // Keep the broken file for inspection instead of silently overwriting
        // it, and start from defaults so the app still runs. Treating this like
        // a first run means the dashboard comes up rather than the app
        // disappearing into the tray with none of its settings applied.
        std::error_code ec;
        std::filesystem::copy_file(path, path.string() + ".broken",
                                   std::filesystem::copy_options::overwrite_existing, ec);
        first_run_ = true;
        config_ = make_default_config();
        return;
    }
    config_ = *parsed;
}

void Application::save_config() {
    const std::filesystem::path path = options_.config_path.empty()
        ? paths::config_file()
        : std::filesystem::path(options_.config_path);

    // Atomic write: temp file then rename, so a crash mid-write cannot leave a
    // truncated config behind.
    const std::filesystem::path temp = path.string() + ".tmp";
    {
        std::ofstream stream(temp, std::ios::binary | std::ios::trunc);
        if (!stream) {
            return;
        }
        const std::string text = to_json(config_);
        stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    }

    std::error_code ec;
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        std::filesystem::remove(temp, ec);
    }
}

void Application::apply_config_to_engine() {
    if (backend_) {
        // The Fn layer key can be changed from the dashboard, and only the
        // backend knows the physical key state.
        backend_->set_fn_layer_key(config_.fn_layer_key);
    }
    if (engine_) {
        engine_->apply_config(config_);
    }
    sync_tray_visibility();
    update_tray_status();
}

void Application::reload_locale() {
    i18n_.load(language_from_code(config_.locale), paths::locale_dir().string());
}

bool Application::install_hook() {
    if (!backend_ || !engine_) {
        return false;
    }
    return backend_->start([this](const InputEvent& event) {
        return engine_->on_input_event(event);
    });
}

void Application::on_session_event(session::SessionEvent event) {
    switch (event) {
        case session::SessionEvent::Locked:
            // Keys held on the way in are gone; without this the cursor would
            // keep moving behind the lock screen. Mouse mode is disarmed too:
            // plain keys must not stay armed while the user is away.
            if (engine_) {
                engine_->set_mouse_mode(false);
                engine_->forget_toggle_states();
                engine_->consume_beep_request();
                engine_->request_release();
                // A macro recording or playing across a lock would capture the
                // lock screen or keep clicking behind it.
                engine_->request_macro_stop();
                engine_->stop_macro_recording();
            }
            platform::stop_mouse_hook();
            break;

        case session::SessionEvent::Unlocked:
            // Windows drops low-level hooks across a desktop switch. Re-arming
            // is cheap and turns a dead application into a working one.
            if (backend_ && !backend_->running() && !options_.safe_mode) {
                install_hook();
            } else if (backend_ && engine_) {
                engine_->request_release();
            }
            if (tray_) {
                update_tray_status();
            }
            break;

        case session::SessionEvent::Other:
            break;
    }
}

void Application::session_callback(session::SessionEvent event) {
    if (g_app) {
        g_app->on_session_event(event);
    }
}

void Application::show_dashboard() {
    if (!window_) {
        return;
    }

    if (IsIconic(window_)) {
        ShowWindow(window_, SW_RESTORE);
    }
    ShowWindow(window_, SW_SHOW);
    SetForegroundWindow(window_);
    window_visible_ = true;
}

void Application::hide_dashboard() {
    if (!window_) {
        return;
    }
    ShowWindow(window_, SW_HIDE);
    window_visible_ = false;

    // A chord recorder cannot be visible while the window is hidden, and
    // leaving capture mode on would stop the engine from ever acting again.
    if (engine_) {
        engine_->set_capture_mode(false);
    }
}

void Application::toggle_enabled() {
    set_enabled(!config_.enabled);
}

void Application::set_enabled(bool enabled) {
    config_.enabled = enabled;
    if (engine_) {
        engine_->set_enabled(enabled);
    }
    update_tray_status();
    config_dirty_ = true;
}

void Application::update_tray_status() {
    if (!tray_) {
        return;
    }
    ui::TrayStatus status = ui::TrayStatus::Active;
    if (!config_.enabled) {
        status = ui::TrayStatus::Disabled;
    } else if (engine_ && engine_->mouse_mode()) {
        status = ui::TrayStatus::MouseMode;
    }
    tray_->set_status(status);
}

void Application::sync_from_engine() {
    if (!engine_) {
        return;
    }

    // The engine can be toggled by a binding or by the panic key, both of which
    // happen without the dashboard knowing.
    const bool engine_enabled = engine_->enabled();
    if (engine_enabled != config_.enabled) {
        config_.enabled = engine_enabled;
        update_tray_status();
        config_dirty_ = true;
    }

    // Mouse mode is runtime state. It is not persisted, but the tray icon and
    // the beep report changes made by the toggle key or the tray menu.
    const bool mode = engine_->mouse_mode();
    if (mode != last_mouse_mode_) {
        last_mouse_mode_ = mode;
        update_tray_status();
    }
    if (engine_->consume_beep_request()) {
        platform::beep();
    }

    // The auto-click key flips the master switch without touching the config.
    if (engine_->consume_auto_click_change()) {
        config_.auto_click.enabled = engine_->auto_click_enabled();
        config_dirty_ = true;
    }

    if (const auto speed = engine_->consume_speed_change()) {
        config_.motion.speed_px_s = *speed;
        config_dirty_ = true;
    }

    if (engine_->consume_dashboard_request()) {
        show_dashboard();
    }

    // ---- Macro requests ----------------------------------------------------
    // The record, play/pause, and stop chords are handled on the hook thread;
    // the file I/O they imply happens here, on the UI thread.
    if (engine_->consume_macro_record_toggle()) {
        toggle_macro_recording();
    }
    if (engine_->consume_macro_play_pause()) {
        toggle_macro_playback();
    }
    std::string macro_hotkey;
    if (engine_->consume_macro_hotkey(macro_hotkey)) {
        play_macro_by_id(macro_hotkey);
    }
    Macro recorded;
    if (engine_->consume_recorded_macro(recorded)) {
        store_finished_recording(std::move(recorded));
    }
}

void Application::apply_pending_requests() {
    if (request_locale_reload_) {
        request_locale_reload_ = false;
        reload_locale();
    }
    if (request_open_config_) {
        request_open_config_ = false;
        open_config_folder();
    }
}

void Application::sync_tray_visibility() {
    if (!tray_ || !window_) {
        return;
    }

    if (config_.show_tray_icon && !tray_visible_) {
        if (tray_->create(window_, nullptr)) {
            tray_visible_ = true;
            update_tray_status();
        }
    } else if (!config_.show_tray_icon && tray_visible_) {
        tray_->destroy();
        tray_visible_ = false;
    }
}

void Application::open_config_folder() {
    const std::filesystem::path dir = paths::data_dir();
    // ShellExecuteW is asynchronous for a folder, so this does not stall the
    // render loop. The failure cases (no association, blocked by policy) leave
    // the user with the path shown in the diagnostics tab.
    ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// ---------------------------------------------------------------------------
// Macros
// ---------------------------------------------------------------------------

std::filesystem::path Application::macro_dir() const {
    return paths::data_dir() / "macros";
}

std::string Application::unique_macro_id(std::string_view display) const {
    std::vector<std::string> taken;
    taken.reserve(config_.macros.entries.size());
    for (const MacroEntry& entry : config_.macros.entries) {
        taken.push_back(entry.id);
    }
    return macro_store::unique_id(macro_dir(), display, taken);
}

std::string Application::next_macro_name() const {
    const std::string base = std::string(i18n_.tr("macro.new_name").data());
    int number = static_cast<int>(config_.macros.entries.size()) + 1;
    for (;;) {
        const std::string candidate =
            number == 1 ? base : base + " " + std::to_string(number);
        bool used = false;
        for (const MacroEntry& entry : config_.macros.entries) {
            if (entry.name == candidate) {
                used = true;
                break;
            }
        }
        if (!used) {
            return candidate;
        }
        ++number;
    }
}

void Application::toggle_macro_recording() {
    if (!engine_) {
        return;
    }
    if (engine_->macro_recording()) {
        engine_->stop_macro_recording();
    } else {
        // Recording and playback do not mix; whatever was playing stops first.
        engine_->request_macro_stop();
        engine_->start_macro_recording(config_.macros.record_keyboard);
    }
    platform::beep();
    sync_macro_mouse_hook();
}

void Application::toggle_macro_playback() {
    if (!engine_) {
        return;
    }
    switch (engine_->macro_player_state()) {
        case MacroPlayerState::Playing:
            engine_->request_macro_pause();
            return;
        case MacroPlayerState::Paused:
            engine_->request_macro_resume();
            return;
        case MacroPlayerState::Idle:
            break;
    }
    play_loaded_macro(false);
}

void Application::play_loaded_macro(bool from_selected) {
    if (!engine_) {
        return;
    }
    if (!dashboard_.macro_edit.loaded) {
        if (config_.macros.entries.empty()) {
            return;
        }
        load_macro_into_editor(config_.macros.entries.front().id);
        if (!dashboard_.macro_edit.loaded) {
            return;
        }
    }
    if (dashboard_.macro_edit.macro.steps.empty()) {
        return;
    }

    // The player gets its own copy: editing the macro while it plays must not
    // reach into the step list that is being executed.
    auto macro = std::make_shared<Macro>(dashboard_.macro_edit.macro);
    MacroPlaybackOptions options = dashboard_.macro_options;
    if (from_selected && dashboard_.macro_edit.selected_step > 0) {
        options.start_step = static_cast<size_t>(dashboard_.macro_edit.selected_step);
    }
    engine_->play_macro(std::move(macro), options);
    sync_macro_mouse_hook();
}

void Application::play_macro_by_id(const std::string& id) {
    if (!engine_) {
        return;
    }
    std::string error;
    auto macro = macro_store::load(macro_dir(), id, error);
    if (!macro || macro->steps.empty()) {
        dashboard_.macro_message = std::string(i18n_.tr("macro.load_failed").data());
        return;
    }
    // Show the macro that the hotkey started, so Pause and Stop act on it.
    load_macro_into_editor(id);

    auto shared = std::make_shared<Macro>(std::move(*macro));
    engine_->play_macro(std::move(shared), dashboard_.macro_options);
    sync_macro_mouse_hook();
}

void Application::load_macro_into_editor(const std::string& id) {
    if (dashboard_.macro_edit.loaded && dashboard_.macro_edit.macro.id == id) {
        return;
    }
    if (dashboard_.macro_edit.dirty) {
        dashboard_.macro_edit.dirty = false;
        save_macro_edit();
    }

    std::string error;
    auto macro = macro_store::load(macro_dir(), id, error);
    if (!macro) {
        dashboard_.macro_message = std::string(i18n_.tr("macro.load_failed").data());
        return;
    }
    macro->id = id;
    dashboard_.macro_edit.macro = std::move(*macro);
    dashboard_.macro_edit.loaded = true;
    dashboard_.macro_edit.selected_step = -1;
    dashboard_.macro_edit.dirty = false;
    dashboard_.macro_message.clear();
}

void Application::save_macro_edit() {
    if (!dashboard_.macro_edit.loaded) {
        return;
    }
    Macro& macro = dashboard_.macro_edit.macro;
    std::string error;
    if (!macro_store::save(macro_dir(), macro, error)) {
        dashboard_.macro_message = error;
        return;
    }
    for (MacroEntry& entry : config_.macros.entries) {
        if (entry.id == macro.id && entry.name != macro.name) {
            entry.name = macro.name;
            config_dirty_ = true;
        }
    }
}

void Application::store_finished_recording(Macro macro) {
    macro.name = next_macro_name();
    macro.id = unique_macro_id(macro.name);

    std::string error;
    if (!macro_store::save(macro_dir(), macro, error)) {
        dashboard_.macro_message = error;
        return;
    }

    MacroEntry entry;
    entry.id = macro.id;
    entry.name = macro.name;
    config_.macros.entries.push_back(entry);
    config_dirty_ = true;

    dashboard_.macro_edit.macro = std::move(macro);
    dashboard_.macro_edit.loaded = true;
    dashboard_.macro_edit.selected_step = -1;
    dashboard_.macro_edit.dirty = false;
    dashboard_.macro_message.clear();
}

void Application::new_macro() {
    Macro macro;
    macro.name = next_macro_name();
    macro.id = unique_macro_id(macro.name);

    std::string error;
    if (!macro_store::save(macro_dir(), macro, error)) {
        dashboard_.macro_message = error;
        return;
    }

    MacroEntry entry;
    entry.id = macro.id;
    entry.name = macro.name;
    config_.macros.entries.push_back(entry);
    config_dirty_ = true;

    dashboard_.macro_edit.macro = std::move(macro);
    dashboard_.macro_edit.loaded = true;
    dashboard_.macro_edit.selected_step = -1;
    dashboard_.macro_edit.dirty = false;
    dashboard_.macro_message.clear();
}

void Application::duplicate_macro(const std::string& id) {
    Macro copy;
    if (dashboard_.macro_edit.loaded && dashboard_.macro_edit.macro.id == id) {
        copy = dashboard_.macro_edit.macro;
    } else {
        std::string error;
        const auto loaded = macro_store::load(macro_dir(), id, error);
        if (!loaded) {
            dashboard_.macro_message = std::string(i18n_.tr("macro.load_failed").data());
            return;
        }
        copy = *loaded;
    }

    copy.name += std::string(i18n_.tr("macro.copy_suffix").data());
    copy.id = unique_macro_id(copy.name);

    std::string error;
    if (!macro_store::save(macro_dir(), copy, error)) {
        dashboard_.macro_message = error;
        return;
    }

    MacroEntry entry;
    entry.id = copy.id;
    entry.name = copy.name;
    config_.macros.entries.push_back(entry);
    config_dirty_ = true;

    dashboard_.macro_edit.macro = std::move(copy);
    dashboard_.macro_edit.loaded = true;
    dashboard_.macro_edit.selected_step = -1;
    dashboard_.macro_edit.dirty = false;
    dashboard_.macro_message.clear();
}

void Application::delete_macro(const std::string& id) {
    macro_store::remove_file(macro_dir(), id);
    config_.macros.entries.erase(
        std::remove_if(config_.macros.entries.begin(), config_.macros.entries.end(),
                       [&id](const MacroEntry& entry) { return entry.id == id; }),
        config_.macros.entries.end());
    config_dirty_ = true;

    if (dashboard_.macro_edit.loaded && dashboard_.macro_edit.macro.id == id) {
        // Everything the editor showed is gone; the next selection reloads it.
        dashboard_.macro_edit.macro = Macro{};
        dashboard_.macro_edit.loaded = false;
        dashboard_.macro_edit.selected_step = -1;
        dashboard_.macro_edit.dirty = false;
    }
    dashboard_.macro_message = std::string(i18n_.tr("macro.deleted").data());
}

void Application::import_macro() {
    static constexpr wchar_t kFilter[] =
        L"KeyMouse macros (*.kmacro)\0*.kmacro\0All files (*.*)\0*.*\0";

    const auto picked = platform::open_file_dialog(L"Import macro", kFilter);
    if (!picked) {
        return;
    }

    std::ifstream stream(*picked, std::ios::binary);
    if (!stream) {
        dashboard_.macro_message = std::string(i18n_.tr("macro.import_failed").data());
        return;
    }
    const std::string text((std::istreambuf_iterator<char>(stream)),
                           std::istreambuf_iterator<char>());

    std::string error;
    auto parsed = macro_from_json(text, error);
    if (!parsed) {
        dashboard_.macro_message = std::string(i18n_.tr("macro.import_failed").data());
        return;
    }

    Macro macro = std::move(*parsed);
    if (macro.name.empty()) {
        macro.name = picked->stem().string();
    }
    macro.id = unique_macro_id(macro.name);

    if (!macro_store::save(macro_dir(), macro, error)) {
        dashboard_.macro_message = error;
        return;
    }

    MacroEntry entry;
    entry.id = macro.id;
    entry.name = macro.name;
    config_.macros.entries.push_back(entry);
    config_dirty_ = true;

    dashboard_.macro_edit.macro = std::move(macro);
    dashboard_.macro_edit.loaded = true;
    dashboard_.macro_edit.selected_step = -1;
    dashboard_.macro_edit.dirty = false;
    dashboard_.macro_message = std::string(i18n_.tr("macro.imported").data());
}

void Application::export_macro(const std::string& id) {
    static constexpr wchar_t kFilter[] =
        L"KeyMouse macros (*.kmacro)\0*.kmacro\0All files (*.*)\0*.*\0";

    Macro macro;
    if (dashboard_.macro_edit.loaded && dashboard_.macro_edit.macro.id == id) {
        macro = dashboard_.macro_edit.macro;
    } else {
        std::string error;
        const auto loaded = macro_store::load(macro_dir(), id, error);
        if (!loaded) {
            dashboard_.macro_message = std::string(i18n_.tr("macro.load_failed").data());
            return;
        }
        macro = *loaded;
    }

    // The id is sanitized ASCII, so widening it is safe.
    const std::wstring default_name(id.begin(), id.end());
    const auto target = platform::save_file_dialog(
        L"Export macro", kFilter, (default_name + L".kmacro").c_str());
    if (!target) {
        return;
    }

    std::ofstream stream(*target, std::ios::binary | std::ios::trunc);
    if (stream) {
        const std::string text = macro_to_json(macro);
        stream.write(text.data(), static_cast<std::streamsize>(text.size()));
        stream.flush();
    }
    if (!stream) {
        dashboard_.macro_message = std::string(i18n_.tr("macro.export_failed").data());
        return;
    }
    dashboard_.macro_message = std::string(i18n_.tr("macro.exported").data());
}

void Application::set_macro_hotkey(const std::string& id, const Chord& chord) {
    for (MacroEntry& entry : config_.macros.entries) {
        if (entry.id != id) {
            continue;
        }
        if (entry.hotkey == chord) {
            return;
        }
        entry.hotkey = chord;
        config_dirty_ = true;
        return;
    }
}

void Application::sync_macro_mouse_hook() {
    if (!engine_) {
        return;
    }
    const bool wanted = engine_->macro_wants_mouse_hook();
    if (wanted && !platform::mouse_hook_running()) {
        platform::start_mouse_hook(&Application::mouse_event_callback);
    } else if (!wanted && platform::mouse_hook_running()) {
        platform::stop_mouse_hook();
    }
}

void Application::mouse_event_callback(const MacroInputEvent& event) {
    // Hook thread. The engine only pushes to a lock-free ring.
    if (g_app && g_app->engine_) {
        g_app->engine_->on_mouse_event(event);
    }
}

void Application::on_command(const std::string& command) {
    if (command == "show") {
        // Posted rather than called directly: the command arrives on the pipe
        // thread and window calls must happen on the UI thread.
        PostMessageW(window_, WM_APP + 10, 0, 0);
    } else if (command == "toggle") {
        PostMessageW(window_, WM_APP + 11, 0, 0);
    } else if (command == "exit") {
        PostMessageW(window_, WM_APP + 12, 0, 0);
    }
}

void Application::poll_foreground() {
    const auto now = std::chrono::steady_clock::now();
    if (now - last_foreground_poll_ < kForegroundPollPeriod) {
        return;
    }
    last_foreground_poll_ = now;

    // Keep this running even with a single profile: the auto-click only_if_app
    // filter and the status tab both need to know what is in front.
    foreground_exe_ = screen::foreground_exe_name();
    if (engine_) {
        engine_->set_foreground_exe(foreground_exe_);
    }

    if (!config_.auto_switch_profiles || config_.profiles.size() < 2) {
        return;
    }

    static std::string last_exe;
    if (foreground_exe_ == last_exe) {
        return;
    }
    last_exe = foreground_exe_;

    const ProfileMatch match = match_profile(config_, foreground_exe_);
    if (match.matched && match.index != config_.active_profile) {
        config_.active_profile = match.index;
        apply_config_to_engine();
        config_dirty_ = true;
    }
}

LRESULT CALLBACK Application::window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) {
        return true;
    }

    switch (msg) {
        case WM_SIZE:
            if (g_app && g_app->swap_chain_ && wparam != SIZE_MINIMIZED &&
                LOWORD(lparam) > 0 && HIWORD(lparam) > 0) {
                if (g_app->render_target_) {
                    g_app->render_target_->Release();
                    g_app->render_target_ = nullptr;
                }
                g_app->swap_chain_->ResizeBuffers(0, LOWORD(lparam), HIWORD(lparam),
                                                  DXGI_FORMAT_UNKNOWN, 0);
                ID3D11Texture2D* back_buffer = nullptr;
                if (g_app->swap_chain_->GetBuffer(0, IID_PPV_ARGS(&back_buffer)) == S_OK) {
                    g_app->device_->CreateRenderTargetView(back_buffer, nullptr,
                                                           &g_app->render_target_);
                    back_buffer->Release();
                }
            }
            return 0;

        case WM_CLOSE:
            // Closing hides to the tray instead of exiting, which is what a
            // background utility should do. Without a tray icon there would be
            // no way back, so the window closes for real instead.
            if (g_app) {
                if (g_app->tray_visible_) {
                    g_app->request_hide_ = true;
                } else {
                    g_app->request_exit_ = true;
                }
            }
            return 0;

        case WM_APP + 10:
            if (g_app) g_app->show_dashboard();
            return 0;

        case WM_APP + 11:
            if (g_app) g_app->toggle_enabled();
            return 0;

        case WM_APP + 12:
            if (g_app) g_app->request_exit_ = true;
            PostQuitMessage(0);
            return 0;

        case WM_WTSSESSION_CHANGE:
            session::handle_session_change(static_cast<unsigned long>(wparam));
            return 0;

        case WM_DPICHANGED:
            if (g_app) {
                g_app->dpi_scale_ = static_cast<float>(HIWORD(wparam)) / 96.0f;
                const RECT* suggested = reinterpret_cast<const RECT*>(lparam);
                SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                             suggested->right - suggested->left,
                             suggested->bottom - suggested->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
                // A DPI change can arrive while the window is being created,
                // before the ImGui context exists. create_device() then uses the
                // updated scale for the first font atlas.
                if (ImGui::GetCurrentContext()) {
                    ui::apply_theme(g_app->dpi_scale_);
                    ui::rebuild_fonts(g_app->dpi_scale_);
                }
            }
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        default:
            break;
    }

    if (msg == ui::TrayIcon::message_id() && g_app && g_app->tray_) {
        g_app->tray_->handle_message(wparam, lparam);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

}  // namespace km
