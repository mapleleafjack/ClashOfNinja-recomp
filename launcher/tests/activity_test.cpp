// Checks what the Activity panel keeps: the lines a job reports, how they are split, and how
// many of them survive. Every rule here used to live in App::append_log, where nothing could
// reach it without a window. Run by ctest; needs no game.

#include "activity_panel.h"

#include <cstdio>
#include <string>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    std::printf("%s %s\n", condition ? "ok  " : "FAIL", what);
    if (!condition) ++failures;
}

// One report of text, split into the lines the panel would show.
void splitting() {
    {
        nla::Activity activity;
        activity.write(L"one\ntwo\n");
        check(activity.lines().size() == 2, "a report of two lines is two lines");
        check(activity.lines()[0] == L"one" && activity.lines()[1] == L"two",
              "the lines are the ones reported, in order");
    }
    {
        nla::Activity activity;
        activity.write(L"a\r\nb\r\n");
        check(activity.lines().size() == 2 && activity.lines()[1] == L"b",
              "carriage returns are dropped, so a CRLF child reads like a line-at-a-time one");
    }
    {
        nla::Activity activity;
        activity.write(L"no newline");
        check(activity.lines().size() == 1 && activity.lines()[0] == L"no newline",
              "a report that ends without a newline still shows its line");
    }
    {
        nla::Activity activity;
        activity.write(L"x\n\n");
        check(activity.lines().size() == 2 && activity.lines()[1].empty(),
              "an empty line is a line");
    }
    {
        nla::Activity activity;
        activity.write(L"one\n");
        activity.write(L"two\n");
        check(activity.lines().size() == 2, "a line at a time adds up the same as one report");
    }
    {
        // Job reports whole lines, and the one place it reports without a newline is a
        // child that ran past the buffer cap. Either way a report's tail is a line of its
        // own: nothing is held back waiting for more.
        nla::Activity activity;
        activity.write(L"one");
        activity.write(L"two\n");
        check(activity.lines().size() == 2 && activity.lines()[0] == L"one",
              "a report that ends mid-line does not wait for the next one");
    }
}

// A build that prints without stopping must not grow the launcher's memory with it.
void keeping() {
    nla::Activity activity;
    const int total = static_cast<int>(nla::Activity::kLines) + 500;
    for (int i = 0; i < total; ++i) activity.write(std::to_wstring(i) + L"\n");
    check(activity.lines().size() == nla::Activity::kLines, "the kept lines are capped");
    check(activity.lines().back() == std::to_wstring(total - 1),
          "the newest line is the one still there");
    check(activity.lines().front() == std::to_wstring(total - static_cast<int>(nla::Activity::kLines)),
          "the oldest lines are the ones dropped");
}

void clearing() {
    nla::Activity activity;
    activity.write(L"one\ntwo\n");
    activity.clear();
    check(activity.empty() && activity.lines().empty(), "clearing forgets every line");
}

void copying() {
    nla::Activity activity;
    activity.write(L"one\ntwo\n");
    check(activity.text() == L"one\r\ntwo\r\n", "the clipboard text is CRLF, one line each");
    nla::Activity empty;
    check(empty.text().empty(), "nothing reported copies as nothing");
}

// The two labels above the lines. A job that has not said how far it is gets no bar, which is
// what -1 means; zero says the same, because a job that has not started has no progress.
void progress() {
    nla::Activity activity;
    check(activity.percent() == -1, "a job that has said nothing has no progress");
    activity.set_percent(0);
    check(activity.percent() == -1, "zero is no progress, not an empty bar");
    activity.set_percent(7);
    check(activity.percent() == 7, "a real percent is kept");
    activity.set_percent(-4);
    check(activity.percent() == -1, "a negative percent goes back to none");

    activity.set_stage(L"building");
    check(activity.stage() == L"building", "the stage is the one the job named");
}

// Starting a job is the moment the last job's numbers stop meaning anything.
void starting() {
    nla::Activity activity;
    activity.write(L"from the last job\n");
    activity.set_stage(L"building");
    activity.set_percent(40);
    activity.reset();
    check(activity.percent() == -1 && activity.stage().empty(),
          "a new job starts with no progress and no stage");
    check(activity.lines().size() == 1,
          "a new job keeps the log: it is what the last steps printed");
}

// The panel's own state: whether it is open, how far back it is scrolled, and that a new
// job opens it again. These were four fields on App, asked about from the event loop and
// from the drawing; nothing could check them without a window.
void panel() {
    {
        nla::ActivityPanel panel;
        check(!panel.visible(), "a new panel is folded away");
        panel.begin();
        check(panel.visible(), "a job opens the panel");
    }
    {
        nla::ActivityPanel panel;
        panel.write(L"one\ntwo\nthree\n");
        panel.set_stage(L"Building");
        panel.set_percent(40);
        panel.begin();
        check(panel.visible() && panel.lines().size() == 3,
              "a new job keeps the lines the last steps printed");
        check(panel.percent() == -1 && panel.stage().empty(),
              "a new job starts with no progress and no stage of its own");
    }
    {
        nla::ActivityPanel panel;
        panel.begin();
        panel.write(L"a\nb\nc\nd\ne\nf\ng\nh\n");
        panel.scroll(1);
        check(panel.offset() == 3, "one notch of the wheel is three lines");
        panel.scroll(-1);
        check(panel.offset() == 0, "the other way comes back");
        panel.scroll(-1);
        check(panel.offset() == 0, "the newest end is the floor");
    }
    {
        // Eight lines with room for three: the offset cannot pass the fifth, or the first
        // line would scroll out of reach and the rest of the log with it.
        nla::ActivityPanel panel;
        panel.begin();
        panel.write(L"a\nb\nc\nd\ne\nf\ng\nh\n");
        panel.scroll(100);
        panel.clamp_offset(3);
        check(panel.offset() == 5, "the offset stops at the first line the frame can show");
        panel.clamp_offset(20);
        check(panel.offset() == 0, "a frame taller than the log shows it from the top");
    }
    {
        nla::ActivityPanel panel;
        panel.begin();
        panel.toggle();
        check(!panel.visible(), "the toggle folds it");
        panel.toggle();
        check(panel.visible(), "and unfolds it");
        panel.fold();
        check(!panel.visible(), "a finished job folds it back");
    }
    {
        nla::ActivityPanel panel;
        panel.begin();
        panel.write(L"a\nb\n");
        panel.scroll(1);
        panel.clear();
        check(panel.empty() && panel.offset() == 0, "clearing the log returns to the newest end");
    }
    {
        nla::ActivityPanel panel;
        check(panel.rect().contains(10, 10) == false, "nothing is over an undrawn panel");
        panel.set_rect({100, 100, 200, 50});
        check(panel.rect().contains(150, 120), "the wheel reaches the drawn lines");
        check(!panel.rect().contains(10, 10), "and not the rest of the page");
    }
}

int run() {
    splitting();
    keeping();
    clearing();
    copying();
    progress();
    starting();
    panel();
    std::printf("%s\n", failures ? "some checks failed" : "all checks passed");
    return failures ? 1 : 0;
}

}  // namespace

// MinGW links with -municode on Windows, which wants wmain.
#if defined(_WIN32)
int wmain() { return run(); }
#else
int main() { return run(); }
#endif
