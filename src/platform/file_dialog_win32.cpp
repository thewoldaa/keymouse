// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/file_dialog.h"

#include <array>
#include <iterator>
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

    // The dialog appends the default extension when the user types a name
    // without one. It must match what is being saved: exporting a macro as
    // ".json" would produce a file KeyMouse itself would then refuse to import.
    // Derive it from the suggested name; fall back to json only when the name
    // has no extension.
    const wchar_t* default_ext = L"json";
    wchar_t extension[16]{};
    if (default_name) {
        const wchar_t* dot = wcsrchr(default_name, L'.');
        const wchar_t* separator = wcsrchr(default_name, L'\\');
        const bool after_separator = separator == nullptr || dot > separator;
        if (dot != nullptr && after_separator && dot[1] != L'\0') {
            size_t length = 0;
            for (const wchar_t* c = dot + 1; *c != L'\0' && length + 1 < std::size(extension);
                 ++c) {
                extension[length++] = *c;
            }
            extension[length] = L'\0';
            if (length > 0) {
                default_ext = extension;
            }
        }
    }

    OPENFILENAMEW options{};
    options.lpstrTitle = title;
    options.lpstrFilter = filter;
    options.lpstrDefExt = default_ext;
    options.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    return run_dialog(options, buffer);
}

}  // namespace platform
}  // namespace km
