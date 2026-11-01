#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace nla {

namespace fs = std::filesystem;

fs::path desktop_folder();
// The Start menu's programs folder, ~/Applications on macOS, the XDG menu on Linux.
fs::path applications_folder();

// "Start menu shortcut" on Windows, "Applications shortcut" on macOS, "Menu shortcut"
// on Linux.
const wchar_t* shortcut_folder_label();
std::wstring shortcut_name();

// `name` is what the entry is called in the menu, and `args` the ones it starts `target`
// with. A desktop entry can carry both; a macOS symlink has nowhere to put the arguments,
// so they are dropped there, which is why the entry for a game passes only what the game
// would default to anyway.
bool create_shortcut(const fs::path& link, const fs::path& target, const fs::path& working_dir,
                     const std::wstring& name, const std::wstring& description,
                     const std::vector<std::wstring>& args, std::wstring* error);

// The file name of one game's entry. Keyed by the game's folder so it stays the same
// across builds and a second build replaces the first rather than adding another.
std::wstring game_entry_name(const std::wstring& game_folder);

}  // namespace nla
