#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "text.h"

namespace nla {

namespace fs = std::filesystem;

// A whole file as text, and the way back. The launcher's files are all small enough to hold.
wstring read_text_file(const fs::path& path);
bool write_text_file(const fs::path& path, std::string_view bytes);

uint64_t file_size(const fs::path& path);

wstring exe_path();
wstring exe_dir();
unsigned long process_id();
// The Resources folder of the macOS bundle the launcher runs from; empty elsewhere, and
// for an executable that is not inside one.
fs::path bundle_resources_dir();
fs::path home_dir();
fs::path user_data_dir();
fs::path temp_dir();

// The character that separates directories in PATH; ';' on Windows, ':' elsewhere.
wchar_t path_list_separator();
// ".exe" on Windows, empty elsewhere.
const wchar_t* exe_suffix();

// A path as the panels show it: an absolute one under the home directory loses that prefix
// and starts with ~ instead.
wstring path_display(const fs::path& path);

wstring environment(const wstring& name);
void set_environment(const wstring& name, const wstring& value);

}  // namespace nla
