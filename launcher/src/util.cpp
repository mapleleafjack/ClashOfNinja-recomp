#include "util.h"

#include <algorithm>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <fstream>
#include <sstream>

#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <sys/wait.h>
#include <unistd.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace nla {

wstring widen(std::string_view utf8) {
    wstring out;
    out.reserve(utf8.size());
    size_t i = 0;
    auto next = [&](uint32_t& cp) -> bool {
        if (i >= utf8.size()) return false;
        auto byte = static_cast<unsigned char>(utf8[i]);
        int extra = 0;
        if (byte < 0x80) {
            cp = byte;
        } else if ((byte & 0xE0) == 0xC0) {
            cp = byte & 0x1F;
            extra = 1;
        } else if ((byte & 0xF0) == 0xE0) {
            cp = byte & 0x0F;
            extra = 2;
        } else if ((byte & 0xF8) == 0xF0) {
            cp = byte & 0x07;
            extra = 3;
        } else {
            cp = 0xFFFD;
            ++i;
            return true;
        }
        ++i;
        for (int n = 0; n < extra; ++n, ++i) {
            if (i >= utf8.size() || (static_cast<unsigned char>(utf8[i]) & 0xC0) != 0x80) {
                cp = 0xFFFD;
                return true;
            }
            cp = (cp << 6) | (static_cast<unsigned char>(utf8[i]) & 0x3F);
        }
        return true;
    };
    uint32_t cp = 0;
    while (next(cp)) {
        if (sizeof(wchar_t) == 2 && cp > 0xFFFF) {
            cp -= 0x10000;
            out.push_back(static_cast<wchar_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<wchar_t>(0xDC00 + (cp & 0x3FF)));
        } else {
            out.push_back(static_cast<wchar_t>(cp));
        }
    }
    return out;
}

std::string narrow(wstring_view text) {
    std::string out;
    out.reserve(text.size());
    auto emit = [&out](uint32_t cp) {
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    };
    for (size_t i = 0; i < text.size(); ++i) {
        uint32_t cp = static_cast<uint32_t>(text[i]);
        if (sizeof(wchar_t) == 2 && cp >= 0xD800 && cp <= 0xDBFF && i + 1 < text.size()) {
            uint32_t low = static_cast<uint32_t>(text[i + 1]);
            if (low >= 0xDC00 && low <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                ++i;
            }
        }
        emit(cp);
    }
    return out;
}

std::vector<uint32_t> codepoints(wstring_view text) {
    std::vector<uint32_t> out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        uint32_t cp = static_cast<uint32_t>(text[i]);
        if (sizeof(wchar_t) == 2 && cp >= 0xD800 && cp <= 0xDBFF && i + 1 < text.size()) {
            uint32_t low = static_cast<uint32_t>(text[i + 1]);
            if (low >= 0xDC00 && low <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                ++i;
            }
        }
        out.push_back(cp);
    }
    return out;
}

wstring format(const wchar_t* fmt, ...) {
    wchar_t buffer[4096];
    va_list args;
    va_start(args, fmt);
    int count = vswprintf(buffer, std::size(buffer) - 1, fmt, args);
    va_end(args);
    if (count < 0) count = static_cast<int>(std::size(buffer)) - 1;
    buffer[count] = L'\0';
    return wstring(buffer, static_cast<size_t>(count));
}

wstring trim(wstring text) {
    auto space = [](wchar_t c) { return iswspace(static_cast<wint_t>(c)) != 0; };
    text.erase(text.begin(), std::find_if_not(text.begin(), text.end(), space));
    text.erase(std::find_if_not(text.rbegin(), text.rend(), space).base(), text.end());
    return text;
}

std::vector<wstring> split(wstring_view text, wchar_t separator) {
    std::vector<wstring> parts;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find(separator, start);
        if (end == wstring_view::npos) end = text.size();
        if (end > start) parts.emplace_back(text.substr(start, end - start));
        start = end + 1;
    }
    return parts;
}

wstring join(const std::vector<wstring>& parts, wstring_view separator) {
    wstring out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) out.append(separator);
        out.append(parts[i]);
    }
    return out;
}

wstring to_lower(wstring text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(towlower(static_cast<wint_t>(c))); });
    return text;
}

bool ends_with_ci(wstring_view text, wstring_view suffix) {
    if (suffix.size() > text.size()) return false;
    return to_lower(wstring(text.substr(text.size() - suffix.size()))) ==
           to_lower(wstring(suffix));
}

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

