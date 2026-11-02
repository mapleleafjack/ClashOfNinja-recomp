// Running things: a command line built so the platform's own quoting rules hold, a short
// program whose output is read back, and a program started so that it outlives the
// launcher. The platform differences are real here - CreateProcess against fork/exec - but
// everything above them is shared.

#include "proc.h"

#include "text.h"

#include <cerrno>
#include <cstdio>
#include <cstring>

#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#elif defined(__APPLE__) || defined(__linux__)
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace nla {

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
