// Where things are: the launcher's own executable and the folder it runs from, the home and
// data directories, and the environment. The only file here that has to know what platform
// it is on twice over - Windows asks the system, macOS asks dyld, everything else reads
// /proc.

#include "paths.h"

#include "text.h"

#include <fstream>
#include <sstream>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <unistd.h>
#else
#include <unistd.h>
#endif

namespace nla {

wchar_t path_list_separator() {
#ifdef _WIN32
    return L';';
#else
    return L':';
#endif
}

const wchar_t* exe_suffix() {
#ifdef _WIN32
    return L".exe";
#else
    return L"";
#endif
}

wstring read_text_file(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};
    std::ostringstream stream;
    stream << file.rdbuf();
    return widen(stream.str());
}

bool write_text_file(const fs::path& path, std::string_view bytes) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return file.good();
}

uint64_t file_size(const fs::path& path) {
    std::error_code ec;
    auto size = fs::file_size(path, ec);
    return ec ? 0 : static_cast<uint64_t>(size);
}

wstring exe_path() {
#if defined(_WIN32)
    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;) {
        DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
                                          static_cast<DWORD>(buffer.size()));
        if (length == 0) return {};
        if (length < buffer.size()) return wstring(buffer.data(), length);
        buffer.resize(buffer.size() * 2);
    }
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buffer(size + 1, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) return {};
    std::error_code ec;
    auto resolved = fs::canonical(buffer.data(), ec);
    return widen(ec ? std::string(buffer.data()) : resolved.string());
#else
    std::error_code ec;
    auto resolved = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) return widen(resolved.string());
    return {};
#endif
}

wstring exe_dir() {
    return fs::path(exe_path()).parent_path().wstring();
}

fs::path bundle_resources_dir() {
#if defined(__APPLE__)
    // A bundled executable sits in Contents/MacOS; anything else, such as the plain
    // binary a developer runs from the build folder, has no resources of its own.
    fs::path contents = fs::path(exe_dir()).parent_path();
    std::error_code ec;
    if (to_lower(contents.filename().wstring()) == L"contents" &&
        fs::is_directory(contents / L"Resources", ec)) {
        return contents / L"Resources";
    }
#endif
    return {};
}

fs::path home_dir() {
#if defined(_WIN32)
    auto profile = environment(L"USERPROFILE");
    if (!profile.empty()) return fs::path(profile);
    auto drive = environment(L"HOMEDRIVE");
    auto path = environment(L"HOMEPATH");
    if (!drive.empty() && !path.empty()) return fs::path(drive + path);
    return {};
#else
    auto home = environment(L"HOME");
    return home.empty() ? fs::path() : fs::path(home);
#endif
}

fs::path user_data_dir() {
#if defined(_WIN32)
    auto appdata = environment(L"APPDATA");
    fs::path base = appdata.empty() ? home_dir() : fs::path(appdata);
    return base / L"NarutoLauncher";
#elif defined(__APPLE__)
    return home_dir() / L"Library" / L"Application Support" / L"NarutoLauncher";
#else
    auto config = environment(L"XDG_CONFIG_HOME");
    fs::path base = config.empty() ? (home_dir() / L".config") : fs::path(config);
    return base / L"NarutoLauncher";
#endif
}

fs::path temp_dir() {
    std::error_code ec;
    auto path = fs::temp_directory_path(ec);
    return ec ? fs::path(L"/tmp") : path;
}

unsigned long process_id() {
#if defined(_WIN32)
    return GetCurrentProcessId();
#else
    return static_cast<unsigned long>(::getpid());
#endif
}

wstring path_display(const fs::path& path) {
    wstring text = path.wstring();
    auto home = home_dir().wstring();
    if (!home.empty() && text.size() > home.size() && text.compare(0, home.size(), home) == 0 &&
        (text[home.size()] == fs::path::preferred_separator || text[home.size()] == L'/')) {
        return L"~" + text.substr(home.size());
    }
    return text;
}

wstring environment(const wstring& name) {
#if defined(_WIN32)
    DWORD length = GetEnvironmentVariableW(name.c_str(), nullptr, 0);
    if (length == 0) return {};
    wstring value(length, L'\0');
    DWORD written = GetEnvironmentVariableW(name.c_str(), value.data(), length);
    if (written == 0) return {};
    value.resize(written);
    return value;
#else
    auto value = std::getenv(narrow(name).c_str());
    return value ? widen(value) : wstring();
#endif
}

void set_environment(const wstring& name, const wstring& value) {
#if defined(_WIN32)
    SetEnvironmentVariableW(name.c_str(), value.c_str());
#else
    setenv(narrow(name).c_str(), narrow(value).c_str(), 1);
#endif
}
}  // namespace nla
