// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/macro.h"

namespace km {

// Reading and writing .kmacro files.
//
// The directory is passed in rather than resolved here so the module stays
// platform independent and testable; the application hands it
// paths::data_dir() / "macros".
namespace macro_store {

inline constexpr std::string_view kMacroExtension = ".kmacro";

// <dir>/<id>.kmacro
std::filesystem::path macro_file(const std::filesystem::path& dir,
                                 std::string_view id);

// Writes the file with an atomic replace, creating the directory when missing.
// The macro's id doubles as the file stem and must already be sanitized.
bool save(const std::filesystem::path& dir, const Macro& macro, std::string& error);

// Reads <id>.kmacro. Returns nullopt with a human-readable error.
std::optional<Macro> load(const std::filesystem::path& dir, std::string_view id,
                          std::string& error);

// Deletes the file. Missing files count as success.
bool remove_file(const std::filesystem::path& dir, std::string_view id);

// An id that is neither present on disk nor in `taken`, derived from `base`.
std::string unique_id(const std::filesystem::path& dir, std::string_view base,
                      const std::vector<std::string>& taken);

}  // namespace macro_store
}  // namespace km
