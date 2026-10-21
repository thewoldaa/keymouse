// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/macro.h"

#include <algorithm>
#include <cctype>
#include <limits>

#include <nlohmann/json.hpp>

namespace km {
namespace {

using json = nlohmann::json;

int32_t clamp_coordinate(int64_t value) {
    return static_cast<int32_t>(std::clamp<int64_t>(
        value, -kMaxMacroCoordinate, kMaxMacroCoordinate));
}

}  // namespace

std::string_view macro_step_type_name(MacroStepType type) {
    switch (type) {
        case MacroStepType::Move: return "move";
        case MacroStepType::ButtonDown: return "button_down";
        case MacroStepType::ButtonUp: return "button_up";
        case MacroStepType::Scroll: return "scroll";
        case MacroStepType::ScrollH: return "scroll_h";
        case MacroStepType::KeyDown: return "key_down";
        case MacroStepType::KeyUp: return "key_up";
        case MacroStepType::Wait: return "wait";
        case MacroStepType::MouseDelta: return "mouse_delta";
    }
    return "wait";
}

MacroStepType macro_step_type_from_name(std::string_view name) {
    if (name == "move") return MacroStepType::Move;
    if (name == "button_down") return MacroStepType::ButtonDown;
    if (name == "button_up") return MacroStepType::ButtonUp;
    if (name == "scroll") return MacroStepType::Scroll;
    if (name == "scroll_h") return MacroStepType::ScrollH;
    if (name == "key_down") return MacroStepType::KeyDown;
    if (name == "key_up") return MacroStepType::KeyUp;
    if (name == "mouse_delta") return MacroStepType::MouseDelta;
    return MacroStepType::Wait;
}

std::string_view macro_button_name(MouseButton button) {
    switch (button) {
        case MouseButton::Left: return "left";
        case MouseButton::Right: return "right";
        case MouseButton::Middle: return "middle";
        case MouseButton::X1: return "x1";
        case MouseButton::X2: return "x2";
    }
    return "left";
}

std::optional<MouseButton> macro_button_from_name(std::string_view name) {
    if (name == "left") return MouseButton::Left;
    if (name == "right") return MouseButton::Right;
    if (name == "middle") return MouseButton::Middle;
    if (name == "x1") return MouseButton::X1;
    if (name == "x2") return MouseButton::X2;
    return std::nullopt;
}

int64_t macro_duration_ms(const Macro& macro) {
    int64_t total = 0;
    for (const MacroStep& step : macro.steps) {
        if (step.dt_ms > kMaxMacroStepDelayMs - total) {
            return kMaxMacroStepDelayMs;
        }
        total += step.dt_ms;
    }
    return total;
}

bool macro_has_action(const Macro& macro) {
    return std::any_of(macro.steps.begin(), macro.steps.end(),
                       [](const MacroStep& step) {
                           return step.type != MacroStepType::Wait;
                       });
}

std::string macro_to_json(const Macro& macro) {
    json steps = json::array();
    for (const MacroStep& step : macro.steps) {
        json item;
        item["dt"] = step.dt_ms;
        item["type"] = std::string(macro_step_type_name(step.type));
        switch (step.type) {
            case MacroStepType::Move:
            case MacroStepType::MouseDelta:
                // Both store a screen-space pair; Move means "go to", MouseDelta
                // means "add this much".
                item["x"] = step.x;
                item["y"] = step.y;
                break;
            case MacroStepType::ButtonDown:
            case MacroStepType::ButtonUp:
                item["button"] = std::string(
                    macro_button_name(static_cast<MouseButton>(step.data)));
                break;
            case MacroStepType::Scroll:
            case MacroStepType::ScrollH:
                item["delta"] = step.data;
                break;
            case MacroStepType::KeyDown:
            case MacroStepType::KeyUp:
                item["key"] = std::string(key_name(step.key));
                break;
            case MacroStepType::Wait:
                break;
        }
        steps.push_back(std::move(item));
    }

    const json root{
        {"format", "keymouse-macro"},
        // 2 added the mouse_delta step. Older builds reject a file that uses
        // it, which is honest: silently dropping the camera movement would
        // produce a replay that looks like it worked and does nothing.
        {"version", kMacroFormatVersion},
        {"id", macro.id},
        {"name", macro.name},
        {"recorded_width", macro.recorded_width},
        {"recorded_height", macro.recorded_height},
        {"steps", steps},
    };
    return root.dump(2);
}

std::optional<Macro> macro_from_json(std::string_view text, std::string& error) {
    json root;
    try {
        root = json::parse(text.begin(), text.end());
    } catch (const std::exception& ex) {
        error = std::string("JSON parse error: ") + ex.what();
        return std::nullopt;
    }

    if (!root.is_object()) {
        error = "The macro root must be a JSON object";
        return std::nullopt;
    }

    // A file from a newer build can contain step types this build does not
    // know, and the per-step check below would reject it with a confusing
    // "unknown step type". Say what the real problem is instead: the file is
    // newer than the application.
    if (root.contains("version") && root["version"].is_number_integer()) {
        const int64_t file_version = root["version"].get<int64_t>();
        if (file_version > kMacroFormatVersion) {
            error = "This macro was written by a newer KeyMouse (format " +
                    std::to_string(file_version) + ", this build reads " +
                    std::to_string(kMacroFormatVersion) + ")";
            return std::nullopt;
        }
    }

    Macro macro;
    if (root.contains("id") && root["id"].is_string()) {
        macro.id = root["id"].get<std::string>();
    }
    if (root.contains("name") && root["name"].is_string()) {
        macro.name = root["name"].get<std::string>();
    }
    if (root.contains("recorded_width") && root["recorded_width"].is_number_integer()) {
        macro.recorded_width = clamp_coordinate(
            root["recorded_width"].get<int64_t>());
    }
    if (root.contains("recorded_height") && root["recorded_height"].is_number_integer()) {
        macro.recorded_height = clamp_coordinate(
            root["recorded_height"].get<int64_t>());
    }

    if (!root.contains("steps") || !root["steps"].is_array()) {
        error = "The macro has no steps array";
        return std::nullopt;
    }
    if (root["steps"].size() > kMaxMacroSteps) {
        error = "The macro has more steps than this build supports";
        return std::nullopt;
    }

    macro.steps.reserve(root["steps"].size());
    size_t index = 0;
    for (const auto& item : root["steps"]) {
        const std::string where = " (step " + std::to_string(index) + ")";
        if (!item.is_object()) {
            error = "A macro step is not an object" + where;
            return std::nullopt;
        }
        if (!item.contains("type") || !item["type"].is_string()) {
            error = "A macro step has no type" + where;
            return std::nullopt;
        }
        const std::string type_name = item["type"].get<std::string>();
        if (type_name != "move" && type_name != "button_down" &&
            type_name != "button_up" && type_name != "scroll" &&
            type_name != "scroll_h" && type_name != "key_down" &&
            type_name != "key_up" && type_name != "wait" &&
            type_name != "mouse_delta") {
            error = "Unknown macro step type '" + type_name + "'" + where;
            return std::nullopt;
        }

        MacroStep step;
        step.type = macro_step_type_from_name(type_name);
        if (item.contains("dt") && item["dt"].is_number_integer()) {
            step.dt_ms = std::clamp<int64_t>(item["dt"].get<int64_t>(), 0,
                                             kMaxMacroStepDelayMs);
        }

        switch (step.type) {
            case MacroStepType::Move:
            case MacroStepType::MouseDelta:
                if (item.contains("x") && item["x"].is_number_integer()) {
                    step.x = clamp_coordinate(item["x"].get<int64_t>());
                }
                if (item.contains("y") && item["y"].is_number_integer()) {
                    step.y = clamp_coordinate(item["y"].get<int64_t>());
                }
                break;
            case MacroStepType::ButtonDown:
            case MacroStepType::ButtonUp: {
                if (!item.contains("button") || !item["button"].is_string()) {
                    error = "A button step has no button" + where;
                    return std::nullopt;
                }
                const auto button = macro_button_from_name(
                    item["button"].get<std::string>());
                if (!button) {
                    error = "Unknown mouse button '" +
                            item["button"].get<std::string>() + "'" + where;
                    return std::nullopt;
                }
                step.data = static_cast<int32_t>(*button);
                break;
            }
            case MacroStepType::Scroll:
            case MacroStepType::ScrollH:
                if (item.contains("delta") && item["delta"].is_number_integer()) {
                    step.data = static_cast<int32_t>(std::clamp<int64_t>(
                        item["delta"].get<int64_t>(), -100000, 100000));
                }
                break;
            case MacroStepType::KeyDown:
            case MacroStepType::KeyUp: {
                if (!item.contains("key") || !item["key"].is_string()) {
                    error = "A key step has no key" + where;
                    return std::nullopt;
                }
                const auto key = key_from_name(item["key"].get<std::string>());
                if (!key || *key == key::Invalid) {
                    error = "Unknown key '" + item["key"].get<std::string>() + "'" + where;
                    return std::nullopt;
                }
                step.key = *key;
                break;
            }
            case MacroStepType::Wait:
                break;
        }

        macro.steps.push_back(step);
        ++index;
    }

    return macro;
}

std::string macro_file_stem(std::string_view name) {
    std::string out;
    out.reserve(name.size());
    for (const char raw : name) {
        const unsigned char c = static_cast<unsigned char>(raw);
        if (std::isalnum(c) != 0 || c == '-' || c == '_') {
            out.push_back(static_cast<char>(c));
        } else if (c == ' ') {
            out.push_back('-');
        }
        if (out.size() >= 64) {
            break;
        }
    }
    if (out.empty()) {
        return "macro";
    }
    return out;
}

}  // namespace km
