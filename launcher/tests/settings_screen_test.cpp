// What the Settings page does: which ids it answers for, the scale list that drops out of
// the combo, and the two settings it writes. The frame and the buttons that belong to
// another page stay the window's, and the checks say so. Run by ctest; no window, no disk
// outside the test's own HOME.

#include "settings_screen.h"

#include "project_state.h"
#include "update_screen.h"
#include "util.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace fs = nla::fs;

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    std::printf("%s %s\n", condition ? "ok  " : "FAIL", what);
    if (!condition) ++failures;
}

// The settings file goes under the home directory, so the test moves HOME before it writes
// one: what the page saved can then be read back without touching the player's own file.
void use_temp_home(const fs::path& home) {
    std::error_code ec;
    fs::create_directories(home, ec);
#if defined(_WIN32)
    _wputenv_s(L"USERPROFILE", home.wstring().c_str());
#else
    setenv("HOME", home.c_str(), 1);
#endif
}

std::wstring saved_settings() {
    std::error_code ec;
    const fs::path path = nla::user_data_dir() / L"settings.ini";
    if (!fs::exists(path, ec)) return {};
    return nla::read_text_file(path);
}

struct RecordingHost {
    nla::ProjectState state;
    int redraws = 0;
    nla::ScreenHost host;

    RecordingHost() {
        host.invalidate = [this] { ++redraws; };
        host.post = [](std::function<void()>) {};
        host.log = [](const std::wstring&) {};
        host.message = [](const std::wstring&, const std::wstring&) {};
        host.update_prompt = [](const std::wstring&, const std::wstring&, const std::wstring&,
                                std::function<void()>) {};
        host.busy = [] { return false; };
        host.game_running = [] { return false; };
        host.rendering = [] { return false; };
        host.refresh = [] {};
        host.settings_note = [](const std::wstring&) {};
        host.launch_cancelled = [] {};
        host.start_job = [](std::vector<nla::Task>, nla::JobOptions) {};
        host.elide_path = [](const std::wstring& text) { return text; };
        host.clipboard = [] { return std::wstring(); };
        host.text_input = [](bool) {};
        host.take_over = [] {};
        host.show_panel = [](nla::OverlayPage, const std::wstring&) {};
        host.set_title = [](const std::wstring&) {};
        host.set_play_status = [](const std::wstring&) {};
        host.launch_if_wanted = [] {};
        host.native_chooser = [](nla::PickerKind, const std::wstring&, const fs::path&) {
            return nla::NativeAnswer{};
        };
    }
};

struct Page {
    RecordingHost host;
    nla::SettingsScreen settings{host.host, host.state};
};

// The panel draws six of the ids below, but only two of them are the settings' own: the
// rest leave the page, start an update, or open the players panel.
void the_ids_it_owns() {
    Page p;
    check(p.settings.handles(nla::Id::SettingsToggleUpdates), "the check-at-start box");
    check(p.settings.handles(nla::Id::Scale), "the scale combo");
    check(p.settings.handles(nla::Id::Scale1), "the first scale it offers");
    check(p.settings.handles(nla::Id::Scale4), "and the last");
    check(!p.settings.handles(nla::Id::SettingsClose), "not the panel's own Done");
    check(!p.settings.handles(nla::Id::SettingsCheckNow), "nor Check now, which updates reads");
    check(!p.settings.handles(nla::Id::SettingsInstallUpdate), "nor Install update");
    check(!p.settings.handles(nla::Id::SettingsPlayers), "nor Players…, which opens a panel");
    check(!p.settings.handles(nla::Id::UpdateBanner), "nor the badge on the main page");
}

// The scale list is the page's: it hangs from a combo that is only ever in this panel.
void the_scale_list() {
    Page p;
    check(!p.settings.menu_open(), "no list is open to begin with");
    p.settings.activate(nla::Id::Scale);
    check(p.settings.menu_open(), "clicking the combo opens it");
    p.settings.activate(nla::Id::Scale);
    check(!p.settings.menu_open(), "and clicking it again closes it");
}

void choosing_a_scale() {
    Page p;
    use_temp_home(nla::temp_dir() / L"nla-settings-screen-test");
    p.settings.activate(nla::Id::Scale);
    p.settings.activate(nla::Id::Scale3);
    check(p.host.state.settings().scale == 3, "the third square sets a scale of 3");
    check(!p.settings.menu_open(), "a choice closes the list");
    const std::wstring text = saved_settings();
    check(text.find(L"scale=3") != std::wstring::npos, "and the scale was written down");
}

// Every factor the list offers maps to itself, so the four rows cannot drift apart from the
// ids they are drawn with.
void every_row_its_own_factor() {
    Page p;
    use_temp_home(nla::temp_dir() / L"nla-settings-screen-test-rows");
    for (int index = 0; index < 4; ++index) {
        p.settings.activate(static_cast<nla::Id>(static_cast<int>(nla::Id::Scale1) + index));
        check(p.host.state.settings().scale == index + 1, "the row sets its own factor");
    }
}

void toggling_the_check_at_start() {
    Page p;
    use_temp_home(nla::temp_dir() / L"nla-settings-screen-test-toggle");
    const bool before = p.host.state.settings().check_updates;
    p.settings.activate(nla::Id::SettingsToggleUpdates);
    check(p.host.state.settings().check_updates != before, "the box flips the setting");
    check(saved_settings().find(L"checkUpdates=") != std::wstring::npos,
          "and it was written down");
    p.settings.activate(nla::Id::SettingsToggleUpdates);
    check(p.host.state.settings().check_updates == before, "a second click flips it back");
}

// Each click redraws once: the page does not leave the window to guess.
void each_click_redraws() {
    Page p;
    use_temp_home(nla::temp_dir() / L"nla-settings-screen-test-redraw");
    p.settings.activate(nla::Id::Scale);
    check(p.host.redraws == 1, "opening the list asks for one redraw");
    p.settings.activate(nla::Id::Scale2);
    check(p.host.redraws == 2, "and choosing from it asks for another");
}

// The list closes whenever something else takes the page: leaving it, or a prompt over it.
void closing_the_list() {
    Page p;
    p.settings.activate(nla::Id::Scale);
    check(p.settings.menu_open(), "the list is open");
    p.settings.close_menu();
    check(!p.settings.menu_open(), "and it closes with the page");
}

int run() {
    the_ids_it_owns();
    the_scale_list();
    choosing_a_scale();
    every_row_its_own_factor();
    toggling_the_check_at_start();
    each_click_redraws();
    closing_the_list();
    std::printf("%s\n", failures ? "some checks failed" : "all checks passed");
    return failures ? 1 : 0;
}

}  // namespace

#if defined(_WIN32)
int wmain() { return run(); }
#else
int main() { return run(); }
#endif
