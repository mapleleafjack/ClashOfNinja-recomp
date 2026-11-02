// The project folder, the games in it, and the cover art the page draws for them.

#include "app.h"

#include "image.h"

#include "util.h"

#include <algorithm>
#include <atomic>
#include <thread>

namespace nla {

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

// -- state -----------------------------------------------------------------

void App::refresh() {
    // The mods come first: their files and codes are what the game's status is measured
    // against, so the status is read after they have been written.
    refresh_mods();
    write_mod_overlay();
    if (project_) status_ = project_->status();
    invalidate();
}

void App::locate_project() {
    fs::path start = project_ ? project_->root() : home_dir();
    choose_path(PickerKind::Folder, L"Choose the folder to put the game folders in", start,
                [this](const fs::path& chosen) {
                    Project candidate(chosen, settings_.game);
                    auto text = chosen.wstring();
                    if (!candidate.valid()) {
                        // An empty folder is the normal case: the projects are the
                        // launcher's to write, since they are not in the package.
                        std::wstring error;
                        if (!Project::create(chosen, &error)) {
                            show_message(L"Could not set the folder up", error);
                            return;
                        }
                        append_log(format(L"[launcher] wrote the game folders in %ls\n",
                                          text.c_str()));
                        candidate = Project(chosen, settings_.game);
                    }
                    if (candidate.valid()) {
                        project_.emplace(chosen, settings_.game);
                        covers_.clear();
                        fetch_covers();
                        settings_.game = project_->game();
                        play_status_.clear();
                        save_settings();
                        append_log(format(L"[launcher] project: %ls\n", text.c_str()));
                        window_.set_title(current_title() + L" — Launcher");
                        refresh();
                        if (launch_wanted_) {
                            launch_wanted_ = false;
                            launch();
                        }
                    } else {
                        show_message(L"Not a project folder",
                                     std::wstring(L"That folder does not hold the game "
                                                  L"folders and tools/extract_disc.py:\n\n") +
                                         text);
                    }
                });
}

std::vector<GameInfo> App::available_games() const {
    // All of them, always: the covers are the launcher's whole first page, and a game
    // whose folder does not exist yet gets it when a disc is chosen for it.
    if (!project_) return {};
    return std::vector<GameInfo>(Project::games());
}

const App::Cover* App::cover(const std::wstring& folder) {
    if (!project_) return nullptr;
    // The key carries the project root, so a moved or newly chosen project folder does
    // not pick up art from the old one.
    std::wstring key = (project_->root() / folder).wstring();
    auto found = covers_.find(key);
    if (found != covers_.end()) return found->second.loaded ? &found->second : nullptr;

    Cover art;
    std::error_code ec;
    // covers/ holds one file per game, named after its folder. A cover.png inside the
    // game folder is where art lived before covers/ existed, and still works.
    for (const wchar_t* extension : {L".png", L".jpg", L".jpeg"}) {
        fs::path path = project_->covers_dir() / (folder + extension);
        if (!fs::is_regular_file(path, ec)) continue;
        art.loaded = load_image(path, &art.width, &art.height, art.pixels);
        break;
    }
    if (!art.loaded) {
        for (const wchar_t* name : {L"cover.png", L"cover.jpg", L"cover.jpeg"}) {
            fs::path path = project_->root() / folder / name;
            if (!fs::is_regular_file(path, ec)) continue;
            art.loaded = load_image(path, &art.width, &art.height, art.pixels);
            break;
        }
    }
    Cover& stored = covers_.emplace(key, std::move(art)).first->second;
    return stored.loaded ? &stored : nullptr;
}

void App::fetch_covers() {
    if (!project_ || rendering_ || covers_fetching_) return;
    std::vector<std::pair<std::wstring, std::wstring>> wanted;
    for (const auto& game : available_games()) {
        if (cover(game.folder)) continue;
        auto url = box_art_url(game.folder);
        if (!url.empty()) wanted.emplace_back(game.folder, url);
    }
    if (wanted.empty()) return;
    auto curl = curl_tool();
    if (curl.empty()) return;

    fs::path folder = project_->covers_dir();
    std::error_code ec;
    fs::create_directories(folder, ec);
    if (ec) return;

    if (covers_thread_.joinable()) covers_thread_.join();
    covers_fetching_ = true;
    append_log(format(L"[launcher] fetching box art for %d game(s)\n",
                      static_cast<int>(wanted.size())));
    covers_thread_ = std::thread([this, folder, curl, wanted] {
        int fetched = 0;
        for (const auto& [game, url] : wanted) {
            if (covers_stop_) return;
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
        if (covers_stop_) return;
        post([this, fetched] {
            covers_fetching_ = false;
            if (fetched == 0) return;
            covers_.clear();
            append_log(format(L"[launcher] box art: %d cover(s) now in covers/\n", fetched));
            invalidate();
        });
    });
}

const App::Cover& App::placeholder_cover() {
    if (placeholder_.loaded) return placeholder_;

    // A still, dark box with the launcher's accent glowing behind where the logo sits,
    // so an empty cover looks deliberate rather than broken.
    constexpr int kWidth = 300;
    constexpr int kHeight = 400;
    const float glow_x = kWidth * 0.5f;
    const float glow_y = kHeight * 0.33f;
    const float glow_radius = kWidth * 0.62f;

    placeholder_.width = kWidth;
    placeholder_.height = kHeight;
    placeholder_.pixels.resize(static_cast<size_t>(kWidth) * kHeight);
    const Color top = rgb(0x262d39);
    const Color bottom = rgb(0x151920);
    const Color accent = rgb(theme::Accent);
    for (int y = 0; y < kHeight; ++y) {
        float t = static_cast<float>(y) / (kHeight - 1);
        for (int x = 0; x < kWidth; ++x) {
            float distance = std::hypot(static_cast<float>(x) - glow_x,
                                        static_cast<float>(y) - glow_y) /
                             glow_radius;
            float glow = std::pow(std::clamp(1.0f - distance, 0.0f, 1.0f), 2.4f) * 0.30f;
            auto channel = [&](float from, float to, float light) {
                float value = from + (to - from) * t;
                value += (light - value) * glow;
                return static_cast<std::uint32_t>(
                    std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
            };
            std::uint32_t red = channel(top.r, bottom.r, accent.r);
            std::uint32_t green = channel(top.g, bottom.g, accent.g);
            std::uint32_t blue = channel(top.b, bottom.b, accent.b);
            placeholder_.pixels[static_cast<size_t>(y) * kWidth + x] =
                0xFF000000u | (red << 16) | (green << 8) | blue;
        }
    }
    placeholder_.loaded = true;
    return placeholder_;
}

std::wstring App::current_title() const {
    return project_ ? project_->title() : std::wstring(kTitle);
}

void App::select_game(const std::wstring& folder) {
    if (!project_ || busy_ || game_.running()) return;
    if (folder == project_->game()) return;
    if (Project::game_info(folder).folder != folder) return;
    // A game the launcher has not set up yet is still a game: it is offered on the cover
    // grid, and its folder is written as it is chosen, so the disc it is given has
    // somewhere to go. Writing the folders leaves the ones already there alone, so a
    // project an older launcher made gets the games it is missing.
    std::error_code ec;
    if (!rendering_ && !headless_ &&
        !fs::is_regular_file(project_->root() / folder / L"CMakeLists.txt", ec)) {
        std::wstring error;
        if (!Project::create(project_->root(), &error))
            append_log(format(L"[launcher] the game folders were not written: %ls\n",
                              error.c_str()));
    }
    project_ = Project(project_->root(), folder);
    settings_.game = project_->game();
    play_status_.clear();
    // A render only paints the result: it must not remember the game a check clicked.
    if (!rendering_) save_settings();
    window_.set_title(current_title() + L" — Launcher");
    append_log(format(L"[launcher] game: %ls\n", project_->title().c_str()));
    refresh();
}

void App::launch_game(const std::wstring& folder) {
    if (busy_ || game_.running()) return;
    if (!project_) {
        launch();
        return;
    }
    select_game(folder);
    launch();
}

}  // namespace nla
