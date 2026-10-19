// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/file_dialog.h"

#include <array>
#include <string>

#include <windows.h>
#include <commdlg.h>

namespace km {
namespace platform {
namespace {

std::optional<std::filesystem::path> run_dialog(OPENFILENAMEW& options,
                                                std::array<wchar_t, 1024>& buffer) {
    options.lStructSize = sizeof(options);
    options.hwndOwner = GetActiveWindow();
    options.lpstrFile = buffer.data();
    options.nMaxFile = static_cast<DWORD>(buffer.size());
    options.Flags |= OFN_EXPLORER | OFN_NOCHANGEDIR;

    const BOOL ok = options.Flags & OFN_OVERWRITEPROMPT
        ? GetSaveFileNameW(&options)
        : GetOpenFileNameW(&options);
    if (!ok) {
        return std::nullopt;
    }
    return std::filesystem::path(buffer.data());
}

}  // namespace

std::optional<std::filesystem::path> open_file_dialog(const wchar_t* title,
                                                      const wchar_t* filter) {
    std::array<wchar_t, 1024> buffer{};
    OPENFILENAMEW options{};
    options.lpstrTitle = title;
    options.lpstrFilter = filter;
    options.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    return run_dialog(options, buffer);
}

std::optional<std::filesystem::path> save_file_dialog(const wchar_t* title,
                                                      const wchar_t* filter,
                                                      const wchar_t* default_name) {
    std::array<wchar_t, 1024> buffer{};
    if (default_name) {
        wcsncpy_s(buffer.data(), buffer.size(), default_name, _TRUNCATE);
    }
    OPENFILENAMEW options{};
    options.lpstrTitle = title;
    options.lpstrFilter = filter;
    options.lpstrDefExt = L"json";
    options.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    return run_dialog(options, buffer);
}

}  // namespace platform
}  // namespace km