wstring quote_arg(wstring_view arg) {
#ifdef _WIN32
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == wstring_view::npos) {
        return wstring(arg);
    }
    wstring out = L"\"";
    for (auto it = arg.begin();; ++it) {
        unsigned backslashes = 0;
        while (it != arg.end() && *it == L'\\') {
            ++it;
            ++backslashes;
        }
        if (it == arg.end()) {
            out.append(backslashes * 2, L'\\');
            break;
        }
        if (*it == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out.push_back(L'"');
        } else {
            out.append(backslashes, L'\\');
            out.push_back(*it);
        }
    }
    out.push_back(L'"');
    return out;
#else
    // Single quotes: everything is literal, and only ' needs escaping.
    if (!arg.empty() && arg.find_first_of(L" \t\n'\"\\$`!*?[](){}<>|&;#~") == wstring_view::npos) {
        return wstring(arg);
    }
    wstring out = L"'";
    for (wchar_t character : arg) {
        if (character == L'\'') {
            out += L"'\\''";
        } else {
            out.push_back(character);
        }
    }
    out.push_back(L'\'');
    return out;
#endif
}

wstring command_line(const wstring& exe, const std::vector<wstring>& args) {
    wstring line = quote_arg(exe);
    for (const auto& arg : args) {
        line.push_back(L' ');
        line.append(quote_arg(arg));
    }
    return line;
}

wstring read_text_file(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};
    std::ostringstream stream;
    stream << file.rdbuf();
    return widen(stream.str());
}

bool capture_output(const std::wstring& exe, const std::vector<std::wstring>& args,
                    std::string* output, int* status) {
#if defined(_WIN32)
    // Not _wpopen: that runs through cmd.exe, which mangles a quoted path that contains a
    // space (C:\Program Files\Git\...). A pipe of our own takes the command line as is.
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE read = nullptr, write = nullptr;
    if (!CreatePipe(&read, &write, &attributes, 0)) return false;
    SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdOutput = write;
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    auto line = command_line(exe, args);
    std::vector<wchar_t> command(line.begin(), line.end());
    command.push_back(L'\0');
    PROCESS_INFORMATION child{};
    bool started = CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, TRUE,
                                  CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, nullptr,
                                  nullptr, &startup, &child);
    CloseHandle(write);
    if (!started) {
        CloseHandle(read);
        return false;
    }
    std::string text;
    char buffer[4096];
    DWORD got = 0;
    while (ReadFile(read, buffer, sizeof(buffer), &got, nullptr) && got > 0) {
        text.append(buffer, got);
    }
    WaitForSingleObject(child.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(child.hProcess, &code);
    CloseHandle(child.hThread);
    CloseHandle(child.hProcess);
    CloseHandle(read);
    if (output) *output = std::move(text);
    if (status) *status = static_cast<int>(code);
    return true;
#else
    FILE* pipe = popen(narrow(command_line(exe, args)).c_str(), "r");
    if (!pipe) return false;
    std::string text;
    char buffer[4096];
    size_t got = 0;
    while ((got = std::fread(buffer, 1, sizeof(buffer), pipe)) > 0) text.append(buffer, got);
    // pclose gives the wait status, not the exit code.
    int code = pclose(pipe);
    if (output) *output = std::move(text);
    if (status) *status = code >= 0 && WIFEXITED(code) ? WEXITSTATUS(code) : -1;
    return true;
#endif
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

// -- SHA-1 -----------------------------------------------------------------

namespace {

struct Sha1 {
    uint32_t state[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    uint64_t length = 0;
    unsigned char block[64] = {};
    size_t used = 0;

    static uint32_t rol(uint32_t value, int bits) {
        return (value << bits) | (value >> (32 - bits));
    }

    void compress(const unsigned char* data) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(data[i * 4]) << 24) |
                   (static_cast<uint32_t>(data[i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(data[i * 4 + 2]) << 8) |
                   static_cast<uint32_t>(data[i * 4 + 3]);
        }
        for (int i = 16; i < 80; ++i)
            w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

        uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];
        for (int i = 0; i < 80; ++i) {
            uint32_t f = 0, k = 0;
            if (i < 20) {
                f = (b & c) | (~b & d);
                k = 0x5A827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }
            uint32_t temp = rol(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rol(b, 30);
            b = a;
            a = temp;
        }
        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
    }

    void update(const unsigned char* data, size_t size) {
        length += size;
        while (size > 0) {
            size_t take = std::min(size, sizeof(block) - used);
            std::memcpy(block + used, data, take);
            used += take;
            data += take;
            size -= take;
            if (used == sizeof(block)) {
                compress(block);
                used = 0;
            }
        }
    }

    wstring finish() {
        uint64_t bits = length * 8;
        unsigned char pad = 0x80;
        update(&pad, 1);
        unsigned char zero = 0;
        while (used != 56) update(&zero, 1);
        unsigned char tail[8];
        for (int i = 0; i < 8; ++i)
            tail[i] = static_cast<unsigned char>((bits >> (56 - i * 8)) & 0xFF);
        update(tail, 8);

        wstring hex;
        hex.reserve(40);
        for (uint32_t word : state) {
            for (int shift = 28; shift >= 0; shift -= 4) {
                unsigned value = (word >> shift) & 0xF;
                hex.push_back(static_cast<wchar_t>(value < 10 ? L'0' + value
                                                              : L'a' + (value - 10)));
            }
        }
        return hex;
    }
};

}  // namespace

wstring sha1_hex(const void* data, size_t size) {
    Sha1 hash;
    hash.update(static_cast<const unsigned char*>(data), size);
    return hash.finish();
}

wstring sha1_file(const fs::path& path, wstring* error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        if (error) *error = L"cannot read " + path.wstring();
        return {};
    }
    Sha1 hash;
    std::vector<char> buffer(1 << 16);
    while (file) {
        file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        std::streamsize got = file.gcount();
        if (got > 0) hash.update(reinterpret_cast<const unsigned char*>(buffer.data()),
                                 static_cast<size_t>(got));
    }
    return hash.finish();
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

wstring last_error() {
#if defined(_WIN32)
    DWORD code = GetLastError();
    wchar_t* buffer = nullptr;
    DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                      FORMAT_MESSAGE_IGNORE_INSERTS,
                                  nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                  reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    wstring text = length && buffer ? wstring(buffer, length) : format(L"error %lu", code);
    if (buffer) LocalFree(buffer);
    text = trim(text);
    while (!text.empty() &&
           (text.back() == L'.' || text.back() == L'\r' || text.back() == L'\n'))
        text.pop_back();
    return text;
#else
    return widen(std::strerror(errno));
#endif
}

namespace {

#if !defined(_WIN32)
bool run_detached(const std::vector<std::string>& argv, wstring* error) {
    // Everything the child needs is built before the fork.
    std::vector<char*> raw;
    raw.reserve(argv.size() + 1);
    for (const auto& arg : argv) raw.push_back(const_cast<char*>(arg.c_str()));
    raw.push_back(nullptr);

    pid_t pid = fork();
    if (pid < 0) {
        if (error) *error = last_error();
        return false;
    }
    if (pid == 0) {
        // Double fork: the launcher reaps the intermediate child, and the program we
        // started is re-parented, so no zombie is left behind.
        pid_t grandchild = fork();
        if (grandchild == 0) {
            execvp(raw[0], raw.data());
            _exit(127);
        }
        _exit(grandchild < 0 ? 1 : 0);
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    return true;
}
#endif

}  // namespace

bool open_path(const fs::path& path, wstring* error) {
#if defined(_WIN32)
    auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", path.c_str(),
                                                          nullptr, nullptr, SW_SHOWNORMAL));
    if (result <= 32) {
        if (error) *error = L"Windows could not open:\n\n" + path.wstring();
        return false;
    }
    return true;
#elif defined(__APPLE__)
    return run_detached({"open", path.string()}, error);
#else
    return run_detached({"xdg-open", path.string()}, error);
#endif
}

