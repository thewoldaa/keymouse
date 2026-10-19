// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace km {

// Attached to every input event KeyMouse injects through SendInput.
//
// The keyboard hook deliberately still sees third-party injected input (a
// remapper, an on-screen keyboard, a macro pad), but it must never interpret
// KeyMouse's own output: a macro that plays a key would otherwise re-enter the
// hook, fire bindings, and possibly record itself. Tagging gives the hooks a
// precise filter instead of the too-broad LLKHF_INJECTED / LLMHF_INJECTED flags.
//
// It fits in 32 bits on purpose. Windows hands low-level hook procedures the
// value truncated to a DWORD — the high half is dropped before the hook sees it
// — so a 64-bit tag silently never matches, which is exactly how a playing macro
// ended up pausing itself on its own first click. "KMO1" is the low half of the
// old 64-bit value, kept because it is already distinctive.
inline constexpr uint32_t kInjectionTag = 0x4B4D4F31u;

}  // namespace km
