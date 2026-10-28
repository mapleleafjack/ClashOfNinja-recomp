#pragma once

#include <filesystem>
#include <string>

namespace nla {

namespace fs = std::filesystem;

fs::path desktop_folder();
// The Start menu's programs folder, ~/Applications on macOS, the XDG menu on Linux.
fs::path applications_folder();

// "Start menu shortcut" on Windows, "Applications shortcut" on macOS, "Menu shortcut"
// on Linux.
const wchar_t* shortcut_folder_label();
std::wstring shortcut_name();

bool create_shortcut(const fs::path& link, const fs::path& target, const fs::path& working_dir,
                     const std::wstring& description, std::wstring* error);

}  // namespace nla
