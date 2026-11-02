// The project folder, the games in it, and the cover art the page draws for them.

#include "games_screen.h"

#include "image.h"
#include "picker_screen.h"

#include "tools.h"
#include "util.h"

#include <algorithm>

namespace nla {

GamesScreen::GamesScreen(ScreenHost& host, ProjectState& model, PickerScreen& picker)
    : host_(host), model_(model), picker_(picker) {}

namespace {

constexpr wchar_t kTitle[] = L"Naruto: Clash of Ninja";

// The box art collection libretro-thumbnails keeps: one file per disc, named after the
// disc's title. Art belongs to the publisher, so the launcher fetches it on the player's
// machine instead of shipping it, and the player can drop a file of their own in its
// place.
constexpr wchar_t kBoxArtBase[] =
    L"https://raw.githubusercontent.com/libretro-thumbnails/Nintendo_-_GameCube/master/"
    L"Named_Boxarts/";

std::wstring box_art_url(const std::wstring& folder) {
    std::wstring name;
    if (folder == L"naruto") {
        name = L"Naruto%20-%20Clash%20of%20Ninja%20(USA).png";
    } else if (folder == L"naruto2") {
        name = L"Naruto%20-%20Clash%20of%20Ninja%202%20(USA).png";
    } else if (folder == L"gnt3") {
        name = L"Naruto%20-%20Gekitou%20Ninja%20Taisen!%203%20(Japan).png";
    } else if (folder == L"gnt4") {
        name = L"Naruto%20-%20Gekitou%20Ninja%20Taisen!%204%20(Japan).png";
    } else if (folder == L"narutoeu") {
        name =
            L"Naruto%20-%20Clash%20of%20Ninja%20-%20European%20Version%20(Europe)%20"
            L"(En,Fr,De,Es,It).png";
    }
    if (name.empty()) return {};
    return kBoxArtBase + name;
}

}  // namespace


void GamesScreen::locate_project() {
    fs::path start = model_.project() ? model_.project()->root() : home_dir();
    picker_.choose_path(PickerKind::Folder, L"Choose the folder to put the game folders in", start,
                [this](const fs::path& chosen) {
                    Project candidate(chosen, model_.settings().game);
                    auto text = chosen.wstring();
                    if (!candidate.complete()) {
                        // An empty folder is the normal case: the projects are the
                        // launcher's to write, since they are not in the package. A folder
                        // missing only the recompiler gets it here as well.
                        std::wstring error;
                        if (!Project::create(chosen, &error)) {
                            host_.message(L"Could not set the folder up", error);
                            return;
                        }
                        host_.log(format(L"[launcher] wrote the game folders in %ls\n",
                                          text.c_str()));
                        candidate = Project(chosen, model_.settings().game);
                    }
                    if (candidate.valid()) {
                        model_.set_project(chosen, model_.settings().game);
                        art_.clear();
                        fetch_covers();
                        model_.settings().game = model_.project()->game();
                        host_.set_play_status({});
                        host_.settings().save();
                        host_.log(format(L"[launcher] project: %ls\n", text.c_str()));
                        host_.set_title(current_title() + L" — Launcher");
                        host_.refresh();
                        host_.launch_if_wanted();
                    } else {
                        host_.message(L"Not a project folder",
                                     std::wstring(L"That folder does not hold the game "
                                                  L"folders and tools/extract_disc.py:\n\n") +
                                         text);
                    }
                });
}

std::vector<GameInfo> GamesScreen::available_games() const {
    // All of them, always: the covers are the launcher's whole first page, and a game
    // whose folder does not exist yet gets it when a disc is chosen for it.
    if (!model_.project()) return {};
    return std::vector<GameInfo>(Project::games());
}

const Cover* GamesScreen::cover(const std::wstring& folder) {
    if (!model_.project()) return nullptr;
    // The key carries the project root, so a moved or newly chosen project folder does not
    // pick up art from the old one.
    std::wstring key = (model_.project()->root() / folder).wstring();
    // covers/ holds one file per game, named after its folder. A cover.png inside the game
    // folder is where art lived before covers/ existed, and still works.
    std::vector<fs::path> candidates;
    for (const wchar_t* extension : {L".png", L".jpg", L".jpeg"})
        candidates.push_back(model_.project()->covers_dir() / (folder + extension));
    for (const wchar_t* name : {L"cover.png", L"cover.jpg", L"cover.jpeg"})
        candidates.push_back(model_.project()->root() / folder / name);
    return art_.load(key, candidates);
}

void GamesScreen::fetch_covers() {
    if (!model_.project() || host_.rendering() || fetching_) return;
    std::vector<std::pair<std::wstring, std::wstring>> wanted;
    for (const auto& game : available_games()) {
        if (cover(game.folder)) continue;
        auto url = box_art_url(game.folder);
        if (!url.empty()) wanted.emplace_back(game.folder, url);
    }
    if (wanted.empty()) return;
    auto curl = curl_tool();
    if (curl.empty()) return;

    fs::path folder = model_.project()->covers_dir();
    std::error_code ec;
    fs::create_directories(folder, ec);
    if (ec) return;

    if (thread_.joinable()) thread_.join();
    fetching_ = true;
    host_.log(format(L"[launcher] fetching box art for %d game(s)\n",
                      static_cast<int>(wanted.size())));
    thread_ = std::thread([this, folder, curl, wanted] {
        int fetched = 0;
        for (const auto& [game, url] : wanted) {
            if (stop_) return;
            // curl writes beside the target and the file is moved into place, so a
            // failed or cut-off download leaves no half image to draw.
            fs::path partial = folder / (game + L".part");
            std::error_code ec;
            int status = -1;
            capture_output(curl,
                           {L"-fsSL", L"--max-time", L"12", L"--connect-timeout", L"5", L"-o",
                            partial.wstring(), url},
                           nullptr, &status);
            if (status == 0 && fs::is_regular_file(partial, ec)) {
                fs::rename(partial, folder / (game + L".png"), ec);
                if (!ec) ++fetched;
            } else {
                fs::remove(partial, ec);
            }
        }
        if (stop_) return;
        host_.post([this, fetched] {
            fetching_ = false;
            if (fetched == 0) return;
            art_.clear();
            host_.log(format(L"[launcher] box art: %d cover(s) now in covers/\n", fetched));
            host_.invalidate();
        });
    });
}

std::wstring GamesScreen::current_title() const {
    return model_.project() ? model_.project()->title() : std::wstring(kTitle);
}

void GamesScreen::select_game(const std::wstring& folder) {
    if (!model_.project() || host_.busy() || host_.game_running()) return;
    if (folder == model_.project()->game()) return;
    if (Project::game_info(folder).folder != folder) return;
    // A game the launcher has not set up yet is still a game: it is offered on the cover
    // grid, and its folder is written as it is chosen, so the disc it is given has
    // somewhere to go. Writing the folders leaves the ones already there alone, so a
    // project an older launcher made gets the games it is missing.
    std::error_code ec;
    if (!host_.rendering() &&
        !fs::is_regular_file(model_.project()->root() / folder / L"CMakeLists.txt", ec)) {
        std::wstring error;
        if (!Project::create(model_.project()->root(), &error))
            host_.log(format(L"[launcher] the game folders were not written: %ls\n",
                              error.c_str()));
    }
    model_.select_game(folder);
    model_.settings().game = model_.project()->game();
    host_.set_play_status({});
    // A render only paints the result: it must not remember the game a check clicked.
    if (!host_.rendering()) host_.settings().save();
    host_.set_title(current_title() + L" — Launcher");
    host_.log(format(L"[launcher] game: %ls\n", model_.project()->title().c_str()));
    host_.refresh();
}


const Cover& GamesScreen::placeholder_tile() { return art_.placeholder(); }

void GamesScreen::stop_fetching() { stop_ = true; }

void GamesScreen::shutdown() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
}

bool GamesScreen::handles(Id id) const {
    return id >= Id::GameCover && id < Id::GameCoverEnd;
}

void GamesScreen::activate(Id id) {
    const int index = static_cast<int>(id) - static_cast<int>(Id::GameCover);
    const auto games = available_games();
    if (id >= Id::GameCover && id < Id::GameCoverEnd && index < static_cast<int>(games.size())) {
        select_game(games[static_cast<std::size_t>(index)].folder);
    }
}

}  // namespace nla
