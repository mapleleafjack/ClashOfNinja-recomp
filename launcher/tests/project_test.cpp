// Checks which game a project folder answers with: a game the launcher knows is the game
// asked for even before its folder is written, so choosing a game the launcher has not set
// up yet does not fall back to another one. Run by ctest; it works in a temporary folder
// and needs no game.

#include "project.h"
#include "project_state.h"
#include "util.h"

#include <cstdio>
#include <string>
#include <utility>

namespace fs = nla::fs;

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    std::printf("%s %s\n", condition ? "ok  " : "FAIL", what);
    if (!condition) ++failures;
}

// A game folder as the launcher writes it, with only what the project reads back.
void write_game(const fs::path& root, const std::wstring& folder, const std::wstring& title,
                const std::wstring& disc) {
    std::error_code ec;
    fs::create_directories(root / folder, ec);
    nla::write_text_file(root / folder / L"CMakeLists.txt",
                         nla::narrow(L"gcn_add_game(\n  NAME " + folder + L"\n  TITLE \"" +
                                     title + L"\"\n  DISC_ID " + disc + L"\n)\n"));
}

// The project an older launcher made: the first and the fourth game, and nothing else.
void older_project(const fs::path& root) {
    std::error_code ec;
    fs::create_directories(root / L"tools", ec);
    nla::write_text_file(root / L"tools" / L"extract_disc.py", "# stand-in\n");
    write_game(root, L"naruto", L"Naruto: Clash of Ninja", L"GNREDA");
    write_game(root, L"gnt4", L"Naruto: Gekitou Ninja Taisen! 4", L"G4NJDA");
}

void choosing(const fs::path& root) {
    nla::Project first(root, L"naruto");
    check(first.game() == L"naruto", "a game that is set up is the game chosen");
    check(first.valid(), "the project with its game is valid");
    // The state a setup that gave up on the recompiler leaves behind: the game folders
    // answer, but a build would stop at `add_subdirectory(../gcn-recomp)`.
    check(!first.complete(), "a project with no recompiler beside the games is not complete");

    // The bug this covers: naruto2 has no folder yet, and the project answered with the
    // first game it found instead, so the cover grid could not leave Clash of Ninja.
    for (const auto& wanted : {L"naruto2", L"gnt3", L"narutoeu"}) {
        nla::Project game(root, wanted);
        check(game.game() == wanted, nla::narrow(nla::format(
                                         L"the game chosen with no folder yet: %ls", wanted))
                                         .c_str());
        check(!game.valid(), nla::narrow(nla::format(
                                 L"a game with no folder yet is not set up: %ls", wanted))
                                 .c_str());
        check(game.game_dir() == root / wanted,
              nla::narrow(nla::format(L"the game folder is the one chosen: %ls", wanted))
                  .c_str());
    }

    nla::Project fourth(root, L"gnt4");
    check(fourth.game() == L"gnt4", "the fourth game is reachable without the others");
    check(fourth.valid(), "the fourth game's folder is set up");
    check(fourth.title() == L"Naruto: Gekitou Ninja Taisen! 4", "the title comes from its folder");
    check(fourth.disc_id() == L"G4NJDA", "the disc ID comes from its folder");

    // Nothing chosen, or a name the launcher does not know: the first game that is there.
    check(nla::Project(root, L"").game() == L"naruto", "with no game chosen, the first one is used");
    check(nla::Project(root, L"nowhere").game() == L"naruto",
          "an unknown game falls back to one that is there");

    // The folder is written before the game is used, which is what makes it valid.
    write_game(root, L"naruto2", L"Naruto: Clash of Ninja 2", L"GNUEDA");
    nla::Project second(root, L"naruto2");
    check(second.valid(), "the game is set up once its folder is written");
    check(second.title() == L"Naruto: Clash of Ninja 2", "its own title, not the first game's");
    check(second.disc_id() == L"GNUEDA", "its own disc, not the first game's");
}

