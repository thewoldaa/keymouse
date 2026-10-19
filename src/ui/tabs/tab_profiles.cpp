// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/dashboard.h"

#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

#include <imgui.h>

#include "core/profiles.h"
#include "platform/file_dialog.h"
#include "ui/theme.h"

namespace km {
namespace ui {
namespace {

void draw_profile_row(DashboardContext& context, size_t index, bool* changed) {
    AppConfig& config = *context.config;
    Profile& profile = config.profiles[index];

    ImGui::PushID(static_cast<int>(index));
    ImGui::TableNextRow();

    ImGui::TableSetColumnIndex(0);
    if (ImGui::Selectable(profile.name.c_str(), config.active_profile == index,
                          ImGuiSelectableFlags_SpanAllColumns)) {
        config.active_profile = index;
        *changed = true;
    }

    ImGui::TableSetColumnIndex(1);
    if (profile.match_exe.empty()) {
        ImGui::TextDisabled("-");
    } else {
        ImGui::TextUnformatted(profile.match_exe.c_str());
    }

    ImGui::TableSetColumnIndex(2);
    ImGui::Text("%zu", profile.bindings.size());

    ImGui::TableSetColumnIndex(3);
    ImGui::BeginDisabled(config.profiles.size() <= 1);
    if (ImGui::SmallButton(context.i18n->tr("common.remove").data())) {
        config.profiles.erase(config.profiles.begin() + static_cast<std::ptrdiff_t>(index));
        if (config.active_profile >= config.profiles.size()) {
            config.active_profile = config.profiles.size() - 1;
        }
        *changed = true;
    }
    ImGui::EndDisabled();

    ImGui::PopID();
}

// Allows ImGui's InputText to grow a std::string instead of writing into a
// fixed buffer, so a long name is never silently truncated.
int resize_string_callback(ImGuiInputTextCallbackData* data) {
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto* text = static_cast<std::string*>(data->UserData);
        text->resize(static_cast<size_t>(data->BufTextLen));
        data->Buf = text->data();
    }
    return 0;
}

bool string_field(const char* label, std::string& value, float width) {
    ImGui::SetNextItemWidth(width);
    return ImGui::InputText(label, value.data(), value.capacity() + 1,
                            ImGuiInputTextFlags_CallbackResize,
                            &resize_string_callback, &value);
}

std::string unique_profile_name(const AppConfig& config, std::string name) {
    if (name.empty()) {
        name = "Imported";
    }
    const auto used = [&config](const std::string& candidate) {
        for (const auto& profile : config.profiles) {
            if (profile.name == candidate) {
                return true;
            }
        }
        return false;
    };
    if (!used(name)) {
        return name;
    }
    for (int number = 2; number < 1000; ++number) {
        const std::string candidate = name + " (" + std::to_string(number) + ")";
        if (!used(candidate)) {
            return candidate;
        }
    }
    return name;
}

}  // namespace

void draw_profiles_tab(DashboardContext& context) {
    AppConfig& config = *context.config;
    I18n& tr = *context.i18n;
    bool changed = false;

    ImGui::TextDisabled("%s", tr.tr("profiles.hint").data());
    ImGui::Spacing();

    if (ImGui::BeginTable("profiles", 4,
                          ImGuiTableFlags_SizingStretchProp |
                          ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn(tr.tr("profiles.col.name").data(),
                                ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(tr.tr("profiles.col.app").data(),
                                ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(tr.tr("profiles.col.bindings").data(),
                                ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableHeadersRow();

        for (size_t i = 0; i < config.profiles.size(); ++i) {
            draw_profile_row(context, i, &changed);
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    if (ImGui::Button(tr.tr("profiles.add").data())) {
        Profile profile;
        profile.name = std::string(tr.tr("profiles.default_name")) + " " +
                       std::to_string(config.profiles.size() + 1);
        config.profiles.push_back(std::move(profile));
        changed = true;
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    Profile& active = config.active();

    if (string_field(tr.tr("profiles.name").data(), active.name, 240.0f)) {
        changed = true;
    }
    if (string_field(tr.tr("profiles.match_exe").data(), active.match_exe, 240.0f)) {
        changed = true;
    }
    ImGui::TextDisabled("%s", tr.tr("profiles.match_hint").data());

    if (ImGui::Checkbox(tr.tr("profiles.auto_switch").data(),
                        &config.auto_switch_profiles)) {
        changed = true;
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextDisabled("%s", tr.tr("profiles.preset_hint").data());

    if (ImGui::Button(tr.tr("profiles.preset_desktop").data())) {
        apply_preset(config, Preset::Desktop);
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(tr.tr("profiles.preset_numpad").data())) {
        apply_preset(config, Preset::Numpad);
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(tr.tr("profiles.preset_game").data())) {
        apply_preset(config, Preset::Game);
        changed = true;
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextDisabled("%s", tr.tr("profiles.share_hint").data());

    static std::string share_status;
    static bool share_ok = false;

    constexpr wchar_t kProfileFilter[] =
        L"KeyMouse profiles (*.json)\0*.json\0All files (*.*)\0*.*\0";

    if (ImGui::Button(tr.tr("profiles.export").data())) {
        if (const auto path = platform::save_file_dialog(
                L"Export KeyMouse profile", kProfileFilter,
                L"keymouse-profile.json")) {
            std::ofstream stream(*path, std::ios::binary | std::ios::trunc);
            if (!stream) {
                share_status = tr.tr("profiles.export_failed").data();
                share_ok = false;
            } else {
                const std::string text = profiles_to_json({active});
                stream.write(text.data(), static_cast<std::streamsize>(text.size()));
                stream.flush();
                if (!stream) {
                    share_status = tr.tr("profiles.export_failed").data();
                    share_ok = false;
                } else {
                    share_status = tr.tr("profiles.export_ok").data();
                    share_ok = true;
                }
            }
        }
    }

    ImGui::SameLine();
    if (ImGui::Button(tr.tr("profiles.import").data())) {
        if (const auto path = platform::open_file_dialog(
                L"Import KeyMouse profiles", kProfileFilter)) {
            std::ifstream stream(*path, std::ios::binary);
            if (!stream) {
                share_status = tr.tr("profiles.import_failed").data();
                share_ok = false;
            } else {
                const std::string text((std::istreambuf_iterator<char>(stream)),
                                       std::istreambuf_iterator<char>());
                std::string error;
                const auto imported = profiles_from_json(text, error);
                if (!imported) {
                    share_status = error;
                    share_ok = false;
                } else {
                    const size_t first_new = config.profiles.size();
                    for (auto profile : *imported) {
                        profile.name = unique_profile_name(config, profile.name);
                        config.profiles.push_back(std::move(profile));
                    }
                    config.active_profile = first_new;
                    changed = true;
                    share_status = tr.tr("profiles.import_ok").data();
                    share_ok = true;
                }
            }
        }
    }

    if (!share_status.empty()) {
        ImGui::TextColored(
            ImGui::ColorConvertU32ToFloat4(share_ok ? colors::kAccent : colors::kDanger),
            "%s", share_status.c_str());
    }

    if (changed) {
        *context.config_dirty = true;
    }
}

}  // namespace ui
}  // namespace km
