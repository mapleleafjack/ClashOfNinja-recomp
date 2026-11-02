// What the Players panel does: which ids it answers for, the stand-in device list a render
// uses, which pad a pick lands on, and when a row's list opens. All of it lived in App and
// in three methods beside it, reachable only through a window and a game to ask. Run by
// ctest; no window, no game, no controllers.

#include "players_screen.h"

#include "project_state.h"
#include "util.h"

#include <cstdio>
#include <string>

namespace fs = nla::fs;

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    std::printf("%s %s\n", condition ? "ok  " : "FAIL", what);
    if (!condition) ++failures;
}

struct RecordingHost {
    nla::ProjectState state;
    bool rendering = false;
    int redraws = 0;
    int panels = 0;
    nla::OverlayPage panel = nla::OverlayPage::None;
    std::wstring panel_title;
    std::vector<std::wstring> logged;
    nla::ScreenHost host;

    RecordingHost() {
        host.invalidate = [this] { ++redraws; };
        host.post = [](std::function<void()>) {};
        host.log = [this](const std::wstring& text) { logged.push_back(text); };
        host.message = [](const std::wstring&, const std::wstring&) {};
        host.update_prompt = [](const std::wstring&, const std::wstring&, const std::wstring&,
                                std::function<void()>) {};
        host.busy = [] { return false; };
        host.game_running = [] { return false; };
        host.rendering = [this] { return rendering; };
        host.refresh = [] {};
        host.settings_note = [](const std::wstring&) {};
        host.launch_cancelled = [] {};
        host.start_job = [](std::vector<nla::Task>, nla::JobOptions) {};
        host.elide_path = [](const std::wstring& text) { return text; };
        host.clipboard = [] { return std::wstring(); };
        host.text_input = [](bool) {};
        host.take_over = [] {};
        host.show_panel = [this](nla::OverlayPage page, const std::wstring& title) {
            ++panels;
            panel = page;
            panel_title = title;
        };
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
    nla::PlayersScreen players{host.host, host.state};
};

fs::path make_project() {
    const fs::path root = nla::temp_dir() / L"nla-players-screen-test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / L"naruto", ec);
    nla::write_text_file(root / L"naruto" / L"CMakeLists.txt",
                         "gcn_add_game(\n  NAME naruto\n  TITLE \"Naruto\"\n"
                         "  DISC_ID ZZZZZZ\n)\n");
    return root;
}

void the_ids_it_owns() {
    Page p;
    check(p.players.handles(nla::Id::PadRow), "the first player row");
    check(p.players.handles(static_cast<nla::Id>(static_cast<int>(nla::Id::PadRow) + 3)),
          "the last player row");
    check(p.players.handles(nla::Id::PadOption), "the first device in a row's list");
    check(p.players.handles(nla::Id::SettingsRefreshDevices), "the Refresh button");
    check(!p.players.handles(nla::Id::PadRowEnd), "and not the id past the last row");
    check(!p.players.handles(nla::Id::OverlayAction), "nor the panel's own Done button");
    check(!p.players.handles(nla::Id::ModRow), "nor a mod row");
}

// A render lists the same two devices wherever it is checked, so the drawing does not
// depend on what this machine happens to have plugged in.
void the_stand_in_devices() {
    Page p;
    p.host.rendering = true;
    p.players.refresh();
    check(p.players.devices().size() == 2, "a render lists two stand-in controllers");
    check(p.players.devices()[0].name == L"Xbox Wireless Controller",
          "the first is the one the panel is drawn with");
    check(p.players.devices()[1].pad == 2, "and the second is pinned to the second pad");
}

// With no built game there is nothing to ask, and the panel says so rather than looking
// like an empty machine.
void with_nothing_to_ask() {
    {
        Page p;
        p.players.refresh();
        check(p.players.devices().empty(), "with no project the list is empty");
    }
    {
        Page p;
        p.host.state.set_project(make_project(), L"naruto");
        p.players.refresh();
        check(p.players.devices().empty(),
              "with a project but no built game there is still nothing to ask");
    }
}

// Opening the panel shows it, and asks the game again so the rows are what it reads now.
void opening() {
    Page p;
    p.host.rendering = true;
    p.players.open();
    check(p.host.panels == 1 && p.host.panel == nla::OverlayPage::Players,
          "the panel is the Players page");
    check(p.host.panel_title == L"Players", "with the page's own title");
    check(p.players.devices().size() == 2, "and the list was read on the way in");
}

// A row's list opens on that row, and one choice takes the pad.
void choosing_a_device() {
    Page p;
    p.host.rendering = true;
    p.players.open();
    check(!p.players.menu_open(), "no list is open to begin with");

    p.players.activate(nla::Id::PadRow);
    check(p.players.menu_open(), "clicking a row opens its list");

    // Option 1 is the keyboard; option 2 is the first controller the game named.
    p.players.activate(static_cast<nla::Id>(static_cast<int>(nla::Id::PadOption) + 2));
    check(!p.players.menu_open(), "a choice closes the list");
    check(!p.players.menu_open(), "and leaves nothing open behind it");
}

// Anything else pressed while a list is open closes it, then does its own work.
void a_click_elsewhere() {
    Page p;
    p.host.rendering = true;
    p.players.open();
    p.players.activate(nla::Id::PadRow);
    check(p.players.menu_open(), "the list is open");
    p.players.activate(nla::Id::SettingsRefreshDevices);
    check(!p.players.menu_open(), "another control closes it first");
}

// The menu closes whenever something else takes the page.
void closing_the_menu() {
    Page p;
    p.host.rendering = true;
    p.players.open();
    p.players.activate(nla::Id::PadRow);
    check(p.players.menu_open(), "the list is open");
    p.players.close_menu();
    check(!p.players.menu_open(), "and it closes with the page");
}

// A render checks the panel, not the player's disk: its stand-in list must never reach
// controls.txt.
void a_render_writes_nothing() {
    Page p;
    p.host.rendering = true;
    p.host.state.set_project(make_project(), L"naruto");
    p.players.refresh();
    p.players.activate(nla::Id::PadRow);
    p.players.activate(static_cast<nla::Id>(static_cast<int>(nla::Id::PadOption) + 2));
    check(p.host.logged.empty(), "a render says nothing about writing pins it did not write");
    std::error_code ec;
    check(!fs::exists(p.host.state.project()->controls(), ec),
          "and controls.txt was not created");
}

int run() {
    the_ids_it_owns();
    the_stand_in_devices();
    with_nothing_to_ask();
    opening();
    choosing_a_device();
    a_click_elsewhere();
    closing_the_menu();
    a_render_writes_nothing();
    std::printf("%s\n", failures ? "some checks failed" : "all checks passed");
    return failures ? 1 : 0;
}

}  // namespace

#if defined(_WIN32)
int wmain() { return run(); }
#else
int main() { return run(); }
#endif