void games() {
    const auto& all = nla::Project::games();
    check(all.size() == 5, "the launcher offers five games");
    check(all.front().folder == L"naruto", "the first game is Clash of Ninja");
    check(nla::Project::game_info(L"narutoeu").folder == L"narutoeu",
          "a known game answers with itself");
    check(nla::Project::game_info(L"narutoeu").short_title == L"Clash of Ninja EU",
          "the European version has a short title");
    check(nla::Project::game_info(L"nowhere").folder == all.front().folder,
          "an unknown game answers with the first one");
    check(nla::Project::default_game() == L"naruto", "the default game is the first one");
}

// The other half of choosing a game: the folders are the launcher's to write, so a project
// an older launcher made gets the games it is missing, each reading its own disc back.
void setup(const fs::path& root) {
    std::error_code ec;
    older_project(root);

    std::wstring error;
    if (!nla::Project::create(root, &error)) {
        // Writing the folders needs the recompiler and the scripts the launcher travels
        // with above it, which a build inside this checkout has and a build made elsewhere
        // has not.
        std::printf("skip the folders: %s\n", nla::narrow(error).c_str());
        return;
    }
    const std::pair<const wchar_t*, const wchar_t*> discs[] = {
        {L"naruto", L"GNREDA"},  {L"naruto2", L"GNUEDA"}, {L"gnt3", L"G3NJDA"},
        {L"gnt4", L"G4NJDA"},    {L"narutoeu", L"GNUPDA"},
    };
    for (const auto& [folder, disc] : discs) {
        nla::Project game(root, folder);
        check(game.valid(), nla::narrow(nla::format(L"%ls is set up", folder)).c_str());
        check(game.disc_id() == disc,
              nla::narrow(nla::format(L"%ls reads its own disc %ls", folder, disc)).c_str());
    }
    // What the game folder had is kept: writing the folder never replaces a file.
    nla::write_text_file(root / L"naruto" / L"controls.txt", "mine\n");
    nla::Project::create(root, &error);
    check(nla::read_text_file(root / L"naruto" / L"controls.txt") == L"mine\n",
          "writing the folders leaves a file the player has alone");
    // The recompiler the games add is what a build reads first, so a written project has it.
    check(nla::Project(root, L"naruto").complete(),
          "the written project has the recompiler beside its game folders");
    fs::remove_all(root, ec);
}

// The three things that have to agree: the project in use, the settings, and what the
// project's files say. The status used to be re-read wherever a caller happened to remember,
// which is what this replaces.
void state(const fs::path& root) {
    nla::ProjectState ps;
    ps.load_settings();
    check(!ps.project(), "a state with nothing loaded has no project");
    check(!ps.status().disc.extracted && !ps.status().built,
          "and no status to go with it");

    ps.set_project(root, L"gnt4");
    check(ps.project().has_value(), "taking a project keeps it");
    check(ps.project()->game() == L"gnt4", "and the game asked for is the one in use");
    check(!ps.status().disc.extracted,
          "and its status is read with it: no disc extracted, so nothing is offered");
    check(!ps.status().built, "and nothing is built");

    const std::wstring settings_game = ps.settings().game;
    ps.set_project(root, L"naruto");
    check(ps.settings().game == settings_game, "taking a project leaves the settings alone");

    ps.select_game(L"gnt4");
    check(ps.project().has_value() && ps.project()->game() == L"gnt4",
          "selecting a game changes the game");
    check(ps.project()->root() == root, "and keeps the folder it was working in");

    ps.clear_project();
    check(!ps.project(), "clearing forgets the project");
    check(!ps.status().disc.extracted && !ps.status().built,
          "and its status goes with it, so the page cannot offer a build that is not there");
}

int run() {
    std::error_code ec;
    fs::path root = fs::temp_directory_path(ec) / L"NarutoLauncher-project-test";
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    if (ec) {
        std::printf("FAIL cannot create %s\n", nla::narrow(root.wstring()).c_str());
        return 1;
    }
    older_project(root);
    state(root);
    choosing(root);
    games();
    setup(root / L"setup");
    fs::remove_all(root, ec);

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
