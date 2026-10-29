#include "tools.h"

#include "jobs.h"
#include "util.h"

#include <algorithm>
#include <cstdlib>
#include <set>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace nla {
namespace {

std::wstring g_search_path;

#if defined(_WIN32)
wstring read_registry_path(HKEY root, const wchar_t* subkey) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subkey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return {};
    DWORD type = 0;
    DWORD bytes = 0;
    if (RegQueryValueExW(key, L"Path", nullptr, &type, nullptr, &bytes) != ERROR_SUCCESS ||
        bytes == 0) {
        RegCloseKey(key);
        return {};
    }
    std::vector<wchar_t> buffer(bytes / sizeof(wchar_t) + 1, L'\0');
    if (RegQueryValueExW(key, L"Path", nullptr, &type,
                         reinterpret_cast<LPBYTE>(buffer.data()), &bytes) != ERROR_SUCCESS) {
        RegCloseKey(key);
        return {};
    }
    RegCloseKey(key);
    wstring value(buffer.data());
    if (type == REG_EXPAND_SZ) {
        std::vector<wchar_t> expanded(32768);
        DWORD length = ExpandEnvironmentStringsW(value.c_str(), expanded.data(),
                                                static_cast<DWORD>(expanded.size()));
        if (length > 0 && length <= expanded.size()) value = expanded.data();
    }
    return value;
}

bool holds_tool(const fs::path& dir) {
    for (const wchar_t* name : {L"cmake.exe", L"ninja.exe", L"g++.exe", L"clang++.exe",
                                L"python.exe"}) {
        std::error_code ec;
        if (fs::is_regular_file(dir / name, ec)) return true;
    }
    return false;
}

// Packages installed with winget live under LOCALAPPDATA with a couple of levels of
// nesting, so a shallow walk finds their bin directories without scanning the whole tree.
void find_tool_dirs(const fs::path& root, int depth, std::vector<fs::path>& out) {
    std::error_code ec;
    if (depth < 0 || !fs::is_directory(root, ec)) return;
    if (holds_tool(root)) {
        out.push_back(root);
        return;
    }
    for (const auto& entry : fs::directory_iterator(
             root, fs::directory_options::skip_permission_denied, ec)) {
        std::error_code item_ec;
        if (!entry.is_directory(item_ec)) continue;
        auto name = to_lower(entry.path().filename().wstring());
        if (name == L"share" || name == L"include" || name == L"lib" || name == L"src" ||
            name == L"doc" || name == L"man" || name == L"locale" || name == L"licenses") {
            continue;
        }
        find_tool_dirs(entry.path(), depth - 1, out);
    }
}
#endif

std::wstring build_search_path() {
    std::vector<fs::path> dirs;
    std::set<wstring> seen;
    auto add = [&](const fs::path& dir) {
        if (dir.empty()) return;
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) return;
        if (seen.insert(to_lower(dir.wstring())).second) dirs.push_back(dir);
    };
    auto add_list = [&](const wstring& value, wchar_t separator) {
        for (const auto& piece : split(value, separator)) add(fs::path(piece));
    };

    // Tools shipped beside the launcher come first, so a release uses its own copies.
    fs::path bundled = launcher_tools_dir();
    add(bundled);
    for (const wchar_t* sub : {L"cmake/bin", L"ninja", L"mingw64/bin", L"python",
                               L"python/bin", L"nodtool", L"dtk", L"sdl2/bin"}) {
        add(bundled / sub);
    }

