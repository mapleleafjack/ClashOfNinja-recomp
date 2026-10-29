#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace nla {

namespace fs = std::filesystem;

class Job;

struct Task {
    std::wstring label;
    std::function<int(Job&)> run;
};

// Runs a queue of tasks with the worker's output collected in a string, then writes that
// to `log`. For a console run with no Activity panel to show the steps in.
int run_tasks_to_log(std::vector<Task> tasks, const fs::path& log);

// Runs a short queue of tasks on one worker thread. A child process is started in its own
// process group (POSIX) or job object (Windows), so Cancel kills the whole tree, including
// compilers the build spawns.
class Job {
  public:
    using TextFn = std::function<void(const std::wstring&)>;
    using ProgressFn = std::function<void(int)>;
    using DoneFn = std::function<void(int)>;

    Job() = default;
    ~Job();

    void start(std::vector<Task> tasks, TextFn log, TextFn stage, ProgressFn progress,
               DoneFn done);
    void cancel();
    void shutdown();

    bool running() const { return running_.load(); }
    bool cancelled() const { return cancel_.load(); }

    // Task-side helpers.
    int run_process(const std::wstring& exe, const std::vector<std::wstring>& args,
                    const fs::path& cwd);
    void log(const std::wstring& text) {
        if (log_) log_(text);
    }
    void stage(const std::wstring& text) {
        if (stage_) stage_(text);
    }
    void progress(int percent) {
        if (progress_) progress_(percent);
    }
    const std::atomic_bool& cancel_flag() const { return cancel_; }

  private:
    void worker(std::vector<Task> tasks);

    std::thread thread_;
    std::atomic_bool running_{false};
    std::atomic_bool cancel_{false};
    std::mutex process_mutex_;
    // A PID on POSIX, a process HANDLE on Windows.
    std::intptr_t active_process_ = 0;
    // A process group on POSIX, a job object HANDLE on Windows.
    std::intptr_t active_group_ = 0;

    TextFn log_;
    TextFn stage_;
    ProgressFn progress_;
    DoneFn done_;
};

// The recompiled game, running in its own window. Output is streamed to the launcher.
class GameProcess {
  public:
    using TextFn = std::function<void(const std::wstring&)>;
    using DoneFn = std::function<void(int, bool stopped)>;

    GameProcess() = default;
    ~GameProcess();

    bool start(const std::wstring& exe, const std::vector<std::wstring>& args,
               const fs::path& cwd, TextFn output, DoneFn done);
    void stop();
    void shutdown();
    bool running() const { return running_.load(); }

    // True on the platforms where the runtime handles SIGINT by dumping its GX FIFO.
    static bool stops_through_sigint();

  private:
    void reader();
    void cleanup();

    std::intptr_t process_ = 0;
    std::intptr_t group_ = 0;
    std::intptr_t pipe_ = 0;
    std::thread thread_;
    std::atomic_bool running_{false};
    std::atomic_bool stopped_{false};
    TextFn output_;
    DoneFn done_;
};

}  // namespace nla
