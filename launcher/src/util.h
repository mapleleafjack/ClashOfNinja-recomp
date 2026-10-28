#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nla {

namespace fs = std::filesystem;
using std::wstring;
using std::wstring_view;

// Text is UTF-16 on Windows and UTF-32 elsewhere; these convert to and from UTF-8.
wstring widen(std::string_view utf8);
std::string narrow(wstring_view text);
std::vector<uint32_t> codepoints(wstring_view text);

wstring format(const wchar_t* fmt, ...);
wstring trim(wstring text);
std::vector<wstring> split(wstring_view text, wchar_t separator);
wstring join(const std::vector<wstring>& parts, wstring_view separator);
wstring to_lower(wstring text);
bool ends_with_ci(wstring_view text, wstring_view suffix);
wstring quote_arg(wstring_view arg);
wstring command_line(const wstring& exe, const std::vector<wstring>& args);

wstring read_text_file(const fs::path& path);
bool write_text_file(const fs::path& path, std::string_view bytes);

// Runs a program and keeps what it prints, without showing a window. For short commands
// whose answer the launcher reads, like curl's or git's.
bool capture_output(const std::wstring& exe, const std::vector<std::wstring>& args,
                    std::string* output, int* status = nullptr);
uint64_t file_size(const fs::path& path);

wstring exe_path();
wstring exe_dir();
unsigned long process_id();
fs::path home_dir();
fs::path user_data_dir();
fs::path temp_dir();

// The character that separates directories in PATH; ';' on Windows, ':' elsewhere.
wchar_t path_list_separator();
// ".exe" on Windows, empty elsewhere.
const wchar_t* exe_suffix();

wstring sha1_hex(const void* data, size_t size);
wstring sha1_file(const fs::path& path, wstring* error = nullptr);

wstring path_display(const fs::path& path);

wstring environment(const wstring& name);
void set_environment(const wstring& name, const wstring& value);

bool open_path(const fs::path& path, wstring* error);
bool open_url(const std::wstring& url, wstring* error = nullptr);

// Starts a program and returns at once, so the program outlives the launcher. Used for
// the helper that swaps in an update once this process has exited.
bool start_detached(const std::wstring& exe, const std::vector<std::wstring>& args,
                    wstring* error = nullptr);

// A short, human-readable description of the last operating-system error.
wstring last_error();

}  // namespace nla
