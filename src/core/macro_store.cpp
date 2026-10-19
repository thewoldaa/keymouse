// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/macro_store.h"

#include <algorithm>
#include <fstream>

namespace km {
namespace macro_store {
namespace {

namespace fs = std::filesystem;

}  // namespace

fs::path macro_file(const fs::path& dir, std::string_view id) {
    return dir / (std::string(id) + std::string(kMacroExtension));
}

bool save(const fs::path& dir, const Macro& macro, std::string& error) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        error = "could not create the macro directory: " + ec.message();
        return false;
    }

    const std::string id = macro_file_stem(macro.id.empty() ? macro.name : macro.id);
    const fs::path target = macro_file(dir, id);
    const fs::path temp = target.string() + ".tmp";

    {
        std::ofstream stream(temp, std::ios::binary | std::ios::trunc);
        if (!stream) {
            error = "could not open the macro file for writing";
            return false;
        }
        const std::string text = macro_to_json(macro);
        stream.write(text.data(), static_cast<std::streamsize>(text.size()));
        stream.flush();
        if (!stream) {
            error = "could not write the macro file";
            fs::remove(temp, ec);
            return false;
        }
    }

    // Replace the old file in one step so a crash cannot leave a half-written
    // macro behind: move_file overwrites on Windows.
    fs::rename(temp, target, ec);
    if (ec) {
        // rename() fails when the target exists on some platforms; fall back to
        // remove-then-rename, which still leaves either the old or the new file.
        fs::remove(target, ec);
        ec.clear();
        fs::rename(temp, target, ec);
        if (ec) {
            error = "could not replace the macro file: " + ec.message();
            fs::remove(temp, ec);
            return false;
        }
    }
    return true;
}

std::optional<Macro> load(const fs::path& dir, std::string_view id,
                          std::string& error) {
    const fs::path path = macro_file(dir, id);
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        error = "macro file not found";
        return std::nullopt;
    }
    const std::string text((std::istreambuf_iterator<char>(stream)),
                           std::istreambuf_iterator<char>());
    auto macro = macro_from_json(text, error);
    if (!macro) {
        return std::nullopt;
    }
    // The file name wins over the stored id: the library resolves macros by
    // file, and a file copied in from elsewhere must not keep claiming its old
    // identity.
    macro->id = std::string(id);
    return macro;
}

bool remove_file(const fs::path& dir, std::string_view id) {
    std::error_code ec;
    fs::remove(macro_file(dir, id), ec);
    // A missing file is not an error: the user may have deleted it by hand and
    // the entry in the config is what needs to go away.
    return !ec || !fs::exists(macro_file(dir, id));
}

std::string unique_id(const fs::path& dir, std::string_view base,
                      const std::vector<std::string>& taken) {
    const std::string stem = macro_file_stem(base);
    std::string candidate = stem;
    for (int suffix = 2;; ++suffix) {
        const bool in_use =
            std::find(taken.begin(), taken.end(), candidate) != taken.end() ||
            fs::exists(macro_file(dir, candidate));
        if (!in_use) {
            return candidate;
        }
        candidate = stem + '-' + std::to_string(suffix);
    }
}

}  // namespace macro_store
}  // namespace km
