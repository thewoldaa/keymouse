// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>

#include "input/input_backend.h"

namespace km {

// Creates the Win32 backend: WH_KEYBOARD_LL capture plus SendInput injection.
std::unique_ptr<InputBackend> make_win32_backend();

}  // namespace km
