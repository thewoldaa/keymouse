// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "core/keycodes.h"

namespace km {
namespace win32 {

// Translates between Windows virtual-key / scan codes and canonical KeyIds.
//
// Scan codes are the source of truth: they describe physical key positions
// and survive keyboard layout changes. The extended-key flag distinguishes
// keys that share a scan code (numpad Enter vs Enter, arrows vs numpad).

struct KeyTranslation {
    KeyId key = key::Invalid;
    // True for keys that Windows reports with the extended flag set.
    bool extended = false;
};

// From a low-level hook's KBDLLHOOKSTRUCT.
KeyTranslation from_scan_code(uint32_t scan_code, bool extended, uint32_t vk);

// From a virtual-key code, for callers that only have a VK.
KeyId from_virtual_key(uint32_t vk);

// Reverse mapping for the UI: shows which physical key the user pressed.
KeyTranslation to_scan_code(KeyId key);

// Returns true when this key must never be swallowed. Ctrl+Alt+Del cannot be
// intercepted; swallowing the pieces of it produces a broken keyboard.
bool is_protected_key(uint32_t vk);

}  // namespace win32
}  // namespace km
