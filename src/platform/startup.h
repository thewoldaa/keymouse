// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

namespace km {

// Start-with-Windows management through HKCU\...\Run. No admin rights, no
// service, and removal is a single registry delete that the uninstaller also
// performs.
namespace startup {

// True when the Run value exists and points at this executable.
bool is_enabled();

// Adds or removes the Run value. Returns false on registry failure.
bool set_enabled(bool enabled);

// Registry value name. Exposed so the uninstaller and diagnostics agree.
constexpr wchar_t kValueName[] = L"KeyMouse";

// The exact command line written to the Run key.
std::wstring command_line();

}  // namespace startup

}  // namespace km
