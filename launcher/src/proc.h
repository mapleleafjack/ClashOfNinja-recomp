#pragma once

#include <string>
#include <vector>

#include "paths.h"

namespace nla {

// An argument quoted the way the platform's own programs expect to receive it, and a whole
// command line built from one and its arguments.
wstring quote_arg(wstring_view arg);
wstring command_line(const wstring& exe, const std::vector<wstring>& args);

// Runs a program and keeps what it prints, without showing a window. For short commands
// whose answer the launcher reads, like curl's or git's.
bool capture_output(const std::wstring& exe, const std::vector<std::wstring>& args,
                    std::string* output, int* status = nullptr);

bool open_path(const fs::path& path, wstring* error);
bool open_url(const std::wstring& url, wstring* error = nullptr);

// Starts a program and returns at once, so the program outlives the launcher. Used for
// the helper that swaps in an update once this process has exited.
bool start_detached(const std::wstring& exe, const std::vector<std::wstring>& args,
                    wstring* error = nullptr);

// A short, human-readable description of the last operating-system error.
wstring last_error();

}  // namespace nla
