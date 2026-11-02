// What the Mods panel does: which ids it answers for, the stand-in table a render draws, what
// switching a row on stages, and what Apply does when there is nothing to write. All of it
// lived in App and in a 590-line file beside it, reachable only through a window. Run by
// ctest; no window, no disc, no network.

#include "mods_screen.h"

#include "geckos_screen.h"
#include "picker_screen.h"
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
    nla::ProjectState state;
    bool rendering = false;
    bool busy = false;
    int redraws = 0;
    int refreshes = 0;
    int panel_calls = 0;
    int messages = 0;
    nla::OverlayPage panel = nla::OverlayPage::None;
    std::wstring panel_title;
    std::vector<std::wstring> logged;
    nla::ScreenHost host;

    RecordingHost() {
        host.invalidate = [this] { ++redraws; };
        host.post = [](std::function<void()>) {};
        host.log = [this](const std::wstring& text) { logged.push_back(text); };
        host.message = [this](const std::wstring&, const std::wstring&) { ++messages; };
        host.update_prompt = [](const std::wstring&, const std::wstring&, const std::wstring&,
                                std::function<void()>) {};
        host.busy = [this] { return busy; };
        host.game_running = [] { return false; };
        host.rendering = [this] { return rendering; };
        host.refresh = [this] { ++refreshes; };
        host.settings_note = [](const std::wstring&) {};
        host.launch_cancelled = [] {};
        host.start_job = [](std::vector<nla::Task>, nla::JobOptions) {};
        host.elide_path = [](const std::wstring& text) { return text; };
        host.clipboard = [] { return std::wstring(); };
        host.text_input = [](bool) {};
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

struct Panel {
    RecordingHost host;
    nla::PickerScreen picker{host.host};
    nla::GeckosScreen geckos{host.host, host.state};
    nla::ModsScreen mods{host.host, host.state, picker, geckos};
};

fs::path make_project() {
    const fs::path root = nla::temp_dir() / L"nla-mods-screen-test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / L"naruto", ec);
    nla::write_text_file(root / L"naruto" / L"CMakeLists.txt",
                         "gcn_add_game(\n  NAME naruto\n  TITLE \"Naruto\"\n"
                         "  DISC_ID ZZZZZZ\n)\n");
    return root;
}

// The same folder with the disc the shipped catalog's SCON4 names, so the entry is a row.
fs::path make_gnt4_project() {
    const fs::path root = nla::temp_dir() / L"nla-mods-screen-test-gnt4";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / L"gnt4", ec);
    nla::write_text_file(root / L"gnt4" / L"CMakeLists.txt",
                         "gcn_add_game(\n  NAME gnt4\n  TITLE \"GNT4\"\n"
                         "  DISC_ID G4NJDA\n)\n");
    return root;
}

void the_ids_it_owns() {
    Panel p;
    check(p.mods.handles(nla::Id::ModRow), "the first mod row");
    check(p.mods.handles(static_cast<nla::Id>(static_cast<int>(nla::Id::ModRow) + 5)),
          "a mod row further down");
    check(p.mods.handles(nla::Id::ModsFolder), "the folder button");
    check(p.mods.handles(nla::Id::ModChoose), "the choose-a-patch button");
    check(p.mods.handles(nla::Id::ModApply), "the Apply button");
    check(!p.mods.handles(nla::Id::CodeRow), "and not a code row");
    check(!p.mods.handles(nla::Id::OverlayAction), "nor the panel's own Done button");
    check(!p.mods.handles(nla::Id::ModRowEnd), "nor the id past the last row");
}

// A render draws a stand-in table: three rows, one of them the patch.
void the_stand_in_table() {
    Panel p;
    p.host.rendering = true;
    p.mods.refresh();
    check(p.mods.selection().entries().size() == 3, "a render lists three stand-in mods");
    check(p.mods.selection().enabled().size() == 1, "one of them is on to begin with");

    int patches = 0;
    for (const auto& entry : p.mods.selection().entries()) {
        if (entry.patch) ++patches;
    }
    check(patches == 1, "exactly one row is a disc patch");
    check(p.mods.summary() == L"2 of 3 on.", "the summary counts the patch as on as well");
}

