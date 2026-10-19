// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <filesystem>
#include <optional>

namespace km {
namespace platform {

// Native open/save dialogs. `filter` uses the Win32 double-null format:
//   L"KeyMouse profiles (*.json)\0*.json\0All files (*.*)\0*.*\0"
// Returns nullopt when the user cancels or the dialog cannot be shown.
std::optional<std::filesystem::path> open_file_dialog(const wchar_t* title,
                                                      const wchar_t* filter);
std::optional<std::filesystem::path> save_file_dialog(const wchar_t* title,
                                                      const wchar_t* filter,
                                                      const wchar_t* default_name);

}  // namespace platform
}  // namespace km