bool open_url(const std::wstring& url, wstring* error) {
#if defined(_WIN32)
    auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", url.c_str(),
                                                          nullptr, nullptr, SW_SHOWNORMAL));
    if (result <= 32) {
        if (error) *error = L"Windows could not open:\n\n" + url;
        return false;
    }
    return true;
#elif defined(__APPLE__)
    return run_detached({"open", narrow(url)}, error);
#else
    return run_detached({"xdg-open", narrow(url)}, error);
#endif
}

bool start_detached(const std::wstring& exe, const std::vector<std::wstring>& args,
                    wstring* error) {
#if defined(_WIN32)
    auto line = command_line(exe, args);
    std::vector<wchar_t> command(line.begin(), line.end());
    command.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};
    // CREATE_NO_WINDOW: a console of its own but no window on screen, so the helper is
    // still there after this process exits and a console program it starts has somewhere
    // to run. Not DETACHED_PROCESS: given no console at all, powershell.exe exits before
    // it runs the script, so an update closes the launcher and nothing else happens.
    if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, nullptr, nullptr,
                        &startup, &info)) {
        if (error) *error = format(L"cannot start %ls: %ls", exe.c_str(), last_error().c_str());
        return false;
    }
    CloseHandle(info.hThread);
    CloseHandle(info.hProcess);
    return true;
#else
    std::vector<std::string> argv;
    argv.reserve(args.size() + 1);
    argv.push_back(narrow(exe));
    for (const auto& arg : args) argv.push_back(narrow(arg));
    return run_detached(argv, error);
#endif
}

}  // namespace nla
