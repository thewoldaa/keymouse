// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include <windows.h>

#include "app/cli.h"
#include "core/config.h"
#include "core/engine.h"
#include "core/i18n.h"
#include "input/input_backend.h"
#include "platform/session.h"
#include "ui/dashboard.h"
#include "ui/tray.h"

struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDXGISwapChain;
struct ID3D11RenderTargetView;

namespace km {

// Owns the application lifetime: config, input backend, engine, tray icon, and
// the DirectX 11 dashboard window.
class Application {
public:
    Application();
    ~Application();

    int run(int argc, wchar_t** argv);

private:
    bool initialize(int argc, wchar_t** argv);
    void shutdown();

    bool create_window();
    bool create_device();
    void destroy_device();
    void render_frame();

    void load_config();
    void save_config();
    void apply_config_to_engine();
    void reload_locale();

    // Installs the keyboard hook. Kept separate because the hook has to be
    // re-armed after a session unlock.
    bool install_hook();
    void on_session_event(session::SessionEvent event);

    void engine_thread_main();

    void show_dashboard();
    void hide_dashboard();
    void toggle_enabled();
    void set_enabled(bool enabled);

    // Copies state the engine owns (enable flag, speed chosen by a binding)
    // back into the config, so the dashboard and the saved file agree with what
    // is actually running.
    void sync_from_engine();
    void apply_pending_requests();
    void sync_tray_visibility();
    void open_config_folder();

    void on_command(const std::string& command);
    void poll_foreground();
    void update_tray_status();

public:
    // The window procedure is a static C callback, so it must be reachable
    // from the file-scope WNDCLASSEXW registration helper.
    static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    // Session notifications arrive through a plain function pointer, so the
    // handler must be a free-standing function too.
    static void session_callback(session::SessionEvent event);

private:

    HWND window_ = nullptr;
    bool window_visible_ = false;
    bool tray_visible_ = false;

    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    IDXGISwapChain* swap_chain_ = nullptr;
    ID3D11RenderTargetView* render_target_ = nullptr;

    AppConfig config_;
    CliOptions options_;
    I18n i18n_;

    std::unique_ptr<InputBackend> backend_;
    std::unique_ptr<Engine> engine_;
    std::unique_ptr<ui::TrayIcon> tray_;

    ui::DashboardContext dashboard_{};

    std::thread engine_thread_;
    std::atomic<bool> engine_stop_{false};

    bool config_dirty_ = false;
    bool request_hide_ = false;
    bool request_exit_ = false;
    bool request_locale_reload_ = false;
    bool request_open_config_ = false;

    bool initialized_ = false;
    bool first_run_ = false;
    float dpi_scale_ = 1.0f;
    bool last_mouse_mode_ = false;

    std::string foreground_exe_;
    std::chrono::steady_clock::time_point last_foreground_poll_{};
};

}  // namespace km
