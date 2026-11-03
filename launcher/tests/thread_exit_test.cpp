// Giving up a host thread, which is how a guest thread that has exited releases the host
// thread that was running it.
//
// The portable implementation used pthread_exit(), which unwinds the thread so its cleanup
// handlers can run. glibc forces that unwind through libgcc_s, while the personality routine
// answering it comes from whatever the binary links - and in a Zig build, which is what the
// Linux tool archive is written with, that is its own statically linked libc++abi. The two
// disagree about the unwinder's context, and a running game died of it four seconds in. The
// thread is now escaped with a longjmp back to the entry instead: no unwinding at all, which
// is what ExitThread does on Windows and what this was always meant to be.
//
// Run by ctest. A host compiler cannot reproduce the mismatch - both halves come from the same
// unwinder there - so what this checks is the path itself: a thread that calls
// plat_thread_exit() stops where it called it, the process carries on, and a thread that
// simply returns still works afterwards.

#include "platform.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    std::printf("%s %s\n", condition ? "ok  " : "FAIL", what);
    if (!condition) ++failures;
}

// Waits for `flag` to be set, and gives up after about a second: a thread that never starts, or
// one that hangs, has to fail the run rather than wedge it. The sleep is 10ms because Windows'
// timers resolve no finer - a 1ms sleep there costs 15ms, and a thousand of them a test run.
bool wait_for(const std::atomic<int>& flag, int rounds = 100) {
    for (int i = 0; i < rounds; ++i) {
        if (flag.load()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return flag.load();
}

// Long enough for a thread that wrongly carried on past its exit to have got there.
void settle() { std::this_thread::sleep_for(std::chrono::milliseconds(200)); }

std::atomic<int> g_exiting_started{0};
std::atomic<int> g_exiting_past_exit{0};

void* exiting(void*) {
    g_exiting_started = 1;
    plat_thread_exit();
    g_exiting_past_exit = 1;  // must not be reached
    return nullptr;
}

std::atomic<int> g_returning_started{0};
std::atomic<int> g_returning_finished{0};

void* returning(void*) {
    g_returning_started = 1;
    g_returning_finished = 1;
    return nullptr;
}

std::atomic<int> g_second_exiting_started{0};
std::atomic<int> g_second_exiting_past_exit{0};

void* second_exiting(void*) {
    g_second_exiting_started = 1;
    plat_thread_exit();
    g_second_exiting_past_exit = 1;
    return nullptr;
}

}  // namespace

namespace {
int run() {
    // A thread that gives itself up stops at the call, and the process keeps running.
    check(plat_thread_start(exiting, nullptr, 1u << 20), "a host thread starts");
    check(wait_for(g_exiting_started), "and reaches its entry");
    settle();
    check(g_exiting_past_exit.load() == 0, "plat_thread_exit does not return");

    // The exit left nothing behind that upsets a thread that finishes the ordinary way.
    check(plat_thread_start(returning, nullptr, 1u << 20), "a second thread starts after that");
    check(wait_for(g_returning_started) && wait_for(g_returning_finished),
          "and runs to the end of its function");

    // And another thread can give itself up once that one has gone: the escape point is the
    // thread's own, so one thread's exit does not consume the next one's.
    check(plat_thread_start(second_exiting, nullptr, 1u << 20), "a third thread starts");
    check(wait_for(g_second_exiting_started), "and reaches its entry");
    settle();
    check(g_second_exiting_past_exit.load() == 0,
          "and its plat_thread_exit does not return either");

    std::printf("%s\n", failures ? "some checks failed" : "all checks passed");
    return failures ? 1 : 0;
}

}  // namespace

// The launcher's test executables are built -municode on Windows, so the entry point is wmain.
#if defined(_WIN32)
int wmain() { return run(); }
#else
int main() { return run(); }
#endif