// A row is staged by its check and written only by Apply.
void staging_a_row() {
    Panel p;
    p.host.rendering = true;
    // open() is what makes the panel show what is written: a bare refresh leaves the staged
    // set empty, which is the state a re-sync from elsewhere puts it in.
    p.mods.open();
    check(!p.mods.selection().checked(L"alt-colours"), "a row starts off");
    p.mods.toggle(L"alt-colours");
    check(p.mods.selection().checked(L"alt-colours"), "and its check stages it");
    check(p.mods.selection().staged(), "so the panel has something to apply");
    p.mods.toggle(L"alt-colours");
    check(!p.mods.selection().staged(), "unchecking it again stages nothing");

    // One that is already on is staged by switching it off, which is the same rule seen from
    // the other side: what is staged is the difference from what is written.
    check(p.mods.selection().checked(L"retuned-opening"), "the stand-in table starts with one on");
    p.mods.toggle(L"retuned-opening");
    check(p.mods.selection().staged(), "switching one off stages it too");
    p.mods.toggle(L"retuned-opening");
    check(!p.mods.selection().staged(), "and switching it back on stages nothing");
}

// Apply in a render says so and writes nothing.
void applying_in_a_render() {
    Panel p;
    p.host.rendering = true;
    p.host.state.set_project(make_project(), L"naruto");
    p.mods.refresh();
    p.mods.apply();
    check(p.mods.note() == L"Applied in a render check only.",
          "Apply in a render says so rather than writing");
    check(p.host.refreshes == 0, "and does not re-sync a disk that is not there");
}

// Opening the panel shows it with the game's name, and says why the table is empty.
void opening() {
    {
        Panel p;
        p.mods.open();
        check(p.host.panel_calls == 1 && p.host.panel == nla::OverlayPage::Mods,
              "the panel is the Mods page");
        check(p.host.panel_title == L"Mods", "with the plain title when there is no game");
        check(p.mods.note().find(L"Set up the game folder first") != std::wstring::npos,
              "and says the folder has to be set up before there can be mods");
    }
    {
        Panel p;
        p.host.state.set_project(make_project(), L"naruto");
        p.mods.open();
        check(p.host.panel_title == L"Mods — Naruto", "a project names the game in the title");
        check(p.mods.note().find(L"No mods for this disc") != std::wstring::npos,
              "and a disc with no mods says a mod is a folder here with a mod.txt");
        check(p.host.refreshes == 0, "opening re-reads the mods, not the whole session");
    }
}

// The summary is the line under the table, and it speaks for the project it is about.
void the_summary() {
    Panel p;
    check(p.mods.summary() == L"Set up the game folder first.",
          "with no project the summary says so");
    p.host.state.set_project(make_project(), L"naruto");
    p.mods.refresh();
    check(p.mods.summary() == L"No mods for this disc.",
          "and with nothing on the disc it says that");
}

// A patch chosen here is staged, not applied; choosing the same one twice does not add a
// second row.
void staging_a_patch() {
    Panel p;
    p.host.state.set_project(make_project(), L"naruto");
    p.mods.refresh();
    const std::size_t before = p.mods.selection().entries().size();

    p.mods.stage_patch(fs::path(L"/tmp/Some Patch.xdelta"));
    check(p.mods.selection().entries().size() == before + 1, "choosing a patch adds a row");
    check(p.mods.selection().staged(), "and stages it");
    const std::wstring id = p.mods.selection().pending_patch();
    check(!id.empty(), "the staged patch is the one named");

    p.mods.stage_patch(fs::path(L"/tmp/Some Patch.xdelta"));
    check(p.mods.selection().entries().size() == before + 1,
          "choosing the same patch again does not add a second row");
    check(p.mods.selection().pending_patch() == id, "and it stays the staged one");
}

// The overlay the runtime reads is written from the rows here and the codes in Geckos; in a
// render there is no disc, so nothing is written and nothing is said.
void writing_the_overlay() {
    Panel p;
    p.host.rendering = true;
    p.host.state.set_project(make_project(), L"naruto");
    p.mods.refresh();
    p.mods.write_overlay();
    check(p.host.logged.empty(), "a render writes no overlay and says nothing about it");
}

