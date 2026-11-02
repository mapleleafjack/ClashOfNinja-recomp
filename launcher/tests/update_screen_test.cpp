// What the update screen does with what a check finds: when it offers a release, when it
// keeps quiet, and what the Settings page says about it. All of that used to be inside App,
// reachable only through a window, a network and a thread. Run by ctest; needs none of the
// three.

#include "update_screen.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    std::printf("%s %s\n", condition ? "ok  " : "FAIL", what);
    if (!condition) ++failures;
}

// Everything the screen can ask the window for, with what it asked recorded.
struct RecordingHost {
    nla::Settings settings;
    bool busy = false;
    bool game_running = false;
    int redraws = 0;
    std::vector<std::wstring> logged;
    std::vector<std::pair<std::wstring, std::wstring>> messages;
    std::vector<std::pair<std::wstring, std::wstring>> prompts;
    std::vector<std::wstring> notes;
    std::vector<std::wstring> jobs_started;
    nla::ScreenHost host;
    std::vector<std::function<void()>> posted;

    RecordingHost() {
        host.invalidate = [this] { ++redraws; };
        host.post = [this](std::function<void()> task) { posted.push_back(std::move(task)); };
        host.log = [this](const std::wstring& text) { logged.push_back(text); };
        host.message = [this](const std::wstring& title, const std::wstring& text) {
            messages.emplace_back(title, text);
        };
        host.update_prompt = [this](const std::wstring& title, const std::wstring& text,
                                    const std::wstring& action,
                                    std::function<void()> on_action) {
            prompts.emplace_back(title, text);
            (void)action;
            (void)on_action;
        };
        host.busy = [this] { return busy; };
        host.game_running = [this] { return game_running; };
        host.settings = [this]() -> nla::Settings& { return settings; };
        host.settings_note = [this](const std::wstring& text) { notes.push_back(text); };
        host.start_job = [this](std::vector<nla::Task> tasks, nla::JobOptions) {
            jobs_started.push_back(tasks.empty() ? L"empty" : tasks.front().label);
        };
        host.elide_path = [](const std::wstring& text) { return text; };
    }
};

nla::ReleaseInfo release() {
    nla::ReleaseInfo info;
    info.version = L"1.1.0";
    info.page_url = L"https://example.invalid/releases/tag/v1.1.0";
    info.asset_name = L"NarutoLauncher-macOS-arm64-v1.1.0.zip";
    info.asset_url = L"https://example.invalid/" + info.asset_name;
    return info;
}

nla::UpdateCheck found(nla::ReleaseInfo info) {
    nla::UpdateCheck result;
    result.state = nla::UpdateCheck::State::Available;
    result.release = std::move(info);
    return result;
}

// A start-up check on a build with the setting off must not ask at all: no thread, no
// thread, no log line, no redraw.
void automatic_check_is_off_by_default() {
    RecordingHost h;
    h.settings.check_updates = false;
    nla::UpdateScreen screen(h.host);
    screen.start(false);
    check(screen.state() == nla::UpdateScreen::State::Never,
          "a start-up check with the setting off stays at never");
    check(h.logged.empty(), "and says nothing in the log");
    check(h.redraws == 0, "and does not redraw");
    screen.shutdown();
}

// A build that was not made from a release tag cannot ask GitHub, and says so rather than
// pretending to have looked. The target is built with the development version for this
// reason: it keeps the test off the network.
void a_build_with_no_release_version() {
    {
        RecordingHost h;
        h.settings.check_updates = true;
        nla::UpdateScreen screen(h.host);
        screen.start(false);
        check(screen.state() == nla::UpdateScreen::State::Failed,
              "a start-up check on a development build ends as failed");
        check(h.messages.empty(), "and says nothing, because nobody asked");
        screen.shutdown();
    }
    {
        RecordingHost h;
        h.settings.check_updates = false;
        nla::UpdateScreen screen(h.host);
        screen.start(true);
        check(screen.state() == nla::UpdateScreen::State::Failed,
              "a manual check on a development build fails rather than asking");
        check(!screen.error().empty(), "and names why");
        check(h.messages.size() == 1 && h.messages[0].first == L"Updates",
              "and tells the player, because they asked");
        check(h.logged.empty(), "a check that never ran logs nothing at all");
        screen.shutdown();
    }
}

