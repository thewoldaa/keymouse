// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace km {
namespace version {

// Filled in by CMake from project(VERSION ...), so the string the user sees in
// the dashboard header, the diagnostics tab, --version, and the executable's
// resource block all come from one place. The fallback keeps the header
// usable when it is compiled outside CMake, for instance by an IDE indexer.
#ifndef KEYMOUSE_VERSION
#define KEYMOUSE_VERSION "0.0.0-unknown"
#endif

inline constexpr const char* kVersion = KEYMOUSE_VERSION;

}  // namespace version
}  // namespace km