#if defined(_WIN32)
    add_list(environment(L"PATH"), L';');
    // A launcher started from a shortcut can hold a PATH older than the one in the
    // registry, so read the registry's too.
    add_list(read_registry_path(HKEY_CURRENT_USER, L"Environment"), L';');
    add_list(read_registry_path(
                 HKEY_LOCAL_MACHINE,
                 L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment"),
             L';');

    fs::path packages = fs::path(environment(L"LOCALAPPDATA")) / L"Microsoft" / L"WinGet" /
                        L"Packages";
    std::vector<fs::path> found;
    find_tool_dirs(packages, 3, found);
    for (const auto& dir : found) add(dir);

    add(fs::path(environment(L"ProgramFiles")) / L"CMake" / L"bin");
    add(fs::path(environment(L"ProgramFiles")) / L"Microsoft" / L"Visual Studio" / L"2022");
#else
    add_list(environment(L"PATH"), L':');
    // A launcher started from Finder or a desktop file gets a minimal PATH, so add the
    // places a package manager installs into.
    add(L"/opt/homebrew/bin");
    add(L"/opt/homebrew/sbin");
    add(L"/usr/local/bin");
    add(L"/usr/local/sbin");
    add(home_dir() / L".cargo" / L"bin");
    add(home_dir() / L".local" / L"bin");
    add(L"/usr/bin");
    add(L"/bin");
#endif

    std::vector<wstring> text;
    text.reserve(dirs.size());
    for (const auto& dir : dirs) text.push_back(dir.wstring());
    return join(text, wstring(1, path_list_separator()));
}

bool is_executable(const fs::path& path) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) return false;
#if defined(_WIN32)
    return true;
#else
    return ::access(path.c_str(), X_OK) == 0;
#endif
}

// The dtk release the launcher fetches when it has none of its own. build-release.ps1 and
// the macOS bundle name the same version, so every route runs one build.
constexpr wchar_t kDtkVersion[] = L"v1.8.4";

// The asset dtk publishes for the machine this launcher was built for.
std::wstring dtk_asset() {
#if defined(_WIN32)
    return L"dtk-windows-x86_64.exe";
#elif defined(__APPLE__)
#if defined(__aarch64__) || defined(__arm64__)
    return L"dtk-macos-arm64";
#else
    return L"dtk-macos-x86_64";
#endif
#else
#if defined(__aarch64__)
    return L"dtk-linux-aarch64";
#else
    return L"dtk-linux-x86_64";
#endif
#endif
}

}  // namespace

std::wstring curl_tool() {
#if defined(_WIN32)
    return find_tool_from({L"C:\\Windows\\System32\\curl.exe"}, L"curl.exe");
#else
    return find_tool_from({L"/usr/bin/curl", L"/usr/local/bin/curl", L"/opt/homebrew/bin/curl"},
                          L"curl");
#endif
}

std::wstring dtk_download_url() {
    return std::wstring(L"https://github.com/encounter/decomp-toolkit/releases/download/") +
           kDtkVersion + L"/" + dtk_asset();
}

std::wstring ensure_dtk(Job& job, std::wstring* error) {
    auto have = find_tool(std::wstring(L"dtk") + exe_suffix());
    if (!have.empty()) return have;

    auto curl = curl_tool();
    if (curl.empty()) {
        if (error) *error = L"curl was not found, and the launcher uses it to fetch dtk";
        return {};
    }

    // Beside the launcher is where a release keeps its tools. A copy that cannot be
    // written into (a signed bundle, an install under Program Files) gets one in the
    // user's data instead, and that path is used directly.
    std::vector<fs::path> dirs;
    if (bundle_resources_dir().empty()) dirs.push_back(launcher_tools_dir() / L"dtk");
    dirs.push_back(user_data_dir() / L"tools" / L"dtk");

    auto url = dtk_download_url();
    job.log(format(L"[launcher] fetching dtk %ls\n", kDtkVersion));
    for (const auto& dir : dirs) {
        std::error_code ec;
        fs::create_directories(dir, ec);
        if (ec) continue;
        fs::path out = dir / (std::wstring(L"dtk") + exe_suffix());
        job.log(L"$ curl -fsSL " + url + L" -o " + out.wstring() + L"\n");
        int code = job.run_process(curl,
                                   {L"-fsSL", L"--retry", L"2", L"--connect-timeout", L"15",
                                    L"-o", out.wstring(), url},
                                   fs::path());
        if (code != 0) {
            fs::remove(out, ec);
            continue;
        }
#ifndef _WIN32
        fs::permissions(out,
                        fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                        fs::perm_options::add, ec);
#endif
        job.log(L"[launcher] dtk is in " + out.wstring() + L"\n");
        rescan_environment();
        return out.wstring();
    }
    if (error) *error = L"the download did not work; install dtk from "
                        L"https://github.com/encounter/decomp-toolkit/releases";
    return {};
}

