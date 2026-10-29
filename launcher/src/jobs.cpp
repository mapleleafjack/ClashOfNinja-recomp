#include "jobs.h"

#include "util.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>

#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace nla {
namespace {

#if defined(_WIN32)

struct Child {
    PROCESS_INFORMATION info{};
    HANDLE job = nullptr;
    HANDLE read = nullptr;

    ~Child() { close(); }

    void close() {
        if (info.hThread) CloseHandle(info.hThread);
        if (info.hProcess) CloseHandle(info.hProcess);
        if (read) CloseHandle(read);
        if (job) CloseHandle(job);
        info = {};
        job = nullptr;
        read = nullptr;
    }
};

HANDLE create_kill_on_close_job() {
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (!job) return nullptr;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
    return job;
}

bool spawn(const std::wstring& exe, const std::vector<std::wstring>& args,
           const fs::path& cwd, bool hide_window, Child& child, std::wstring* error) {
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE read = nullptr, write = nullptr;
    if (!CreatePipe(&read, &write, &attributes, 0)) {
        if (error) *error = format(L"cannot create a pipe: %ls", last_error().c_str());
        return false;
    }
    SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    if (hide_window) {
        // Build tools: no console flash. Never do this for the game: SDL reads
        // STARTUPINFO.wShowWindow and creates its window hidden when it is SW_HIDE.
        startup.dwFlags |= STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_HIDE;
    } else {
        startup.wShowWindow = SW_SHOWNORMAL;
    }
    startup.hStdOutput = write;
    startup.hStdError = write;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    auto line = command_line(exe, args);
    std::vector<wchar_t> command(line.begin(), line.end());
    command.push_back(L'\0');

    DWORD flags = CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT;
    BOOL ok = CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, TRUE, flags,
                             nullptr, cwd.empty() ? nullptr : cwd.c_str(), &startup,
                             &child.info);
    CloseHandle(write);
    if (!ok) {
        if (error) *error = format(L"cannot start %ls: %ls", exe.c_str(),
                                   last_error().c_str());
        CloseHandle(read);
        return false;
    }

    child.job = create_kill_on_close_job();
    if (child.job) AssignProcessToJobObject(child.job, child.info.hProcess);
    child.read = read;
    ResumeThread(child.info.hThread);
    return true;
}

#else

struct Child {
    pid_t pid = 0;
    int read_fd = -1;

    ~Child() { close(); }

    void close() {
        if (read_fd >= 0) ::close(read_fd);
        read_fd = -1;
        pid = 0;
    }
};

bool spawn(const std::wstring& exe, const std::vector<std::wstring>& args,
           const fs::path& cwd, bool, Child& child, std::wstring* error) {
    int fds[2];
    if (pipe(fds) != 0) {
        if (error) *error = format(L"cannot create a pipe: %ls", last_error().c_str());
        return false;
    }

    // Everything the child needs is built before fork(): after it, only async-signal-safe
    // calls are allowed.
    std::vector<std::string> storage;
    storage.push_back(narrow(exe));
    for (const auto& arg : args) storage.push_back(narrow(arg));
    std::vector<char*> argv;
    argv.reserve(storage.size() + 1);
    for (auto& item : storage) argv.push_back(const_cast<char*>(item.c_str()));
    argv.push_back(nullptr);
    std::string directory = cwd.empty() ? std::string() : narrow(cwd.wstring());

    pid_t pid = fork();
    if (pid < 0) {
        ::close(fds[0]);
        ::close(fds[1]);
        if (error) *error = format(L"cannot fork: %ls", last_error().c_str());
        return false;
    }
    if (pid == 0) {
        // Its own process group, so cancelling reaches the whole build tree.
        setpgid(0, 0);
        dup2(fds[1], STDOUT_FILENO);
        dup2(fds[1], STDERR_FILENO);
        ::close(fds[0]);
        ::close(fds[1]);
        if (!directory.empty() && chdir(directory.c_str()) != 0) _exit(126);
        execv(argv[0], argv.data());
        _exit(127);
    }

    ::close(fds[1]);
    child.pid = pid;
    child.read_fd = fds[0];
    return true;
}

#endif

#if defined(_WIN32)

