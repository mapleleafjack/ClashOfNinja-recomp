// What the Geckos panel does: which ids it answers for, what the box takes while it has
// focus, what switching a code on does, and what Apply and Refresh do when there is nothing
// to write to. All of it lived in App's own switches and in a 300-line file beside them,
// reachable only through a window. Run by ctest; no window, no disc, no network.

#include "geckos_screen.h"

#include "project.h"
#include "project_state.h"
#include "util.h"

#include <cstdio>
#include <string>
#include <vector>

namespace fs = nla::fs;

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    std::printf("%s %s\n", condition ? "ok  " : "FAIL", what);
    if (!condition) ++failures;
}

struct RecordingHost {
    nla::Settings settings;
    nla::ProjectState state;
    bool rendering = false;
    bool busy = false;
    std::wstring clipboard_text;
    int redraws = 0;
    int panel_calls = 0;
    int refresh_calls = 0;
    nla::OverlayPage panel = nla::OverlayPage::None;
    std::wstring panel_title;
    std::vector<std::wstring> logged;
    std::vector<bool> text_input;
    nla::ScreenHost host;

    RecordingHost() {
        host.invalidate = [this] { ++redraws; };
        host.post = [](std::function<void()>) {};
        host.log = [this](const std::wstring& text) { logged.push_back(text); };
        host.message = [](const std::wstring&, const std::wstring&) {};
        host.update_prompt = [](const std::wstring&, const std::wstring&, const std::wstring&,
                                std::function<void()>) {};
        host.busy = [this] { return busy; };
        host.game_running = [] { return false; };
        host.project = [this]() -> const std::optional<nla::Project>& { return state.project(); };
        host.rendering = [this] { return rendering; };
        host.refresh = [this] { ++refresh_calls; };
        host.settings = [this]() -> nla::Settings& { return settings; };
        host.settings_note = [](const std::wstring&) {};
        host.launch_cancelled = [] {};
        host.start_job = [](std::vector<nla::Task>, nla::JobOptions) {};
        host.elide_path = [](const std::wstring& text) { return text; };
        host.clipboard = [this] { return clipboard_text; };
        host.text_input = [this](bool on) { text_input.push_back(on); };
        host.take_over = [] {};
        host.show_panel = [this](nla::OverlayPage page, const std::wstring& title) {
            ++panel_calls;
            panel = page;
            panel_title = title;
        };
        host.native_chooser = [](nla::PickerKind, const std::wstring&, const fs::path&) {
            return nla::NativeAnswer{};
        };
    }
};

// A project folder whose disc has no guide, so refresh() never reaches for the network.
fs::path make_project() {
    const fs::path root = nla::temp_dir() / L"nla-geckos-screen-test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / L"naruto", ec);
    nla::write_text_file(root / L"naruto" / L"CMakeLists.txt",
                         "gcn_add_game(\n  NAME naruto\n  TITLE \"Naruto\"\n"
                         "  DISC_ID ZZZZZZ\n)\n");
    return root;
}

// The panel answers for its own ids and no others, so the window can ask without knowing
// the page.
void the_ids_it_owns() {
    RecordingHost h;
    nla::GeckosScreen screen(h.host);
    check(screen.handles(nla::Id::CodeRow), "the first code row");
    check(screen.handles(static_cast<nla::Id>(static_cast<int>(nla::Id::CodeRow) + 5)),
          "a code row further down");
    check(screen.handles(nla::Id::CodeBox), "the paste box");
    check(screen.handles(nla::Id::CodeApply), "the Apply button");
    check(screen.handles(nla::Id::CodeRefresh), "the Refresh button");
    check(!screen.handles(nla::Id::ModRow), "and not a mod row");
    check(!screen.handles(nla::Id::OverlayAction), "nor the panel's own Done button");
    check(!screen.handles(nla::Id::CodeRowEnd), "nor the id past the last row");
}

// A render draws a stand-in list, including one code that cannot run here.
void the_stand_in_list() {
    RecordingHost h;
    h.rendering = true;
    nla::GeckosScreen screen(h.host);
    screen.refresh();
    check(screen.codes().entries().size() == 5, "a render lists five stand-in codes");
    check(screen.codes().enabled().size() == 1, "one of them is on to begin with");
    check(screen.online_label() == L"GNT4", "and the list says which guide it stands in for");
    int unrunnable = 0;
    for (const auto& entry : screen.codes().entries()) {
        if (!entry.runnable) ++unrunnable;
        check(!entry.warning.empty() == !entry.runnable,
              "only the code that cannot run explains why");
    }
    check(unrunnable == 1, "exactly one stand-in cannot run here");
}