int fetch_dtk_to_log(const fs::path& log) {
    Task task{L"Fetching dtk…", [](Job& job) -> int {
                  std::wstring error;
                  if (ensure_dtk(job, &error).empty()) {
                      job.log(error + L"\n");
                      return 1;
                  }
                  return 0;
              }};
    return run_tasks_to_log(std::vector<Task>{std::move(task)}, log);
}

fs::path launcher_tools_dir() {
    // A macOS release keeps its tools inside the bundle, where they travel with the
    // .app the player drags into place; every other release keeps them in tools beside
    // the executable.
    fs::path resources = bundle_resources_dir();
    if (!resources.empty()) {
        std::error_code ec;
        fs::path bundled = resources / L"tools";
        if (fs::is_directory(bundled, ec)) return bundled;
    }
    return fs::path(exe_dir()) / L"tools";
}

fs::path bundled_sdl2_prefix() {
    std::vector<fs::path> candidates;
    candidates.push_back(launcher_tools_dir() / L"sdl2");

#if defined(_WIN32)
    // A development build can borrow the copy gcn-recomp builds once per machine.
    fs::path root = fs::path(environment(L"LOCALAPPDATA")) / L"gcn-recomp";
    std::error_code ec;
    if (fs::is_directory(root, ec)) {
        for (const auto& entry : fs::directory_iterator(root, ec)) {
            std::error_code item_ec;
            if (!entry.is_directory(item_ec)) continue;
            if (entry.path().filename().wstring().rfind(L"SDL2-", 0) == 0)
                candidates.push_back(entry.path());
        }
    }
#endif

    for (const auto& candidate : candidates) {
        std::error_code check_ec;
        if (fs::is_regular_file(candidate / L"SDL2Config.cmake", check_ec) ||
            fs::is_regular_file(candidate / L"lib" / L"cmake" / L"SDL2" / L"SDL2Config.cmake",
                                check_ec)) {
            return candidate;
        }
    }
    return {};
}

void initialise_environment() {
    g_search_path = build_search_path();
    set_environment(L"PATH", g_search_path);
    set_environment(L"PYTHONUTF8", L"1");
}

void rescan_environment() {
    g_search_path.clear();
    initialise_environment();
}

const std::wstring& search_path() {
    if (g_search_path.empty()) initialise_environment();
    return g_search_path;
}

std::wstring find_tool(const std::wstring& executable) {
    if (executable.empty()) return {};
    if (executable.find(L'/') != wstring::npos || executable.find(L'\\') != wstring::npos) {
        return is_executable(fs::path(executable)) ? executable : wstring();
    }
    for (const auto& piece : split(search_path(), path_list_separator())) {
        fs::path candidate = fs::path(piece) / executable;
        if (is_executable(candidate)) return candidate.wstring();
    }
    return {};
}

std::wstring find_tool_from(const std::vector<std::wstring>& known,
                            const std::wstring& name) {
    for (const auto& candidate : known) {
        auto found = find_tool(candidate);
        if (!found.empty()) return found;
    }
    return find_tool(name);
}