// A delta the catalog names is that mod, even when it is kept outside this install's
// patches/: the setting an earlier install left behind must not add a row beside the one
// the catalog already lists.
void a_patch_kept_outside_the_install() {
    Panel p;
    const fs::path root = make_gnt4_project();
    std::error_code ec;
    fs::create_directories(root / L"elsewhere", ec);
    const fs::path delta = root / L"elsewhere" / L"SCON4-2.1.0.xdelta";
    nla::write_text_file(delta, "not a real delta; only the name is read here");
    p.host.state.set_project(root, L"gnt4");
    p.host.state.settings().patches[L"gnt4"] = delta.wstring();

    p.mods.open();
    const auto& entries = p.mods.selection().entries();
    check(entries.size() == 1, "a recorded patch the catalog names is one row, not two");
    if (entries.size() == 1) {
        check(entries[0].id == L"scon4", "and it is the catalog's entry");
        check(entries[0].installed, "read as installed, because the delta it names is here");
        check(entries[0].delta == delta, "with the delta it was played with");
        check(p.mods.selection().checked(L"scon4"), "and checked, as what the game plays");
    }
    check(p.mods.summary() == L"1 of 1 on.", "so the table is one row, and it is on");

    // A delta nothing names is still a row of its own, with no name, version or author.
    p.host.state.settings().patches[L"gnt4"] = (root / L"elsewhere" / L"Unknown-1.0.xdelta").wstring();
    nla::write_text_file(root / L"elsewhere" / L"Unknown-1.0.xdelta", "x");
    p.mods.open();
    check(p.mods.selection().entries().size() == 2,
          "a delta the catalog does not name keeps its own row beside the entry");
}

// The applied delta may be a different copy from the one in patches/. It is still the same
// mod, so the table is one row, checked, and the delta the game plays is that copy.
void an_applied_delta_that_is_another_copy() {
    Panel p;
    const fs::path root = make_gnt4_project();
    std::error_code ec;
    const fs::path in_patches = root / L"patches" / L"SCON4-2.1.0.xdelta";
    const fs::path elsewhere = root / L"elsewhere" / L"SCON4-2.1.0.xdelta";
    fs::create_directories(in_patches.parent_path(), ec);
    fs::create_directories(elsewhere.parent_path(), ec);
    nla::write_text_file(in_patches, "the copy patches/ holds");
    nla::write_text_file(elsewhere, "the copy the game plays");
    p.host.state.set_project(root, L"gnt4");
    p.host.state.settings().patches[L"gnt4"] = elsewhere.wstring();

    p.mods.open();
    const auto& entries = p.mods.selection().entries();
    check(entries.size() == 1, "one mod played from another copy is still one row");
    if (entries.size() == 1) {
        check(entries[0].id == L"scon4", "the catalog's entry");
        check(entries[0].delta == elsewhere, "showing the copy the game plays");
        check(p.mods.selection().checked(L"scon4"), "and checked");
    }
}

// A refresh re-reads the disk, but the checks the player has staged are still theirs until
// Apply writes them; one whose row is gone is dropped.
void a_refresh_keeps_the_staged_checks() {
    Panel p;
    const fs::path root = make_project();
    std::error_code ec;
    fs::create_directories(root / L"mods" / L"alt-colours", ec);
    nla::write_text_file(root / L"mods" / L"alt-colours" / L"mod.txt",
                         "name=Alternate Colours\ndisc_id=ZZZZZZ\n");
    p.host.state.set_project(root, L"naruto");

    p.mods.open();
    p.mods.toggle(L"alt-colours");
    check(p.mods.selection().checked(L"alt-colours"), "a check is staged");

    p.mods.refresh();
    check(p.mods.selection().checked(L"alt-colours"),
          "and a refresh in between does not drop it");
    check(p.mods.selection().staged(), "so Apply still has something to write");

    fs::remove_all(root / L"mods" / L"alt-colours", ec);
    p.mods.refresh();
    check(!p.mods.selection().staged(), "a check whose row went away is dropped");
}

int run() {
    the_ids_it_owns();
    the_stand_in_table();
    staging_a_row();
    applying_in_a_render();
    opening();
    the_summary();
    staging_a_patch();
    writing_the_overlay();
    a_patch_kept_outside_the_install();
    an_applied_delta_that_is_another_copy();
    a_refresh_keeps_the_staged_checks();
    std::printf("%s\n", failures ? "some checks failed" : "all checks passed");
    return failures ? 1 : 0;
}

}  // namespace

#if defined(_WIN32)
int wmain() { return run(); }
#else
int main() { return run(); }
#endif