// Switching a code on keeps it pending until Apply: nothing is written by a check.
void toggling() {
    RecordingHost h;
    h.rendering = true;
    h.state.set_project(make_project(), L"naruto");
    nla::GeckosScreen screen(h.host);
    screen.refresh();
    screen.activate(nla::Id::CodeApply);  // a render: says so and writes nothing
    check(screen.note() == L"Applied in a render check only.",
          "Apply in a render says so rather than writing");
    check(h.refresh_calls == 0, "and does not re-sync a disk that is not there");

    const std::wstring runnable = L"Unlock Everything [Nick]";
    const std::wstring unrunnable = L"Enable DPad During Fights [Ralf]";
    check(!screen.codes().checked(runnable), "a code starts off");
    screen.toggle(runnable);
    check(screen.codes().checked(runnable), "and switching it on checks it");
    check(screen.codes().pending() != screen.codes().enabled(),
          "the check is pending: nothing is written until Apply");
    check(screen.note().empty(), "a clean toggle says nothing");

    screen.toggle(unrunnable);
    check(!screen.codes().checked(unrunnable), "a code this game cannot run stays off");
    check(screen.note().find(L"cannot run") != std::wstring::npos,
          "and the note says why it cannot be switched on");
}

// The box takes the keyboard while it has focus, and gives it back when it loses it.
void the_paste_box() {
    RecordingHost h;
    h.rendering = true;
    nla::GeckosScreen screen(h.host);
    screen.refresh();
    check(!screen.capturing(), "the box does not have the keyboard until it is clicked");

    screen.activate(nla::Id::CodeBox);
    check(screen.capturing(), "clicking the box gives it the keyboard");
    check(!h.text_input.empty() && h.text_input.back(), "and the window starts taking text");

    screen.text(L"$Infinite Time\n");
    check(screen.edit() == L"$Infinite Time\n", "typed text goes into the box");

    screen.key(nla::ScreenKey::Enter);
    check(screen.edit() == L"$Infinite Time\n\n", "Enter is a new line in the block");

    screen.key(nla::ScreenKey::Backspace);
    check(screen.edit() == L"$Infinite Time\n", "Backspace takes it back");

    h.clipboard_text = L"040374B8 60000000\n";
    screen.key(nla::ScreenKey::Paste);
    check(screen.edit() == L"$Infinite Time\n040374B8 60000000\n", "paste is appended");

    // Escape leaves the box, not the panel: the panel is what the window closes after.
    const int panels_before = h.panel_calls;
    screen.key(nla::ScreenKey::Escape);
    check(!screen.capturing(), "Escape takes the keyboard off the box");
    check(h.panel_calls == panels_before, "and leaves the panel open");
    check(!h.text_input.empty() && !h.text_input.back(), "and the window stops taking text");

    // While the box has the keys, the list's keys are not taken from it.
    screen.activate(nla::Id::CodeBox);
    check(!screen.key(nla::ScreenKey::Down), "the arrows are left to the window");
}

// Opening the panel shows it with the game's name, and says why the table is empty.
void opening() {
    RecordingHost h;
    nla::GeckosScreen screen(h.host);
    screen.open();
    check(h.panel_calls == 1 && h.panel == nla::OverlayPage::Geckos,
          "the panel is the Geckos page");
    check(h.panel_title == L"Geckos", "with the plain title when there is no game");
    check(screen.note().find(L"Set up the game folder first") != std::wstring::npos,
          "and says the folder has to be set up before there can be codes");

    RecordingHost h2;
    const fs::path root = make_project();
    h2.state.set_project(root, L"naruto");
    nla::GeckosScreen screen2(h2.host);
    screen2.open();
    check(h2.panel_title == L"Geckos — Naruto", "a project names the game in the title");
    check(screen2.note().find(L"No code guide is known") != std::wstring::npos,
          "and a disc with no guide says so rather than offering a fetch that would fail");
    check(screen2.note().find(L"ZZZZZZ") != std::wstring::npos, "naming the disc");
}

// A list longer than the panel scrolls, and cannot scroll itself empty.
void scrolling() {
    RecordingHost h;
    h.rendering = true;
    nla::GeckosScreen screen(h.host);
    screen.refresh();
    screen.set_rows_fit(2);
    screen.scroll(-1);
    check(screen.offset() == 3, "a notch down is three rows");
    screen.scroll(10);
    check(screen.offset() == 0, "and the top is the floor");
    screen.scroll(-50);
    screen.clamp_offset();
    check(screen.offset() <= screen.codes().entries().size(), "the clamp keeps it on the list");

    screen.set_rows_fit(12);
    screen.scroll(-50);
    screen.clamp_offset();
    check(screen.offset() == 0, "a panel with room for every row shows them all");
}

// Nothing is switched on, so the overlay the mods write has nothing from here.
void what_the_mods_are_given() {
    RecordingHost h;
    h.rendering = true;
    h.state.set_project(make_project(), L"naruto");
    nla::GeckosScreen screen(h.host);
    screen.refresh();
    check(screen.enabled_entries().empty(),
          "a render writes no codes into the mods' overlay");
}

int run() {
    the_ids_it_owns();
    the_stand_in_list();
    toggling();
    the_paste_box();
    opening();
    scrolling();
    what_the_mods_are_given();
    std::printf("%s\n", failures ? "some checks failed" : "all checks passed");
    return failures ? 1 : 0;
}

}  // namespace

#if defined(_WIN32)
int wmain() { return run(); }
#else
int main() { return run(); }
#endif
