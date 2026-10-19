// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/binding.h"

#include <array>

namespace km {
namespace {

struct NamePair {
    std::string_view name;
    BindMode mode;
};

constexpr std::array<NamePair, 3> kBindModes{{
    {"hold", BindMode::Hold},
    {"toggle", BindMode::Toggle},
    {"tap", BindMode::Tap},
}};

struct SwallowPair {
    std::string_view name;
    SwallowMode mode;
};

constexpr std::array<SwallowPair, 2> kSwallowModes{{
    {"pass", SwallowMode::PassThrough},
    {"swallow", SwallowMode::Swallow},
}};

}  // namespace

std::string_view bind_mode_name(BindMode mode) {
    for (const auto& entry : kBindModes) {
        if (entry.mode == mode) {
            return entry.name;
        }
    }
    return "hold";
}

BindMode bind_mode_from_name(std::string_view name) {
    for (const auto& entry : kBindModes) {
        if (entry.name == name) {
            return entry.mode;
        }
    }
    return BindMode::Hold;
}

std::string_view swallow_mode_name(SwallowMode mode) {
    for (const auto& entry : kSwallowModes) {
        if (entry.mode == mode) {
            return entry.name;
        }
    }
    return "swallow";
}

SwallowMode swallow_mode_from_name(std::string_view name) {
    for (const auto& entry : kSwallowModes) {
        if (entry.name == name) {
            return entry.mode;
        }
    }
    return SwallowMode::Swallow;
}

}  // namespace km
