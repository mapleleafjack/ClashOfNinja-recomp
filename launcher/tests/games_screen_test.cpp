// What the game page does: which ids it answers for, what a cover click chooses, what a
// project folder answers with, and what the title and the covers say about it. All of it
// lived in App and in a 200-line file beside it, reachable only through a window. Run by
// ctest; no window, no disc, no network.

#include "games_screen.h"

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
    int launches = 0;
    std::wstring title;
    std::wstring play_status = L"something";
    std::vector<std::wstring> logged;
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
        host.status = [this]() -> const nla::ProjectStatus& { return state.status(); };
        host.rendering = [this] { return rendering; };
        host.refresh = [this] { ++refreshes; };
        host.settings = [this]() -> nla::Settings& { return state.settings(); };
        host.settings_note = [](const std::wstring&) {};
        host.launch_cancelled = [] {};
        host.start_job = [](std::vector<nla::Task>, nla::JobOptions) {};
        host.elide_path = [](const std::wstring& text) { return text; };
        host.set_title = [this](const std::wstring& text) { title = text; };
        host.set_play_status = [this](const std::wstring& text) { play_status = text; };
        host.launch_if_wanted = [this] { ++launches; };
        host.clipboard = [] { return std::wstring(); };
        host.text_input = [](bool) {};
        host.take_over = [] {};
        host.show_panel = [](nla::OverlayPage, const std::wstring&) {};
        host.native_chooser = [](nla::PickerKind, const std::wstring&, const fs::path&) {
            return nla::NativeAnswer{};
        };
    }
};

struct Page {
    RecordingHost host;
    nla::PickerScreen picker{host.host};
    nla::GamesScreen games{host.host, host.state, picker};
};

// A project folder the launcher would write: two games, the recompiler, and the extractor.
fs::path make_project() {
    const fs::path root = nla::temp_dir() / L"nla-games-screen-test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / L"naruto", ec);
    fs::create_directories(root / L"gnt4", ec);
    nla::write_text_file(root / L"naruto" / L"CMakeLists.txt",
                         "gcn_add_game(\n  NAME naruto\n  TITLE \"Naruto: Clash of Ninja\"\n"
                         "  DISC_ID GNREDA\n)\n");
    nla::write_text_file(root / L"gnt4" / L"CMakeLists.txt",
                         "gcn_add_game(\n  NAME gnt4\n  TITLE \"Naruto: Gekitou Ninja Taisen! 4\"\n"
                         "  DISC_ID G4NJDA\n)\n");
    return root;
}

void the_ids_it_owns() {
    Page p;
    check(p.games.handles(nla::Id::GameCover), "the first cover");
    check(p.games.handles(static_cast<nla::Id>(static_cast<int>(nla::Id::GameCover) + 3)),
          "a cover further along the grid");
    check(!p.games.handles(nla::Id::Play), "and not the Play button, which starts a job");
    check(!p.games.handles(nla::Id::GameCoverEnd), "nor the id past the last cover");
    check(!p.games.handles(nla::Id::Mods), "nor the Mods button");
}

// The title names the game on the page, and falls back to the launcher's own name when there
// is no project at all.
void the_title() {
    Page p;
    check(p.games.current_title() == L"Naruto: Clash of Ninja",
          "with no project the title is the launcher's own");
    p.host.state.set_project(make_project(), L"gnt4");
    check(p.games.current_title() == L"Naruto: Gekitou Ninja Taisen! 4",
          "with a project it names the game in use");
}

// The grid offers every game the launcher knows, not only the ones it has set up.
void the_grid() {
    Page p;
    check(p.games.available_games().empty(), "with no project there is no grid");
    p.host.state.set_project(make_project(), L"naruto");
    const auto games = p.games.available_games();
    check(!games.empty(), "with a project the grid has the games the launcher knows");
    bool found_gnt4 = false;
    for (const auto& game : games) {
        if (game.folder == L"gnt4") found_gnt4 = true;
    }
    check(found_gnt4, "including one that was never set up on its own");
}

// Clicking a cover chooses that game: the project switches, the title follows, and the line
// under the Play button is cleared.
void choosing_a_game() {
    Page p;
    p.host.state.set_project(make_project(), L"naruto");
    const auto games = p.games.available_games();
    int gnt4 = -1;
    for (int i = 0; i < static_cast<int>(games.size()); ++i) {
        if (games[static_cast<std::size_t>(i)].folder == L"gnt4") gnt4 = i;
    }
    check(gnt4 >= 0, "the grid knows where gnt4 sits");

    p.host.state.settings().game = L"naruto";
    p.host.play_status = L"Starting…";
    p.games.activate(static_cast<nla::Id>(static_cast<int>(nla::Id::GameCover) + gnt4));

    check(p.host.state.project()->game() == L"gnt4", "the cover chooses that game");
    check(p.host.state.settings().game == L"gnt4", "and the choice is remembered");
    check(p.host.title.find(L"Gekitou Ninja Taisen! 4") != std::wstring::npos,
          "and the window title follows");
    check(p.host.play_status.empty(), "and the line under Play is cleared");
    check(p.host.refreshes == 1, "and the session is re-synced once");
    check(!p.host.logged.empty(), "and Activity is told which game is in use");
}

// A cover click in a render paints the result but remembers nothing: the check would
// otherwise write the game it clicked into the player's settings.
void choosing_a_game_in_a_render() {
    Page p;
    p.host.rendering = true;
    p.host.state.set_project(make_project(), L"naruto");
    p.host.state.settings().game = L"naruto";
    const auto games = p.games.available_games();
    int gnt4 = -1;
    for (int i = 0; i < static_cast<int>(games.size()); ++i) {
        if (games[static_cast<std::size_t>(i)].folder == L"gnt4") gnt4 = i;
    }
    p.games.activate(static_cast<nla::Id>(static_cast<int>(nla::Id::GameCover) + gnt4));
    check(p.host.state.project()->game() == L"gnt4", "a render still shows the game clicked");
}

// A click past the end of the grid does nothing, rather than choosing a game that is not
// there.
void a_click_past_the_grid() {
    Page p;
    p.host.state.set_project(make_project(), L"naruto");
    const std::wstring before = p.host.state.project()->game();
    p.games.activate(static_cast<nla::Id>(static_cast<int>(nla::Id::GameCover) + 30));
    check(p.host.state.project()->game() == before, "a click past the last cover changes nothing");
}

// A game already in use is not switched to again, and one the launcher does not know is not
// offered at all.
void choosing_nothing() {
    Page p;
    p.host.state.set_project(make_project(), L"naruto");
    p.games.select_game(L"naruto");
    check(p.host.refreshes == 0, "choosing the game already in use re-syncs nothing");
    p.games.select_game(L"nosuchgame");
    check(p.host.state.project()->game() == L"naruto",
          "and a folder the launcher does not know as a game is ignored");
}

// The tile drawn where a game has no art of its own still says where the file goes.
void the_placeholder_tile() {
    Page p;
    const nla::Cover& tile = p.games.placeholder_tile();
    check(tile.width > 0 && tile.height > 0, "there is a stand-in tile to draw");
    check(!tile.pixels.empty(), "and it has pixels");
}

int run() {
    the_ids_it_owns();
    the_title();
    the_grid();
    choosing_a_game();
    choosing_a_game_in_a_render();
    a_click_past_the_grid();
    choosing_nothing();
    the_placeholder_tile();
    std::printf("%s\n", failures ? "some checks failed" : "all checks passed");
    return failures ? 1 : 0;
}

}  // namespace

#if defined(_WIN32)
int wmain() { return run(); }
#else
int main() { return run(); }
#endif
