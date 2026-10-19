// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/fonts.h"

#include <filesystem>
#include <string>
#include <vector>

#include <windows.h>

namespace km {
namespace ui {
namespace {

constexpr float kBaseFontSize = 16.0f;

// Fonts are loaded from the system rather than bundled. Bundling Segoe UI plus
// a Japanese font would add several megabytes to a small binary; loading from
// C:\Windows\Fonts costs nothing and every supported Windows install has them.
//
// Since ImGui 1.92 the atlas loads glyphs on demand, so no glyph ranges are
// specified here: Cyrillic and Japanese characters are rasterised when they
// first appear. That also means no Build() call and no atlas rebuild needed
// when the language changes.
constexpr const wchar_t* kLatinFonts[] = {
    L"C:\\Windows\\Fonts\\segoeui.ttf",
    L"C:\\Windows\\Fonts\\arial.ttf",
};

constexpr const wchar_t* kJapaneseFonts[] = {
    L"C:\\Windows\\Fonts\\YuGothM.ttc",
    L"C:\\Windows\\Fonts\\meiryo.ttc",
    L"C:\\Windows\\Fonts\\msgothic.ttc",
};

// Returns a stable, UTF-8 path to the first font that exists.
const std::string* first_existing(const wchar_t* const* candidates, size_t count) {
    static std::vector<std::string> storage;
    for (size_t i = 0; i < count; ++i) {
        const std::filesystem::path path(candidates[i]);
        std::error_code ec;
        if (std::filesystem::exists(path, ec)) {
            storage.push_back(path.string());
            return &storage.back();
        }
    }
    return nullptr;
}

}  // namespace

void load_fonts(float dpi_scale) {
    ImGuiIO& io = ImGui::GetIO();
    const float size = kBaseFontSize * dpi_scale;

    ImFontConfig config;
    config.OversampleH = 2;
    config.OversampleV = 1;
    config.PixelSnapH = false;

    ImFont* base = nullptr;
    if (const std::string* latin = first_existing(kLatinFonts, std::size(kLatinFonts))) {
        base = io.Fonts->AddFontFromFileTTF(latin->c_str(), size, &config);
    }
    if (!base) {
        // The embedded default font is small but always available, so the UI
        // still renders if the system fonts are missing.
        ImFontConfig fallback;
        fallback.SizePixels = size;
        base = io.Fonts->AddFontDefault(&fallback);
    }
    io.FontDefault = base;

    // Merge a Japanese face so kanji and kana render. Latin and Cyrillic come
    // from Segoe UI above; merging keeps one font entry, so every widget uses
    // a single font without per-language branching.
    if (const std::string* japanese = first_existing(kJapaneseFonts, std::size(kJapaneseFonts))) {
        ImFontConfig merge;
        merge.MergeMode = true;
        merge.OversampleH = 1;
        merge.OversampleV = 1;
        io.Fonts->AddFontFromFileTTF(japanese->c_str(), size, &merge);
    }
}

void rebuild_fonts(float dpi_scale) {
    // ClearFonts() drops the font data; the atlas rebuilds on the next frame
    // because the DX11 backend advertises ImGuiBackendFlags_RendererHasTextures.
    ImGui::GetIO().Fonts->ClearFonts();
    load_fonts(dpi_scale);
}

}  // namespace ui
}  // namespace km
