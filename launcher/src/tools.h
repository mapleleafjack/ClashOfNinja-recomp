#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace nla {

namespace fs = std::filesystem;

struct ToolInfo {
    std::wstring name;
    std::wstring path;
    std::wstring hint;
    bool required = true;

    bool found() const { return !path.empty(); }
};

// Puts the discovered tool directories on this process's PATH, so every child
// process (cmake, ninja, the compiler, python) sees the same tools.
void initialise_environment();
void rescan_environment();

const std::wstring& search_path();
std::wstring find_tool(const std::wstring& executable);
// The first of `known` that exists, else `name` from the search path. For a tool that
// ships with the system in a fixed place but may also be installed elsewhere.
std::wstring find_tool_from(const std::vector<std::wstring>& known, const std::wstring& name);

std::vector<ToolInfo> check_tools();

// Tools that shipped beside the launcher, if any.
fs::path launcher_tools_dir();
fs::path bundled_sdl2_prefix();

}  // namespace nla