#if defined(__APPLE__)
bool command_line_tools_installed() {
    std::error_code ec;
    if (fs::is_regular_file(fs::path(L"/Library/Developer/CommandLineTools/usr/bin/clang"),
                            ec))
        return true;
    // Xcode brings the same SDK; it is installed from the App Store, and a beta keeps its
    // own name there.
    for (const auto& entry :
         fs::directory_iterator(fs::path(L"/Applications"),
                                fs::directory_options::skip_permission_denied, ec)) {
        auto name = to_lower(entry.path().filename().wstring());
        if (name.rfind(L"xcode", 0) != 0) continue;
        if (fs::is_regular_file(entry.path() / L"Contents" / L"Developer" / L"usr" / L"bin" /
                                    L"clang",
                                ec))
            return true;
    }
    // Then whatever xcode-select points at, since Xcode can live anywhere.
    auto configured = environment(L"DEVELOPER_DIR");
    return !configured.empty() &&
           fs::is_regular_file(fs::path(configured) / L"usr" / L"bin" / L"clang", ec);
}
#endif

std::vector<ToolInfo> check_tools() {
    std::vector<ToolInfo> tools;
#if defined(_WIN32)
    tools.push_back({L"cmake", find_tool(L"cmake.exe"), L"winget install Kitware.CMake", true});
    tools.push_back({L"ninja", find_tool(L"ninja.exe"), L"winget install Ninja-build.Ninja",
                     true});

    wstring compiler = find_tool(L"g++.exe");
    if (compiler.empty()) compiler = find_tool(L"clang++.exe");
    if (compiler.empty()) compiler = find_tool(L"cl.exe");
    tools.push_back({L"compiler", compiler,
                     L"install WinLibs MinGW-w64 (bundled with the release build)", true});

    auto python = find_tool(L"python.exe");
    if (python.empty()) python = find_tool(L"python3.exe");
    tools.push_back({L"python", python, L"winget install Python.Python.3.12", true});

    tools.push_back({L"nodtool", find_tool(L"nodtool.exe"),
                     L"bundled with the release, or cargo install --locked nodtool "
                     L"(only for RVZ/WIA/GCZ images)",
                     false});
#elif defined(__APPLE__)
    tools.push_back({L"cmake", find_tool(L"cmake"), L"brew install cmake", true});
    tools.push_back({L"ninja", find_tool(L"ninja"), L"brew install ninja", true});

    // /usr/bin/c++ and /usr/bin/python3 are shims until Apple's tools are installed: they
    // open the installer instead of running. A compiler is only usable with the SDK those
    // tools bring, so no developer folder means no compiler, wherever it came from; a
    // python3 from Homebrew is a real interpreter and counts as it is.
    bool apple = command_line_tools_installed();
    wstring compiler = find_tool(L"c++");
    if (compiler.empty()) compiler = find_tool(L"clang++");
    if (compiler.empty()) compiler = find_tool(L"g++");
    if (!apple) compiler.clear();
    tools.push_back({L"compiler", compiler, L"Apple's command line tools", true});

    auto python = find_tool(L"python3");
    if (python.empty()) python = find_tool(L"python");
    if (!apple && python.rfind(L"/usr/bin/", 0) == 0) python.clear();
    tools.push_back({L"python", python, L"Apple's command line tools, or brew install python",
                     true});

    tools.push_back({L"nodtool", find_tool(L"nodtool"),
                     L"cargo install --locked nodtool (only for RVZ/WIA/GCZ images)", false});
#else
    tools.push_back({L"cmake", find_tool(L"cmake"), L"sudo apt install cmake", true});
    tools.push_back({L"ninja", find_tool(L"ninja"), L"sudo apt install ninja-build", true});

    wstring compiler = find_tool(L"c++");
    if (compiler.empty()) compiler = find_tool(L"g++");
    if (compiler.empty()) compiler = find_tool(L"clang++");
    tools.push_back({L"compiler", compiler, L"sudo apt install build-essential", true});

    auto python = find_tool(L"python3");
    if (python.empty()) python = find_tool(L"python");
    tools.push_back({L"python", python, L"sudo apt install python3", true});

    tools.push_back({L"nodtool", find_tool(L"nodtool"),
                     L"cargo install --locked nodtool (only for RVZ/WIA/GCZ images)", false});
#endif
    return tools;
}

}  // namespace nla