void pump_child(HANDLE pipe, const std::function<void(const std::wstring&)>& emit) {
    std::string pending;
    char buffer[8192];
    for (;;) {
        DWORD got = 0;
        if (!ReadFile(pipe, buffer, sizeof(buffer), &got, nullptr) || got == 0) break;
        pending.append(buffer, got);
        for (;;) {
            auto newline = pending.find('\n');
            if (newline == std::string::npos) break;
            std::string line = pending.substr(0, newline);
            pending.erase(0, newline + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            emit(widen(line) + L"\n");
        }
        if (pending.size() > 1 << 16) {
            emit(widen(pending));
            pending.clear();
        }
    }
    if (!pending.empty()) emit(widen(pending) + L"\n");
}

#else

void pump_child(int fd, const std::function<void(const std::wstring&)>& emit) {
    std::string pending;
    char buffer[8192];
    for (;;) {
        ssize_t got = ::read(fd, buffer, sizeof(buffer));
        if (got <= 0) break;
        pending.append(buffer, static_cast<size_t>(got));
        for (;;) {
            auto newline = pending.find('\n');
            if (newline == std::string::npos) break;
            std::string line = pending.substr(0, newline);
            pending.erase(0, newline + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            emit(widen(line) + L"\n");
        }
        if (pending.size() > 1 << 16) {
            emit(widen(pending));
            pending.clear();
        }
    }
    if (!pending.empty()) emit(widen(pending) + L"\n");
}

#endif

}  // namespace

Job::~Job() { shutdown(); }

void Job::start(std::vector<Task> tasks, TextFn log, TextFn stage, ProgressFn progress,
                DoneFn done) {
    if (running_.load()) return;
    if (thread_.joinable()) thread_.join();
    log_ = std::move(log);
    stage_ = std::move(stage);
    progress_ = std::move(progress);
    done_ = std::move(done);
    cancel_.store(false);
    running_.store(true);
    thread_ = std::thread([this, tasks = std::move(tasks)]() mutable { worker(std::move(tasks)); });
}

void Job::worker(std::vector<Task> tasks) {
    int status = 0;
    for (auto& task : tasks) {
        if (cancel_.load()) {
            status = 130;
            break;
        }
        if (!task.label.empty()) stage(task.label);
        status = task.run(*this);
        if (status != 0) break;
        if (cancel_.load()) {
            status = 130;
            break;
        }
    }
    running_.store(false);
    if (progress_) progress_(0);
    if (stage_) stage_(L"");
    if (done_) done_(cancel_.load() ? 130 : status);
}

void Job::cancel() {
    cancel_.store(true);
    std::lock_guard<std::mutex> lock(process_mutex_);
#if defined(_WIN32)
    if (active_group_) TerminateJobObject(reinterpret_cast<HANDLE>(active_group_), 1);
#else
    if (active_process_) {
        if (active_group_) ::kill(-static_cast<pid_t>(active_group_), SIGKILL);
        ::kill(static_cast<pid_t>(active_process_), SIGKILL);
    }
#endif
}

void Job::shutdown() {
    cancel();
    if (thread_.joinable()) thread_.join();
}

int Job::run_process(const std::wstring& exe, const std::vector<std::wstring>& args,
                     const fs::path& cwd) {
    if (cancel_.load()) return 130;
    Child child;
    std::wstring error;
    if (!spawn(exe, args, cwd, true, child, &error)) {
        log(error + L"\n");
        return 127;
    }
#if defined(_WIN32)
    {
        std::lock_guard<std::mutex> lock(process_mutex_);
        active_group_ = reinterpret_cast<std::intptr_t>(child.job);
        active_process_ = reinterpret_cast<std::intptr_t>(child.info.hProcess);
    }
    pump_child(child.read, [this](const std::wstring& line) { log(line); });
    WaitForSingleObject(child.info.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(child.info.hProcess, &code);
    {
        std::lock_guard<std::mutex> lock(process_mutex_);
        active_group_ = 0;
        active_process_ = 0;
    }
    if (cancel_.load()) return 130;
    return static_cast<int>(code);
#else
    {
        std::lock_guard<std::mutex> lock(process_mutex_);
        active_process_ = static_cast<std::intptr_t>(child.pid);
        active_group_ = static_cast<std::intptr_t>(child.pid);
    }
    pump_child(child.read_fd, [this](const std::wstring& line) { log(line); });
    int status = 0;
    while (waitpid(child.pid, &status, 0) < 0 && errno == EINTR) {
    }
    {
        std::lock_guard<std::mutex> lock(process_mutex_);
        active_process_ = 0;
        active_group_ = 0;
    }
    if (cancel_.load()) return 130;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 1;
#endif
}

// -- game ------------------------------------------------------------------

bool GameProcess::stops_through_sigint() {
#if defined(_WIN32)
    return false;
#else
    return true;
#endif
}

GameProcess::~GameProcess() { shutdown(); }

bool GameProcess::start(const std::wstring& exe, const std::vector<std::wstring>& args,
                        const fs::path& cwd, TextFn output, DoneFn done) {
    if (running_.load()) return false;
    cleanup();
    Child child;
    std::wstring error;
    if (!spawn(exe, args, cwd, false, child, &error)) {
        if (output) output(error + L"\n");
        return false;
    }
#if defined(_WIN32)
    process_ = reinterpret_cast<std::intptr_t>(child.info.hProcess);
    group_ = reinterpret_cast<std::intptr_t>(child.job);
    pipe_ = reinterpret_cast<std::intptr_t>(child.read);
    child.info.hProcess = nullptr;
    child.info.hThread = nullptr;
    child.job = nullptr;
    child.read = nullptr;
#else
    process_ = static_cast<std::intptr_t>(child.pid);
    group_ = static_cast<std::intptr_t>(child.pid);
    pipe_ = static_cast<std::intptr_t>(child.read_fd);
    child.pid = 0;
    child.read_fd = -1;
#endif
    output_ = std::move(output);
    done_ = std::move(done);
    stopped_.store(false);
    running_.store(true);
    thread_ = std::thread([this] { reader(); });
    return true;
}

void GameProcess::reader() {
#if defined(_WIN32)
    pump_child(reinterpret_cast<HANDLE>(pipe_), [this](const std::wstring& line) {
        if (output_) output_(line);
    });
    WaitForSingleObject(reinterpret_cast<HANDLE>(process_), INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(reinterpret_cast<HANDLE>(process_), &code);
    bool was_running = running_.exchange(false);
    if (was_running && done_) done_(static_cast<int>(code), stopped_.load());
#else
    pump_child(static_cast<int>(pipe_), [this](const std::wstring& line) {
        if (output_) output_(line);
    });
    int status = 0;
    while (waitpid(static_cast<pid_t>(process_), &status, 0) < 0 && errno == EINTR) {
    }
    int code = 1;
    if (WIFEXITED(status)) code = WEXITSTATUS(status);
    else if (WIFSIGNALED(status)) code = 128 + WTERMSIG(status);
    bool was_running = running_.exchange(false);
    if (was_running && done_) done_(code, stopped_.load());
#endif
}

void GameProcess::cleanup() {
    if (thread_.joinable()) thread_.join();
#if defined(_WIN32)
    if (pipe_) CloseHandle(reinterpret_cast<HANDLE>(pipe_));
    if (process_) CloseHandle(reinterpret_cast<HANDLE>(process_));
    if (group_) CloseHandle(reinterpret_cast<HANDLE>(group_));
#else
    if (pipe_ > 0) ::close(static_cast<int>(pipe_));
#endif
    pipe_ = 0;
    process_ = 0;
    group_ = 0;
}

void GameProcess::stop() {
    if (!running_.load()) return;
    stopped_.store(true);
#if defined(_WIN32)
    if (group_) TerminateJobObject(reinterpret_cast<HANDLE>(group_), 1);
#else
    // SIGINT first: the runtime handles it by dumping its GX FIFO. Escalate if ignored.
    if (process_) ::kill(static_cast<pid_t>(process_), SIGINT);
    pid_t group = static_cast<pid_t>(group_);
    std::thread([group] {
        for (int attempt = 0; attempt < 12; ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (::kill(-group, 0) != 0) return;
        }
        ::kill(-group, SIGKILL);
    }).detach();
#endif
}

void GameProcess::shutdown() {
    if (!running_.load()) {
        cleanup();
        return;
    }
    stopped_.store(true);
#if defined(_WIN32)
    if (group_) TerminateJobObject(reinterpret_cast<HANDLE>(group_), 1);
#else
    if (process_) {
        ::kill(static_cast<pid_t>(process_), SIGINT);
        for (int attempt = 0; attempt < 12; ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (::kill(static_cast<pid_t>(process_), 0) != 0) break;
            if (attempt == 11) ::kill(-static_cast<pid_t>(group_), SIGKILL);
        }
    }
#endif
    cleanup();
    running_.store(false);
}

int run_tasks_to_log(std::vector<Task> tasks, const fs::path& log) {
    std::string text;
    std::atomic<int> code{-1};
    Job job;
    job.start(
        std::move(tasks), [&text](const std::wstring& line) { text += narrow(line); },
        [&text](const std::wstring& line) { text += narrow(line) + "\n"; },
        [&text](int percent) { text += narrow(format(L"[launcher] %d%%\n", percent)); },
        [&code](int status) { code = status; });
    while (code.load() < 0) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    write_text_file(log, text);
    return code.load();
}

}  // namespace nla