// Finding a release offers it, unless the player already said "not now" to that version.
void a_found_release_is_offered() {
    {
        RecordingHost h;
        nla::UpdateScreen screen(h.host);
        screen.on_checked(found(release()), false);
        check(screen.state() == nla::UpdateScreen::State::Available, "the check is available");
        check(screen.version() == L"1.1.0", "the version is the one found");
        check(h.prompts.size() == 1, "the prompt is shown on a start-up check");
    }
    {
        RecordingHost h;
        h.settings.skipped_update = L"1.1.0";
        nla::UpdateScreen screen(h.host);
        screen.on_checked(found(release()), false);
        check(h.prompts.empty(), "a version the player said no to waits behind the button");
        check(screen.state() == nla::UpdateScreen::State::Available,
              "and is still there for the button to offer");
    }
    {
        RecordingHost h;
        h.settings.skipped_update = L"1.1.0";
        nla::UpdateScreen screen(h.host);
        screen.on_checked(found(release()), true);
        check(h.prompts.size() == 1, "asking by hand offers it even after a skip");
    }
    {
        RecordingHost h;
        h.busy = true;
        nla::UpdateScreen screen(h.host);
        screen.on_checked(found(release()), true);
        check(h.prompts.empty(), "a step that is running is never talked over");
    }
}

// Being up to date and failing are both silent at start, and both reported when asked.
void the_quiet_outcomes() {
    {
        RecordingHost h;
        nla::UpdateScreen screen(h.host);
        nla::UpdateCheck up_to_date;
        up_to_date.state = nla::UpdateCheck::State::UpToDate;
        screen.on_checked(up_to_date, false);
        check(screen.state() == nla::UpdateScreen::State::UpToDate, "up to date is a state");
        check(h.notes.empty(), "a start-up check says nothing when nothing is new");
        screen.on_checked(up_to_date, true);
        check(h.notes.size() == 1, "a manual check says so under the panel");
    }
    {
        RecordingHost h;
        nla::UpdateScreen screen(h.host);
        nla::UpdateCheck failed;
        failed.state = nla::UpdateCheck::State::Failed;
        failed.error = L"no network";
        screen.on_checked(failed, false);
        check(screen.state() == nla::UpdateScreen::State::Failed, "a failed check is a state");
        check(h.messages.empty(), "a failed start-up check does not interrupt");
        screen.on_checked(failed, true);
        check(h.messages.size() == 1 && h.messages[0].second == L"no network",
              "a failed manual check says why");
    }
}

// The line under UPDATES is the screen's, and its words match the state.
void the_settings_line() {
    RecordingHost h;
    nla::UpdateScreen screen(h.host);
    check(!screen.line().empty(), "there is always a line, even before any check");
    screen.set_stand_in(release());
    check(screen.state() == nla::UpdateScreen::State::Available, "a stand-in is available");
    check(screen.line().find(L"1.1.0") != std::wstring::npos,
          "the line names the version that is available");
    check(screen.line().find(L"is available") != std::wstring::npos, "and that it is there");

    nla::UpdateCheck failed;
    failed.state = nla::UpdateCheck::State::Failed;
    failed.error = L"no network";
    screen.on_checked(failed, false);
    check(screen.line().find(L"no network") != std::wstring::npos,
          "a failed check puts why in the line");
}

// The header button is the one id this page owns, and pressing it offers the release.
void the_header_button() {
    RecordingHost h;
    nla::UpdateScreen screen(h.host);
    check(screen.handles(nla::Id::UpdateBanner), "the screen owns the update button");
    check(!screen.handles(nla::Id::OpenSettings), "and not the Settings button");
    check(!screen.handles(nla::Id::Mods), "nor a game page button");

    screen.activate(nla::Id::UpdateBanner);
    check(h.prompts.empty(), "the button does nothing before a release is known");

    screen.set_stand_in(release());
    screen.activate(nla::Id::UpdateBanner);
    check(h.prompts.size() == 1, "the button offers the release once one is known");
}

// Installing refuses in the cases that must not be talked past. What it does when it does
// go ahead depends on where the running copy sits - a bundle the launcher can write to
// starts the download, one it cannot opens the release page - so that is left to the
// platform rather than asserted from a test binary that is in neither place.
void installing() {
    {
        RecordingHost h;
        nla::UpdateScreen screen(h.host);
        screen.begin();
        check(h.jobs_started.empty() && h.messages.empty(),
              "installing without a release known does nothing at all");
    }
    {
        RecordingHost h;
        h.game_running = true;
        nla::UpdateScreen screen(h.host);
        screen.set_stand_in(release());
        screen.begin();
        check(h.jobs_started.empty(), "the game is never ended by an update under it");
        check(h.messages.size() == 1 && h.messages[0].first == L"Close the game first",
              "and the player is told to stop it");
    }
    {
        RecordingHost h;
        h.busy = true;
        nla::UpdateScreen screen(h.host);
        screen.set_stand_in(release());
        screen.begin();
        check(h.jobs_started.empty(), "a step already running is not replaced");
    }
}

int run() {
    automatic_check_is_off_by_default();
    a_build_with_no_release_version();
    a_found_release_is_offered();
    the_quiet_outcomes();
    the_settings_line();
    the_header_button();
    installing();
    std::printf("%s\n", failures ? "some checks failed" : "all checks passed");
    return failures ? 1 : 0;
}

}  // namespace

#if defined(_WIN32)
int wmain() { return run(); }
#else
int main() { return run(); }
#endif
