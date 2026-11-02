// Everything the launcher draws: the pages, the widgets they are built from, and the
// off-screen render used to check them.

#include "app.h"
#include "app_shared.h"

#include "util.h"

#include <algorithm>
#include <cstdio>

namespace nla {

namespace {

constexpr wchar_t kSubtitle[] = L"GameCube static recompilation";

std::wstring scale_label(int factor) {
    return format(L"%d×   %d × %d", factor, 640 * factor, 480 * factor);
}

// How many lines `text` takes when Painter draws it wrapped into `width`: words break at
// spaces and each newline starts a paragraph. A block that sizes itself to this before
// drawing keeps its text from being cut off.
int wrapped_lines(Painter& painter, const std::wstring& text, Face face, float width) {
    int lines = 0;
    size_t start = 0;
    do {
        size_t end = text.find(L'\n', start);
        if (end == std::wstring::npos) end = text.size();
        const std::wstring paragraph = text.substr(start, end - start);
        ++lines;
        std::wstring line;
        size_t word_start = 0;
        while (word_start <= paragraph.size()) {
            size_t word_end = paragraph.find(L' ', word_start);
            if (word_end == std::wstring::npos) word_end = paragraph.size();
            const std::wstring word = paragraph.substr(word_start, word_end - word_start);
            std::wstring candidate = line.empty() ? word : line + L" " + word;
            if (!line.empty() && painter.measure(candidate, face) > width) {
                ++lines;
                line = word;
            } else {
                line = candidate;
            }
            word_start = word_end + 1;
        }
        start = end + 1;
    } while (start <= text.size());
    return std::max(1, lines);
}

bool write_bmp(const std::string& path, const std::uint32_t* pixels, int width, int height) {
    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (!file) return false;
    const std::uint32_t image_size = static_cast<std::uint32_t>(width) * height * 4;
    std::uint8_t header[54] = {};
    header[0] = 'B';
    header[1] = 'M';
    auto store32 = [](std::uint8_t* at, std::uint32_t value) {
        at[0] = static_cast<std::uint8_t>(value);
        at[1] = static_cast<std::uint8_t>(value >> 8);
        at[2] = static_cast<std::uint8_t>(value >> 16);
        at[3] = static_cast<std::uint8_t>(value >> 24);
    };
    store32(header + 2, 54 + image_size);
    store32(header + 10, 54);
    store32(header + 14, 40);
    store32(header + 18, static_cast<std::uint32_t>(width));
    store32(header + 22, static_cast<std::uint32_t>(height));
    header[26] = 1;
    header[28] = 32;
    store32(header + 34, image_size);
    bool ok = std::fwrite(header, 1, sizeof(header), file) == sizeof(header);
    // BMP rows run bottom to top and hold BGRA.
    for (int y = height - 1; ok && y >= 0; --y) {
        const std::uint32_t* row = pixels + static_cast<size_t>(y) * width;
        for (int x = 0; x < width; ++x) {
            std::uint32_t pixel = row[x];
            std::uint8_t bgra[4] = {static_cast<std::uint8_t>(pixel),
                                    static_cast<std::uint8_t>(pixel >> 8),
                                    static_cast<std::uint8_t>(pixel >> 16),
                                    static_cast<std::uint8_t>(pixel >> 24)};
            if (std::fwrite(bgra, 1, 4, file) != 4) {
                ok = false;
                break;
            }
        }
    }
    std::fclose(file);
    return ok;
}

}  // namespace

int App::render_to(const std::string& path, float scale, const std::string& view,
                   int window_width, int window_height) {
    rendering_ = true;
    headless_ = true;
    settings_ = Settings::load();
    tools_ = check_tools();
    if (auto root = Project::locate()) project_.emplace(*root, settings_.game);
    if (project_) settings_.game = project_->game();
    status_ = project_ ? project_->status() : ProjectStatus{};
    append_log(L"[launcher] render check");

    std::wstring error;
    if (!painter_.initialise(nullptr, &error)) {
        std::fprintf(stderr, "%s\n", narrow(error).c_str());
        return 1;
    }
    float factor = scale > 0.1f ? scale : 1.0f;
    // The view stands in for a window of this many pixels at 1×, zoom and all, so the
    // layout can be checked at any window size; `factor` supersamples the whole render.
    int view_w = window_width > 0 ? window_width : static_cast<int>(kDesignWidth);
    int view_h = window_height > 0 ? window_height : static_cast<int>(kDesignHeight);
    float zoom = ui_zoom_for(view_w, view_h, 1.0f);
    painter_.resize(static_cast<int>(std::lround(view_w * factor)),
                    static_cast<int>(std::lround(view_h * factor)), zoom * factor);
    width_ = static_cast<float>(view_w) / zoom;
    height_ = static_cast<float>(view_h) / zoom;

    // Exercises hit testing and the click path without a window: paint once to build the
    // hit list, click the point, then paint the result.
    // Points are separated by ';', so a render can open a menu and then pick from it.
    auto click = [this](const std::string& points) {
        size_t start = 0;
        while (start <= points.size()) {
            size_t end = points.find(';', start);
            if (end == std::string::npos) end = points.size();
            float x = 0, y = 0;
            if (std::sscanf(points.substr(start, end - start).c_str(), "%f,%f", &x, &y) != 2)
                return false;
            paint();
            SDL_Event event{};
            event.type = SDL_MOUSEBUTTONDOWN;
            event.button.button = SDL_BUTTON_LEFT;
            event.button.x = static_cast<Sint32>(x);
            event.button.y = static_cast<Sint32>(y);
            handle_event(event);
            event.type = SDL_MOUSEBUTTONUP;
            handle_event(event);
            start = end + 1;
        }
        return true;
    };

    // Clicks the control with this id wherever the layout put it. A point written into a
    // test stops testing the control the moment the layout moves, which is what happened
    // to the click that was meant to open the details.
    auto click_control = [&](Id id, int index = 0) {
        paint();
        int seen = 0;
        Rect target{};
        bool found = false;
        for (const auto& hit : ui_.hits()) {
            if (hit.id != id || seen++ != index) continue;
            target = hit.rect;
            found = true;
            break;
        }
        if (!found) {
            std::fprintf(stderr, "click-control: the page has no control %d\n",
                         static_cast<int>(id));
            return false;
        }
        char point[64] = {};
        std::snprintf(point, sizeof(point), "%d,%d",
                      static_cast<int>(target.x + target.w / 2),
                      static_cast<int>(target.y + target.h / 2));
        return click(point);
    };

    // A stand-in release, so the update prompt and the header button can be rendered
    // without a network.
    auto fake_update = [this] {
        update_state_ = UpdateState::Available;
        update_release_.version = L"1.1.0";
        update_release_.page_url = L"https://example.invalid/releases/tag/v1.1.0";
        update_release_.asset_name = L"NarutoLauncher-macOS-arm64-v1.1.0.zip";
        update_release_.asset_url = L"https://example.invalid/" + update_release_.asset_name;
    };

    if (view == "settings") {
        open_settings();
    } else if (view == "players") {
        open_players();
    } else if (view.rfind("players-click:", 0) == 0) {
        open_players();
        if (!click(view.substr(14))) {
            std::fprintf(stderr, "usage: players-click:<x>,<y>\n");
            return 1;
        }
    } else if (view == "mods") {
        open_mods();
    } else if (view.rfind("mods-click:", 0) == 0) {
        open_mods();
        if (!click(view.substr(11))) {
            std::fprintf(stderr, "usage: mods-click:<x>,<y>\n");
            return 1;
        }
    } else if (view == "mods-from-game") {
        // The Mods button belongs to the game's page: it opens the panel for the game that
        // is on the page, and Escape closes it back there rather than to Settings. Two
        // games, so a panel that ignores which game is selected cannot pass.
        fs::path scratch = fs::path(path);
        scratch.replace_extension(L".mods.project");
        std::error_code ec;
        fs::create_directories(scratch, ec);
        for (const wchar_t* game : {L"naruto", L"gnt4"}) {
            project_.emplace(scratch, game);
            // A disc stands extracted, or the page would not offer the button at all.
            status_ = ProjectStatus{};
            status_.disc.extracted = true;
            paint();
            Rect mods{};
            bool found = false;
            for (const auto& hit : ui_.hits()) {
                if (hit.id == Id::Mods) {
                    mods = hit.rect;
                    found = true;
                    break;
                }
            }
            if (!found) {
                std::fprintf(stderr, "mods-from-game: the page has no Mods button\n");
                return 1;
            }
            char point[64] = {};
            std::snprintf(point, sizeof(point), "%d,%d",
                          static_cast<int>(mods.x + mods.w / 2),
                          static_cast<int>(mods.y + mods.h / 2));
            if (!click(point)) return 1;
            const std::wstring wanted = L"Mods — " + Project::game_info(game).title;
            if (!overlay_.is(OverlayPage::Mods) || settings_open_ ||
                overlay_.title() != wanted) {
                std::fprintf(stderr, "mods-from-game: %s\n",
                             narrow(overlay_.is(OverlayPage::Mods) ? L"the mods panel says \"" +
                                                        overlay_.title() + L"\""
                                                  : L"the Mods button opened no panel")
                                 .c_str());
                return 1;
            }
            SDL_Event escape{};
            escape.type = SDL_KEYDOWN;
            escape.key.keysym.sym = SDLK_ESCAPE;
            handle_event(escape);
            if (overlay_.open() || settings_open_) {
                std::fprintf(stderr,
                             "mods-from-game: Escape did not return to the game\n");
                return 1;
            }
        }
    } else if (view == "geckos") {
        open_geckos();
    } else if (view == "geckos-paste") {
        // The box is clicked and then handed text the way the window hands it over, so the
        // path being checked is the one a paste takes, not a shortcut into the buffer.
        open_geckos();
        paint();
        Rect box{};
        for (const auto& hit : ui_.hits()) {
            if (hit.id != Id::CodeBox) continue;
            box = hit.rect;
            break;
        }
        if (box.w <= 0) {
            std::fprintf(stderr, "geckos-paste: the panel has no code box\n");
            return 1;
        }
        char point[64] = {};
        std::snprintf(point, sizeof(point), "%d,%d", static_cast<int>(box.x + box.w / 2),
                      static_cast<int>(box.y + box.h / 2));
        if (!click(point)) return 1;
        if (!gecko_editing_) {
            std::fprintf(stderr, "geckos-paste: the box did not take the click\n");
            return 1;
        }
        for (const char* chunk : {"$Infinite Time\n", "040374B8 60000000\n"}) {
            SDL_Event text{};
            text.type = SDL_TEXTINPUT;
            std::snprintf(text.text.text, sizeof(text.text.text), "%s", chunk);
            handle_event(text);
        }
    } else if (view.rfind("geckos-click:", 0) == 0) {
        open_geckos();
        if (!click(view.substr(13))) {
            std::fprintf(stderr, "usage: geckos-click:<x>,<y>\n");
            return 1;
        }
    } else if (view == "geckos-from-game") {
        // The Geckos button belongs to the game's page the way Mods does: it opens the panel
        // for the game that is on the page, and Escape closes it back there rather than to
        // Settings. Two games, so a panel that ignores which game is selected cannot pass.
        fs::path scratch = fs::path(path);
        scratch.replace_extension(L".geckos.project");
        std::error_code ec;
        fs::create_directories(scratch, ec);
        for (const wchar_t* game : {L"naruto", L"gnt4"}) {
            project_.emplace(scratch, game);
            // A disc stands extracted, or the page would not offer the button at all.
            status_ = ProjectStatus{};
            status_.disc.extracted = true;
            paint();
            Rect geckos{};
            bool found = false;
            for (const auto& hit : ui_.hits()) {
                if (hit.id == Id::Geckos) {
                    geckos = hit.rect;
                    found = true;
                    break;
                }
            }
            if (!found) {
                std::fprintf(stderr, "geckos-from-game: the page has no Geckos button\n");
                return 1;
            }
            char point[64] = {};
            std::snprintf(point, sizeof(point), "%d,%d",
                          static_cast<int>(geckos.x + geckos.w / 2),
                          static_cast<int>(geckos.y + geckos.h / 2));
            if (!click(point)) return 1;
            const std::wstring wanted = L"Geckos \u2014 " + Project::game_info(game).title;
            if (!overlay_.is(OverlayPage::Geckos) || settings_open_ ||
                overlay_.title() != wanted) {
                std::fprintf(stderr, "geckos-from-game: %s\n",
                             narrow(overlay_.is(OverlayPage::Geckos)
                                        ? L"the geckos panel says \"" + overlay_.title() + L"\""
                                        : L"the Geckos button opened no panel")
                                 .c_str());
                return 1;
            }
            SDL_Event escape{};
            escape.type = SDL_KEYDOWN;
            escape.key.keysym.sym = SDLK_ESCAPE;
            handle_event(escape);
            if (overlay_.open() || settings_open_) {
                std::fprintf(stderr,
                             "geckos-from-game: Escape did not return to the game\n");
                return 1;
            }
        }
    } else if (view.rfind("state:", 0) == 0) {
        // The page's eight states, drawn from the state this render names rather than from
        // what this machine happens to have. A project folder of its own stands beside the
        // render, so nothing here depends on the checkout being set up.
        fs::path scratch = fs::path(path);
        scratch.replace_extension(L".state.project");
        std::error_code ec;
        fs::create_directories(scratch, ec);
        // The scratch folder stands in for a project, and borrows this checkout's box art
        // when there is any, so a frame of the map does not show placeholder tiles.
        const std::optional<fs::path> covers =
            project_ ? std::optional<fs::path>(project_->covers_dir()) : std::nullopt;
        const std::size_t colon = view.find(':', 6);
        const std::string state =
            view.substr(6, colon == std::string::npos ? std::string::npos : colon - 6);
        // A trailing `:details` opens the details, which is the layout that gives the
        // buttons the least room and so the one worth drawing.
        const bool with_details =
            colon != std::string::npos && view.find("details", colon) != std::string::npos;
        const bool known =
            state == "working" || state == "running" || state == "setup" ||
            state == "needs-disc" || state == "wrong-disc" || state == "not-built" ||
            state == "mods-changed" || state == "ready";
        if (!known) {
            std::fprintf(stderr,
                         "usage: state:working|running|setup|needs-disc|wrong-disc|"
                         "not-built|mods-changed|ready[:details]\n");
            return 1;
        }
        project_.emplace(scratch, settings_.game);
        if (covers) {
            std::error_code link_ec;
            fs::create_directory_symlink(*covers, scratch / L"covers", link_ec);
        }
        status_ = ProjectStatus{};
        // A game with its disc extracted stands behind everything but `setup` and
        // `needs-disc`; the two live states are that page with a step in flight.
        const bool installed = state != "setup" && state != "needs-disc";
        status_.disc.extracted = installed;
        // A wrong disc is one that was extracted and did not match.
        status_.disc.verified = installed && state != "wrong-disc";
        status_.built = state == "mods-changed" || state == "ready";
        status_.stale_code = state == "mods-changed";
        if (state == "setup") project_.reset();
        if (state == "working") forced_busy_ = true;
        if (state == "running") forced_running_ = true;
        if (with_details && !click_control(Id::LogToggle)) return 1;
    } else if (view == "scale-menu" || view.rfind("scale-menu:", 0) == 0) {
        open_settings();
        if (!click_control(Id::Scale)) return 1;
    } else if (view == "pad-menu" || view.rfind("pad-menu:", 0) == 0) {
        // The device list: one row's menu, and the last row's, which has to open upwards
        // rather than off the bottom of the panel.
        const bool last = view.find(":last") != std::string::npos;
        open_players();
        if (!click_control(Id::PadRow, last ? kPads - 1 : 0)) return 1;
    } else if (view == "picker") {
        open_picker(PickerKind::Image, L"Choose your own disc image",
                    project_ ? project_->root() : home_dir(), [](const fs::path&) {});    } else if (view == "update") {
        fake_update();
        show_update_prompt();
    } else if (view == "update-banner") {
        fake_update();
    } else if (view.rfind("update-click:", 0) == 0) {
        fake_update();
        if (!click(view.substr(13))) {
            std::fprintf(stderr, "usage: update-click:<x>,<y>\n");
            return 1;
        }
    } else if (view == "confirm") {
        show_confirm(L"Replace the extracted disc?",
                     L"A disc is already extracted. Extract again from this image and "
                     L"replace it?",
                     L"Extract", [] {});
    } else if (view.rfind("click:", 0) == 0) {
        if (!click(view.substr(6))) {
            std::fprintf(stderr, "usage: click:<x>,<y>\n");
            return 1;
        }
    } else if (view.rfind("settings-click:", 0) == 0) {
        open_settings();
        if (!click(view.substr(15))) {
            std::fprintf(stderr, "usage: settings-click:<x>,<y>\n");
            return 1;
        }
    } else if (view.rfind("cover-click:", 0) == 0) {
        // A cover has to leave the game the page is on for the one clicked. The check runs
        // in a project folder of its own beside the render, which holds no game folder at
        // all, so every game it clicks is one the launcher has not set up yet -- the case
        // that used to land on the first game -- on every machine. Another cover is
        // clicked first, so reaching the one under test is always a real move.
        int index = -1;
        if (std::sscanf(view.c_str() + 12, "%d", &index) != 1) {
            std::fprintf(stderr, "usage: cover-click:<index>\n");
            return 1;
        }
        fs::path scratch = fs::path(path);
        scratch.replace_extension(L".project");
        std::error_code ec;
        fs::create_directories(scratch, ec);
        // The first game is set up, as it is in a project an older launcher made: the game
        // the page falls back to only exists when one does.
        fs::create_directories(scratch / L"naruto", ec);
        write_text_file(scratch / L"naruto" / L"CMakeLists.txt", "# stand-in\n");
        project_.emplace(scratch, settings_.game);
        status_ = project_->status();

        auto games = available_games();
        if (index < 0 || index >= static_cast<int>(games.size())) {
            std::fprintf(stderr, "cover-click: there is no cover %d, 0 to %d\n", index,
                         static_cast<int>(games.size()) - 1);
            return 1;
        }
        const int elsewhere = index + 1 < static_cast<int>(games.size()) ? index + 1 : 0;
        for (const int which : {elsewhere, index}) {
            paint();
            Rect tile{};
            bool found = false;
            for (const auto& hit : ui_.hits()) {
                if (static_cast<int>(hit.id) == static_cast<int>(Id::GameCover) + which) {
                    tile = hit.rect;
                    found = true;
                    break;
                }
            }
            if (!found) {
                std::fprintf(stderr, "cover-click: there is no cover %d on the page\n", which);
                return 1;
            }
            char point[64] = {};
            std::snprintf(point, sizeof(point), "%d,%d",
                          static_cast<int>(tile.x + tile.w / 2),
                          static_cast<int>(tile.y + tile.h / 2));
            if (!click(point)) return 1;
        }
        const auto& wanted = games[static_cast<size_t>(index)];
        if (!project_ || project_->game() != wanted.folder) {
            std::fprintf(stderr, "cover-click: the page is on %s, not %s\n",
                         project_ ? narrow(project_->game()).c_str() : "no game",
                         narrow(wanted.folder).c_str());
            return 1;
        }
    }

    paint();
    return write_bmp(path, painter_.buffer(), painter_.pixel_width(), painter_.pixel_height())
               ? 0
               : 1;
}

// -- painting --------------------------------------------------------------

void App::paint() {
    if (!painter_.begin()) return;
    painter_.clear(rgb(theme::Background));
    ui_.begin_frame();

    paint_header();
    paint_games();

    if (settings_open_) {
        paint_settings();
    } else if (overlay_.open()) {
        paint_overlay();
    }
    if (picker_ != PickerKind::None) paint_picker();
    if (scale_open_) paint_scale_menu();
    if (device_map_.menu_row() >= 0) paint_pad_menu();

    painter_.end();
}

void App::paint_header() {
    Rect logo{24, 18, 48, 48};
    if (painter_.has_logo()) {
        painter_.draw_logo(logo.box());
    } else {
        painter_.fill_round(logo.box(), 12, rgb(theme::Accent));
        painter_.text_clipped(L"N", {logo.x, logo.y + 1, logo.w, logo.h}, Face::Title,
                              rgb(0xffffff), TextAlign::Center);
    }

    Rect settings_button{width_ - 24 - 104, 24, 104, 34};
    ui_.button(Id::OpenSettings, settings_button, L"Settings", false, true);

    // The games are the big covers below, so the header only carries the title of the
    // one they would launch, and what the launcher found about it.
    float title_right = settings_button.x - 16;
    if (update_state_ == UpdateState::Available && !update_release_.version.empty()) {
        std::wstring label = L"Update " + update_release_.version;
        float badge_w = ui_.text_width(label, Face::Button) + 30;
        Rect badge{settings_button.x - 8 - badge_w, 24, badge_w, 34};
        ui_.button(Id::UpdateBanner, badge, label, true, !busy_);
        title_right = std::min(title_right, badge.x - 16);
    }

    painter_.text(current_title(), RectF(82, 14, title_right, 46), Face::Title,
                  rgb(theme::Text));
    painter_.text(kSubtitle, RectF(83, 48, title_right, 68), Face::Subtitle,
                  rgb(theme::Muted));

    painter_.line(24, 86, width_ - 24, 86, rgb(theme::Border));
}

void App::paint_games() {
    constexpr float kHeaderBottom = 86.0f;
    auto games = available_games();
    bool compact = log_visible_;
    // The one step in flight, and the game that is up: a render names them instead of
    // finding them, so a state that no machine happens to be in can still be drawn.
    const bool busy = busy_ || forced_busy_;
    const bool running = forced_running_ || game_.running();

    std::wstring title;
    std::wstring detail;
    std::wstring pill_text;
    Pill pill_state = Pill::Warn;

    if (busy) {
        title = activity_.stage().empty() ? L"Working…" : activity_.stage();
        detail = L"The launcher is running this step. Details holds its output.";
        pill_text = L"working";
        pill_state = Pill::Muted;
    } else if (running) {
        title = L"The game is running";
        detail = L"It opens in its own window. Close that window, or stop it here.";
        pill_text = L"running";
        pill_state = Pill::Ok;
    } else if (!project_) {
        title = L"Set up your game folder";
        detail = L"Pick a folder and the launcher writes the game folders into it, then "
                 L"extracts your disc. It remembers the choice.";
        pill_text = L"setup";
        pill_state = Pill::Warn;
    } else if (!status_.disc.extracted) {
        title = L"Choose your disc image";
        detail = L"Click a cover and pick your own disc image. The launcher extracts the "                 L"files the build and runtime read; nothing from the disc is included in "
                 L"the launcher.";
        pill_text = L"needs disc";
        pill_state = Pill::Warn;
    } else if (!status_.disc.verified) {
        title = L"This is not the supported disc";
        detail = L"The extracted main.dol does not match the supported USA release. Click "
                 L"the cover and pick the correct image.";
        pill_text = L"wrong disc";
        pill_state = Pill::Bad;
    } else if (!status_.built) {
        title = L"Ready to build";
        detail = L"A cover builds that game first, then starts it. A first build takes a "
                 L"few minutes.";
        pill_text = L"not built";
        pill_state = Pill::Warn;
    } else if (status_.stale_code) {
        title = L"Ready to rebuild";
        detail = L"A mod's codes changed, and they are compiled into the game: a cover "
                 L"builds it again, then starts it.";
        pill_text = L"mods changed";
        pill_state = Pill::Warn;
    } else {
        title = L"Ready to play";
        detail = L"Click a cover to choose the game, then Play. Escape quits in the game.";
        pill_text = L"ready";
        pill_state = Pill::Ok;
    }

    // The step in flight, or the game that is up, and the way back to the log.
    auto progress_bar = [this](const Rect& bar) {
        painter_.fill_round(bar.box(), 3, rgb(0x242932));
        if (activity_.percent() >= 0) {
            float fraction = std::min(1.0f, static_cast<float>(activity_.percent()) / 100.0f);
            painter_.fill_round(
                RectF(bar.x, bar.y, bar.x + bar.w * fraction, bar.y + bar.h), 3,
                rgb(theme::Accent));
        } else {
            float chunk = bar.w * 0.28f;
            float travel = bar.w + chunk;
            float start = bar.x - chunk + travel * progress_phase_;
            float left = std::max(bar.x, start);
            float right_edge = std::min(bar.x + bar.w, start + chunk);
            if (right_edge > left) {
                painter_.fill_round(RectF(left, bar.y, right_edge, bar.y + bar.h), 3,
                                    rgb(theme::Accent));
            }
        }
    };
    // What the page offers once a game is picked: one button to start it, and one for each
    // way of changing what it plays -- the mods, and the Gecko codes chosen on their own. A
    // cover only chooses the game, so nothing starts by accident.
    //
    // Play asks for a disc when there is no verified tree, which is what launch() does: the
    // button says so rather than starting and then asking.
    const std::wstring play_label =
        (!project_ || !status_.disc.verified) ? L"Set up the disc\u2026" : L"Play";
    // The mods and the codes change what an extracted disc plays. Before there is one there
    // is nothing to change, so the page offers Play on its own.
    const bool changeable = project_ != std::nullopt && status_.disc.extracted;

    constexpr float kButtonGap = 12.0f;
    constexpr float kButtonHeight = 50.0f;
    constexpr float kIconButton = 50.0f;
    constexpr float kButtonPad = 12.0f;
    constexpr float kLoneWidth = 240.0f;

    struct ButtonPlan {
        float play_width;
        float other_width;  // a Mods or Geckos button; 0 when the page offers neither
        bool icons;         // the two are square marks rather than words
    };
    // A wide page puts the three side by side as words. A narrow column cannot hold
    // "Set up the disc…" three times over -- drawn that way it clipped at both ends, "et up
    // the disc." -- so Play keeps its label at the width left over and the other two become
    // square marks. With no disc there is only Play, and it takes a wider button.
    auto plan_buttons = [&](float width) -> ButtonPlan {
        if (!changeable) return {std::min(kLoneWidth, width), 0.0f, false};
        const float row_w = std::min(170.0f, (width - kButtonGap * 2.0f) / 3.0f);
        const bool fits = ui_.elide(play_label, Face::Button, row_w - kButtonPad) == play_label &&
                          ui_.elide(L"Geckos", Face::Button, row_w - kButtonPad) == L"Geckos";
        if (fits) return {row_w, row_w, false};
        return {std::max(kIconButton, width - 2.0f * (kIconButton + kButtonGap)),
                kIconButton, true};
    };

    // The two marks a narrow column puts beside Play: the mods table, and the brackets a
    // block of codes sits between.
    auto mods_mark = [this](const Rect& rect, Color color) {
        const float cx = rect.x + rect.w / 2.0f;
        const float cy = rect.y + rect.h / 2.0f;
        painter_.stroke_round(RectF(cx - 10, cy - 9, cx + 10, cy + 9), 4, color, 1.6f);
        for (int line = 0; line < 3; ++line) {
            const float y = cy - 4.5f + static_cast<float>(line) * 4.5f;
            painter_.line(cx - 5.5f, y, cx + 5.5f, y, color, 1.6f);
        }
    };
    auto codes_mark = [this](const Rect& rect, Color color) {
        const float cx = rect.x + rect.w / 2.0f;
        const float cy = rect.y + rect.h / 2.0f;
        painter_.line(cx - 3.0f, cy - 8.0f, cx - 10.0f, cy, color, 1.8f);
        painter_.line(cx - 10.0f, cy, cx - 3.0f, cy + 8.0f, color, 1.8f);
        painter_.line(cx + 3.0f, cy - 8.0f, cx + 10.0f, cy, color, 1.8f);
        painter_.line(cx + 10.0f, cy, cx + 3.0f, cy + 8.0f, color, 1.8f);
    };

    auto draw_game_buttons = [&](float center_x, float top, float width = 480.0f) {
        const bool waiting = busy || running;
        const ButtonPlan plan = plan_buttons(width);
        // A narrow button drops to the smaller face rather than crowding its label.
        auto face_for = [this, &plan](const std::wstring& label) {
            return ui_.text_width(label, Face::ButtonLarge) <= plan.play_width - kButtonPad
                       ? Face::ButtonLarge
                       : Face::Button;
        };
        const float total = plan.other_width <= 0
                                ? plan.play_width
                                : plan.play_width + 2.0f * (plan.other_width + kButtonGap);
        const float left = center_x - total / 2.0f;
        ui_.button(Id::Play, Rect{left, top, plan.play_width, kButtonHeight}, play_label, true,
               !waiting, false, face_for(play_label));
        if (plan.other_width <= 0) return;
        const bool ready = !waiting;
        const float x = left + plan.play_width + kButtonGap;
        if (plan.icons) {
            ui_.button(Id::Mods, Rect{x, top, plan.other_width, kButtonHeight}, L"", false, ready,
                   false, Face::Button, mods_mark);
            ui_.button(Id::Geckos,
                   Rect{x + plan.other_width + kButtonGap, top, plan.other_width,
                        kButtonHeight},
                   L"", false, ready, false, Face::Button, codes_mark);
        } else {
            ui_.button(Id::Mods, Rect{x, top, plan.other_width, kButtonHeight}, L"Mods", false,
                   ready, false, Face::Button);
            ui_.button(Id::Geckos,
                   Rect{x + plan.other_width + kButtonGap, top, plan.other_width,
                        kButtonHeight},
                   L"Geckos", false, ready, false, Face::Button);
        }
    };

    auto draw_controls = [&](float center_x, float top, float width) {
        float bottom = top;
        if (busy) {
            progress_bar(Rect{center_x - width / 2, top + 4, width, 6});
            if (job_.running()) {
                float cancel_w = ui_.text_width(L"Cancel", Face::Tiny);
                ui_.link(Id::Stop, Rect{center_x - cancel_w / 2, top + 20, cancel_w, 18},
                     L"Cancel");
            }
            bottom = top + 44;
        } else if (running) {
            ui_.button(Id::Stop, Rect{center_x - 110, top, 220, 40}, L"Stop the game", true, true,
                   true);
            bottom = top + 46;
        }
        if (!compact) {
            std::wstring toggle = L"Show details";
            float toggle_w = ui_.text_width(toggle, Face::Tiny);
            ui_.link(Id::LogToggle, Rect{center_x - toggle_w / 2, bottom + 4, toggle_w, 18},
                 toggle);
            bottom += 24;
        }
        if (!play_status_.empty() && !busy) {
            // The note stays inside the column it belongs to: a narrow column would
            // otherwise print it across the covers.
            float note_w = std::min(660.0f, width);
            painter_.text(play_status_,
                          RectF(center_x - note_w / 2, bottom + 2, center_x + note_w / 2,
                                bottom + 20),
                          Face::Tiny, rgb(theme::Muted), TextAlign::Center);
        }
    };

    if (games.empty()) {
        constexpr float kBlockHeight = 214.0f;
        float center_x = width_ / 2;
        float top =
            kHeaderBottom + std::max(24.0f, (height_ - kHeaderBottom - kBlockHeight) * 0.5f);
        float pill_w = ui_.text_width(pill_text, Face::Small) + 32;
        ui_.pill(Rect{center_x - pill_w / 2, top, pill_w, 22}, pill_text, pill_state);
        painter_.text(title, RectF(center_x - 380, top + 26, center_x + 380, top + 78),
                      Face::Hero, rgb(theme::Text), TextAlign::Center);
        painter_.text(detail, RectF(center_x - 340, top + 82, center_x + 340, top + 136),
                      Face::Body, rgb(theme::Muted), TextAlign::Center);
        ui_.button(Id::Launch, Rect{center_x - 130, top + 152, 260, 50}, L"Choose a folder",
               true, !busy, false, Face::ButtonLarge);
        if (!play_status_.empty()) {
            painter_.text(play_status_,
                          RectF(center_x - 330, top + 210, center_x + 330, top + 228),
                          Face::Tiny, rgb(theme::Muted), TextAlign::Center);
        }
        return;
    }

    // Each game is a box-art sized cover, `count` of them across the page.
    const int count = static_cast<int>(games.size());
    const float gap = compact ? 20.0f : 32.0f;
    const float label_height = compact ? 22.0f : 28.0f;
    const float status_height = compact ? 0.0f : 172.0f;

    // With the details open the covers share the row with them: the details keep a column
    // of their own against the right edge and the covers take what is left of the row, so
    // the pill, the text and the buttons all stay inside the column.
    const float status_w = compact ? std::clamp(width_ * 0.32f, 300.0f, 380.0f) : 0.0f;
    const float status_left = compact ? width_ - 24.0f - status_w : 0.0f;

    float row_right = compact ? status_left - 28.0f : width_ - 72.0f;
    float max_tile_width = (row_right - 24.0f - gap * (count - 1)) / count;
    float max_tile_height = height_ - kHeaderBottom - 48.0f - label_height - status_height;
    if (compact) max_tile_height = std::min(max_tile_height, 216.0f);
    max_tile_height = std::max(160.0f, std::min(max_tile_height, 470.0f));
    float tile_height = max_tile_height;
    float tile_width = tile_height * 3.0f / 4.0f;
    if (tile_width > max_tile_width) {
        tile_width = max_tile_width;
        tile_height = tile_width * 4.0f / 3.0f;
    }
    float row_width = count * tile_width + (count - 1) * gap;

    float left = compact ? 24.0f : (width_ - row_width) / 2.0f;
    float top;
    if (compact) {
        top = kHeaderBottom + 24.0f;
    } else {
        float block_height = tile_height + label_height + 22.0f + status_height;
        top = kHeaderBottom +
              std::max(16.0f, (height_ - kHeaderBottom - 24.0f - block_height) * 0.5f);
    }

    bool can_launch = !busy && !running;
    float x = left;
    for (int index = 0; index < count; ++index) {
        const auto& game = games[static_cast<size_t>(index)];
        bool selected = project_ && project_->game() == game.folder;
        Rect tile{x, top, tile_width, tile_height};
        paint_cover(static_cast<Id>(static_cast<int>(Id::GameCover) + index), tile, game,
                    selected, can_launch);
        painter_.text(ui_.elide(game.short_title, Face::Button, tile_width - 6.0f),
                      RectF(tile.x, tile.y + tile.h + 4, tile.x + tile.w,
                            tile.y + tile.h + 4 + label_height),
                      Face::Button, selected ? rgb(theme::Accent) : rgb(theme::Muted),
                      TextAlign::Center, TextVAlign::Center, false);
        x += tile_width + gap;
    }

    if (compact) {
        const float status_right = width_ - 24.0f;
        const float column_width = status_right - status_left;
        const float column_center = (status_left + status_right) / 2.0f;
        const float pill_h = 22.0f;
        const float title_line = painter_.line_height(Face::CardTitle);
        const float body_line = painter_.line_height(Face::Body);
        const float buttons_h = kButtonHeight;
        // The step's controls sit under the buttons: a progress bar with Cancel, or the
        // note the last launch left behind.
        const float controls_h = busy ? 44.0f
                                : running ? 46.0f
                                : play_status_.empty() ? 0.0f
                                                       : 20.0f;

        const float title_h =
            static_cast<float>(wrapped_lines(painter_, title, Face::CardTitle, column_width)) *
            title_line;
        const float detail_h =
            static_cast<float>(wrapped_lines(painter_, detail, Face::Body, column_width)) *
            body_line;
        // The column is as tall as its own text, so the covers beside it can be short
        // without the buttons, the text and the log card running into each other.
        const float column_h = 2.0f + pill_h + 8.0f + title_h + 6.0f + detail_h + 12.0f +
                               buttons_h + (controls_h > 0 ? 10.0f + controls_h : 0.0f);
        const float block_h = std::max(tile_height + label_height, column_h);

        float y = top + 2.0f;
        float pill_w = std::min(ui_.text_width(pill_text, Face::Small) + 32, column_width);
        ui_.pill(Rect{status_left, y, pill_w, pill_h}, pill_text, pill_state);
        y += pill_h + 8.0f;
        painter_.text(title, RectF(status_left, y, status_right, y + title_h),
                      Face::CardTitle, rgb(theme::Text));
        y += title_h + 6.0f;
        painter_.text(detail, RectF(status_left, y, status_right, y + detail_h), Face::Body,
                      rgb(theme::Muted));
        y += detail_h + 12.0f;
        draw_game_buttons(column_center, y, column_width);
        y += buttons_h;
        if (controls_h > 0) {
            draw_controls(column_center, y + 10.0f, std::min(420.0f, column_width));
        }
        paint_activity(top + block_h + 26.0f);
        return;
    }

    float status_top = top + tile_height + label_height + 22.0f;
    float center_x = width_ / 2;
    float pill_w = ui_.text_width(pill_text, Face::Small) + 32;
    ui_.pill(Rect{center_x - pill_w / 2, status_top, pill_w, 22}, pill_text, pill_state);
    painter_.text(title,
                  RectF(center_x - 380, status_top + 26, center_x + 380, status_top + 74),
                  Face::Hero, rgb(theme::Text), TextAlign::Center);
    painter_.text(detail,
                  RectF(center_x - 350, status_top + 78, center_x + 350, status_top + 124),
                  Face::Body, rgb(theme::Muted), TextAlign::Center);
    draw_game_buttons(center_x, status_top + 130);
    draw_controls(center_x, status_top + 130 + kButtonHeight + 12.0f, 420.0f);
}

void App::paint_cover(Id id, const Rect& rect, const GameInfo& game, bool selected,
                      bool enabled) {
    bool hovered = enabled && ui_.hover() == id;
    bool down = enabled && ui_.pressed() == id;
    constexpr float kRadius = 14.0f;
    RectF box = rect.box();

    painter_.fill_round(RectF(box.left, box.top + 3, box.right, box.bottom + 5), kRadius,
                        rgb(0x000000, 0.32f));

    const Cover* art = cover(game.folder);
    if (art && art->loaded) {
        painter_.draw_image(box, art->pixels.data(), art->width, art->height, kRadius);
    } else {
        // Without art of its own, the tile still names the game and says where the cover
        // goes, so the page works on a fresh checkout.
        const Cover& fallback = placeholder_cover();
        painter_.draw_image(box, fallback.pixels.data(), fallback.width, fallback.height,
                            kRadius);
        painter_.text(game.short_title,
                      RectF(box.left + 14, box.top + box.height() * 0.62f, box.right - 14,
                            box.top + box.height() * 0.86f),
                      Face::CardTitle, rgb(theme::Text), TextAlign::Center);
        painter_.text(L"Drop " + game.folder + L".png in covers/",
                      RectF(box.left + 10, box.bottom - 34, box.right - 10, box.bottom - 14),
                      Face::Tiny, rgb(theme::Muted), TextAlign::Center);
    }

    if (hovered) painter_.fill_round(box, kRadius, rgb(0xffffff, down ? 0.10f : 0.06f));
    if (!enabled) painter_.fill_round(box, kRadius, rgb(theme::Background, 0.55f));

    Color border = !enabled ? rgb(theme::Border)
                            : (selected ? rgb(theme::Accent)
                                        : (hovered ? rgb(theme::BorderStrong)
                                                   : rgb(theme::Border)));
    if (selected) painter_.stroke_round(box, kRadius, rgb(theme::Accent, 0.30f), 6.0f);
    painter_.stroke_round(box, kRadius, border, selected ? 2.0f : 1.0f);
    if (enabled) ui_.register_hit(id, rect);
}

void App::paint_activity(float top) {
    float card_height = std::max(140.0f, height_ - top - 24.0f);
    Rect card{24, top, width_ - 48, card_height};
    ui_.card_box(card);

    painter_.text(L"Activity", RectF(card.x + 20, card.y + 16, card.x + 200,
                                           card.y + 40),
                  Face::CardTitle, rgb(theme::Text));

    std::wstring toggle = log_visible_ ? L"Hide" : L"Show";
    float clear_w = ui_.text_width(L"Clear", Face::Tiny);
    float copy_w = ui_.text_width(L"Copy", Face::Tiny);
    float toggle_w = ui_.text_width(toggle, Face::Tiny);
    float right = card.x + card.w - 20;
    ui_.link(Id::LogToggle, Rect{right - toggle_w, card.y + 18, toggle_w, 18}, toggle);
    right -= toggle_w + 16;
    ui_.link(Id::CopyLog, Rect{right - copy_w, card.y + 18, copy_w, 18}, L"Copy");
    right -= copy_w + 16;
    ui_.link(Id::ClearLog, Rect{right - clear_w, card.y + 18, clear_w, 18}, L"Clear");

    float content_top = card.y + 44;
    float content_bottom = card.y + card.h - 14;
    float reserved = busy_ ? 34.0f : 0.0f;

    log_rect_ = {};
    if (log_visible_) {
        Rect log_area{card.x + 20, content_top, card.w - 40,
                      std::max(40.0f, content_bottom - reserved - content_top)};
        log_rect_ = log_area;
        painter_.fill_round(log_area.box(), 10, rgb(theme::Log));
        painter_.stroke_round(log_area.box(), 10, rgb(0x242932), 1);
        if (activity_.empty()) {
            painter_.text(L"Setup and build output appears here.",
                          RectF(log_area.x + 12, log_area.y + 8, log_area.x + log_area.w - 12,
                                      log_area.y + 28),
                          Face::Mono, rgb(0x646d7d));
        } else {
            const float line_height = painter_.line_height(Face::Mono);
            int visible = static_cast<int>((log_area.h - 16) / line_height);
            visible = std::max(1, visible);
            int total = static_cast<int>(activity_.lines().size());
            int max_offset = std::max(0, total - visible);
            log_offset_ = std::min(log_offset_, max_offset);
            int end = total - log_offset_;
            int start = std::max(0, end - visible);
            for (int index = start; index < end; ++index) {
                float y = log_area.y + 8 + (index - start) * line_height;
                Color color = rgb(0xc8cede);
                const auto& line = activity_.lines()[static_cast<size_t>(index)];
                if (line.rfind(L"$ ", 0) == 0) color = rgb(0xe0a084);
                if (line.rfind(L"[launcher]", 0) == 0) color = rgb(theme::Muted);
                painter_.text_clipped(line,
                                      RectF(log_area.x + 12, y, log_area.x + log_area.w - 12,
                                                  y + line_height + 2),
                                      Face::Mono, color);
            }
        }
    }

    if (busy_) {
        float bottom = card.y + card.h - 14;
        float bar_y = bottom - 8;
        std::wstring label = activity_.stage().empty() ? L"Working…" : activity_.stage();
        painter_.text(label, RectF(card.x + 20, bottom - 30, card.x + card.w - 160,
                                         bottom - 10),
                      Face::Tiny, rgb(theme::Muted));
        Rect cancel{card.x + card.w - 20 - 86, bottom - 32, 86, 24};
        if (job_.running()) {
            painter_.text(L"Cancel", RectF(cancel.x, cancel.y, cancel.x + cancel.w,
                                                 cancel.y + cancel.h),
                          Face::Tiny, rgb(theme::Link), TextAlign::Center,
                          TextVAlign::Center);
            ui_.register_hit(Id::Stop, cancel);
        }

        Rect bar{card.x + 20, bar_y, card.w - 40, 6};
        painter_.fill_round(bar.box(), 3, rgb(0x242932));
        if (activity_.percent() >= 0) {
            float fraction = std::min(1.0f, static_cast<float>(activity_.percent()) / 100.0f);
            painter_.fill_round(RectF(bar.x, bar.y, bar.x + bar.w * fraction,
                                            bar.y + bar.h),
                                3, rgb(theme::Accent));
        } else {
            float chunk = bar.w * 0.28f;
            float travel = bar.w + chunk;
            float start = bar.x - chunk + travel * progress_phase_;
            float left = std::max(bar.x, start);
            float right_edge = std::min(bar.x + bar.w, start + chunk);
            if (right_edge > left) {
                painter_.fill_round(RectF(left, bar.y, right_edge, bar.y + bar.h), 3,
                                    rgb(theme::Accent));
            }
        }
    }
}

void App::paint_settings() {
    painter_.fill_rect(RectF(0, 0, width_, height_), rgb(0x000000, 0.62f));
    float panel_w = std::min(720.0f, width_ - 48);
    float panel_h = std::min(340.0f, height_ - 40);
    Rect panel{(width_ - panel_w) / 2, (height_ - panel_h) / 2, panel_w, panel_h};
    painter_.fill_round(panel.box(), 16, rgb(0x171a21));
    painter_.stroke_round(panel.box(), 16, rgb(theme::BorderStrong), 1);

    painter_.text(L"Settings", RectF(panel.x + 26, panel.y + 18, panel.x + 300,
                                            panel.y + 48),
                  Face::CardTitle, rgb(theme::Text));
    ui_.link(Id::SettingsClose, Rect{panel.x + panel.w - 26 - 40, panel.y + 20, 40, 20}, L"Close");

    float left = panel.x + 26;
    float content_w = panel.w - 52;
    float right = panel.x + panel.w - 26;
    float y = panel.y + 56;

    // The sections are laid out tight enough that the whole panel fits below the header
    // at the window's smallest size.
    auto section = [&](const std::wstring& text) {
        painter_.text(text, RectF(left, y, left + content_w, y + 18), Face::Tiny,
                      rgb(theme::Accent));
        y += 18;
    };
    auto separator = [&] {
        painter_.line(left, y, left + content_w, y, rgb(theme::Border));
        y += 10;
    };

    section(L"CONTROLLERS");
    painter_.text(ui_.elide(L"Which controller plays as which player.", Face::Small,
                        content_w - 130),
                  RectF(left, y + 7, left + content_w - 130, y + 29), Face::Small,
                  rgb(theme::Muted));
    ui_.button(Id::SettingsPlayers, Rect{right - 110, y, 110, 32}, L"Players…", false, !busy_);
    y += 36;
    separator();

    section(L"RESOLUTION");
    painter_.text(L"Window scale", RectF(left, y + 8, left + 96, y + 28), Face::Small,
                  rgb(theme::Muted));
    ui_.combo(Id::Scale, Rect{left + 96, y, 180, 32}, scale_label(settings_.scale), !busy_);
    y += 36;
    separator();

    section(L"UPDATES");
    std::wstring update_line = L"Version " + launcher_version() + L".";
    Color update_color = rgb(theme::Muted);
    switch (update_state_) {
        case UpdateState::Checking:
            update_line = L"Asking GitHub for the newest release…";
            break;
        case UpdateState::UpToDate:
            update_line = L"Version " + launcher_version() + L" is the newest release.";
            update_color = rgb(theme::Ok);
            break;
        case UpdateState::Available:
            update_line = L"Version " + update_release_.version + L" is available.";
            update_color = rgb(theme::Accent);
            break;
        case UpdateState::Failed:
            update_line = L"Version " + launcher_version() + L". " + update_error_;
            update_color = rgb(theme::Warn);
            break;
        case UpdateState::Never:
            break;
    }
    painter_.text(ui_.elide(update_line, Face::Small, content_w - 250),
                  RectF(left, y + 7, left + content_w - 250, y + 29), Face::Small,
                  update_color);
    ui_.button(Id::SettingsCheckNow, Rect{right - 100, y, 100, 32}, L"Check now", false,
           !busy_ && update_state_ != UpdateState::Checking);
    if (update_state_ == UpdateState::Available) {
        ui_.button(Id::SettingsInstallUpdate, Rect{right - 100 - 8 - 128, y, 128, 32},
               L"Install update", true, !busy_);
    }
    y += 36;
    ui_.checkbox(Id::SettingsToggleUpdates, left, y + 4, L"Check when the launcher starts",
             settings_.check_updates, !busy_);
    y += 24;

    if (!settings_note_.empty()) {
        painter_.text(settings_note_, RectF(left, y, left + content_w, y + 22),
                      Face::Tiny, rgb(theme::Ok));
    }

    ui_.button(Id::SettingsClose, Rect{right - 110, panel.y + panel.h - 54, 110, 36}, L"Done",
           true, true);
}

void App::paint_overlay() {
    painter_.fill_rect(RectF(0, 0, width_, height_), rgb(0x000000, 0.62f));
    float panel_w = overlay_.is(OverlayPage::Players) ? 660.0f
                    : overlay_.is(OverlayPage::Mods)  ? 720.0f
                    : overlay_.is(OverlayPage::Geckos) ? 760.0f
                                                      : kMessagePanelWidth;
    // How many code rows fit: the fixed part of the Geckos panel is the two lines above the
    // table, the line under it, the paste box and the buttons, and the rest is rows. A
    // window too short for all of them shows fewer rather than a panel that does not fit,
    // because a panel taller than the window loses its title and its last rows.
    if (overlay_.is(OverlayPage::Geckos)) {
        constexpr float kChrome = 150.0f + 26.0f + 66.0f + 96.0f;
        constexpr float kRow = 22.0f;
        int fit = static_cast<int>((height_ - kChrome) / kRow);
        fit = std::clamp(fit, 3, kCodeRows);
        code_rows_fit_ = static_cast<std::size_t>(fit);
    }
    // A message is as tall as what it says: the fixed 620×336 panel left half of itself
    // empty for a short prompt, and would have cut a long one off at the bottom.
    const float message_lines =
        static_cast<float>(wrapped_lines(painter_, overlay_.text(), Face::Body,
                                         panel_w - 52.0f));
    float panel_h = overlay_.is(OverlayPage::Players) ? 168.0f + 34.0f * kPads
                    : overlay_.is(OverlayPage::Mods)
                        ? 250.0f + 34.0f * static_cast<float>(mods_.rows())
                    : overlay_.is(OverlayPage::Geckos)
                        ? 308.0f + 22.0f * static_cast<float>(code_rows())
                        : std::clamp(60.0f + message_lines * painter_.line_height(Face::Body) +
                                         16.0f + 56.0f + 24.0f,
                                     220.0f, height_ - 80.0f);
    Rect panel{(width_ - panel_w) / 2, (height_ - panel_h) / 2, panel_w, panel_h};
    device_map_.set_menu_panel(panel);
    painter_.fill_round(panel.box(), 16, rgb(0x171a21));
    painter_.stroke_round(panel.box(), 16, rgb(theme::BorderStrong), 1);

    painter_.text(overlay_.title(), RectF(panel.x + 26, panel.y + 20, panel.x + panel.w - 26,
                                              panel.y + 50),
                  Face::CardTitle, rgb(theme::Text));
    if (overlay_.is(OverlayPage::Players)) {
        // One row per pad, and the device that plays as it: the picks go to the game's
        // controls.txt, which is what the runtime reads.
        float left = panel.x + 26;
        float right = panel.x + panel.w - 26;
        float y = panel.y + 60;
        painter_.text(L"Pick the device each player uses. Controllers take the first free pad.",
                      RectF(left, y, right - 70, y + 20), Face::Tiny, rgb(theme::Muted));
        ui_.link(Id::SettingsRefreshDevices, Rect{right - 62, y, 62, 18}, L"Refresh");
        y += 26;
        auto pad_row = [&](Id id, const std::wstring& name, const std::wstring& value,
                           bool empty) {
            painter_.text(ui_.elide(name, Face::Small, right - left - 200),
                          RectF(left, y + 5, right - left - 200 + left, y + 27), Face::Small,
                          rgb(theme::Text));
            ui_.combo(id, Rect{right - 190, y, 190, 28}, value, !busy_, empty);
            y += 34;
        };
        for (int player = 1; player <= kPads; ++player) {
            const std::wstring value = device_map_.player_device_label(player);
            pad_row(static_cast<Id>(static_cast<int>(Id::PadRow) + player - 1),
                    L"Player " + std::to_wstring(player), value, value == L"Not set");
        }
        if (!device_map_.note().empty()) {
            painter_.text(ui_.elide(device_map_.note(), Face::Tiny, right - left),
                          RectF(left, y, right, y + 18), Face::Tiny, rgb(theme::Muted));
        }
    } else if (overlay_.is(OverlayPage::Mods)) {
        // One row per mod this disc allows: a folder in mods/, or a disc patch. The checks
        // are the choice Apply writes; a known mod whose files are not here yet is
        // downloaded when it is checked.
        const float left = panel.x + 26;
        const float right = panel.x + panel.w - 26;
        const float version_right = right - 150;
        const float version_left = version_right - 90;
        float y = panel.y + 60;
        painter_.text(L"Only mods this disc can play are listed. A disc patch replaces the "
                      L"whole disc. Apply writes the checks.",
                      RectF(left, y, right, y + 20), Face::Tiny, rgb(theme::Muted));
        y += 26;
        painter_.text(L"On", RectF(left, y, left + 24, y + 16), Face::Tiny,
                      rgb(theme::Muted));
        painter_.text(L"Mod", RectF(left + 30, y, version_left - 6, y + 16), Face::Tiny,
                      rgb(theme::Muted));
        painter_.text(L"Version", RectF(version_left, y, version_right - 6, y + 16),
                      Face::Tiny, rgb(theme::Muted), TextAlign::Trailing);
        painter_.text(L"Author", RectF(version_right, y, right, y + 16), Face::Tiny,
                      rgb(theme::Muted), TextAlign::Trailing);
        y += 22;
        for (size_t i = 0; i < mods_.rows(); ++i) {
            const ModChoice& entry = mods_.entries()[i];
            Id id = static_cast<Id>(static_cast<int>(Id::ModRow) + static_cast<int>(i));
            const bool available = mods_.downloadable(entry.id);
            std::wstring label = entry.name;
            if (entry.patch) label += L" · disc patch";
            else if (!entry.mod.gecko_files.empty()) label += L" · codes";
            if (!entry.installed) label += L" · download";
            ui_.checkbox(id, left, y + 6, ui_.elide(label, Face::Small, version_left - left - 30),
                     mods_.checked(entry.id), !busy_ && available);
            painter_.text(ui_.elide(entry.version, Face::Tiny, 84),
                          RectF(version_left, y + 4, version_right - 6, y + 26), Face::Tiny,
                          rgb(theme::Muted), TextAlign::Trailing);
            painter_.text(ui_.elide(entry.author, Face::Tiny, right - version_right),
                          RectF(version_right, y + 4, right, y + 26), Face::Tiny,
                          rgb(theme::Muted), TextAlign::Trailing);
            // The whole row is the checkbox, so a click anywhere on it is the same choice.
            if (available && !busy_) ui_.register_hit(id, Rect{left - 4, y - 4, right - left + 8, 30});
            y += 34;
        }

        // What the row under the cursor is, or what the panel will do when Apply is
        // pressed. The description is here rather than in a column because it is the one
        // thing about a mod a table cannot keep short.
        const ModChoice* hovered = nullptr;
        for (size_t i = 0; i < mods_.rows(); ++i) {
            if (ui_.hover() == static_cast<Id>(static_cast<int>(Id::ModRow) + static_cast<int>(i)))
                hovered = &mods_.entries()[i];
        }
        const bool staged =
            mods_.staged();
        std::wstring note;
        if (hovered && !hovered->description.empty()) {
            note = hovered->description;
        } else if (!mods_note_.empty()) {
            note = mods_note_;
        } else if (staged) {
            note = L"Apply to write these changes to the game.";
        } else {
            note = mods_summary();
        }
        painter_.text(ui_.elide(note, Face::Small, right - left),
                      RectF(left, y + 8, right, y + 30), Face::Small, rgb(theme::Muted));
        if (!patch_note_.empty()) {
            painter_.text(ui_.elide(patch_note_, Face::Tiny, right - left),
                          RectF(left, y + 30, right, y + 48), Face::Tiny, rgb(theme::Muted));
        } else if (mods_.rows() < mods_.entries().size()) {
            painter_.text(format(L"… and %zu more in the mods folder.",
                                 mods_.entries().size() - mods_.rows()),
                          RectF(left, y + 30, right, y + 48), Face::Tiny, rgb(theme::Muted));
        }
        ui_.link(Id::ModsFolder, Rect{left, panel.y + panel.h - 84, 160, 18},
             L"Open the mods folder");
        ui_.link(Id::ModChoose, Rect{left + 172, panel.y + panel.h - 84, 190, 18},
             L"Choose a patch file…");
        ui_.button(Id::ModApply,
               Rect{panel.x + panel.w - 26 - 130 - 8 - 110, panel.y + panel.h - 56, 110, 36},
               L"Apply", true, !busy_ && staged);
    } else if (overlay_.is(OverlayPage::Geckos)) {
        // One row per code this disc can be played with, its own and the guide's. The checks
        // are the choice Apply writes; a code the recompiler cannot run is listed so it can
        // be read, and cannot be switched on.
        const float left = panel.x + 26;
        const float right = panel.x + panel.w - 26;
        const float source_right = right;
        const float source_left = right - 96;
        float y = panel.y + 58;
        const std::wstring lead =
            project_ ? L"Only codes written for this disc are listed. Each is switched on by "
                       L"itself."
                     : L"Set up the game folder first: the codes are kept beside it.";
        painter_.text(ui_.elide(lead, Face::Tiny, right - left),
                      RectF(left, y, right, y + 18), Face::Tiny, rgb(theme::Muted));
        y += 20;
        std::wstring second;
        if (codes_fetching_) {
            second = L"Fetching what the guide for this disc holds\u2026";
        } else if (!online_label_.empty()) {
            second = L"The guide's list is here (" + online_label_ + L").";
        } else if (project_) {
            second = L"Fetch to see the codes the community's guide holds for this disc.";
        }
        if (!second.empty()) {
            painter_.text(ui_.elide(second, Face::Tiny, right - left),
                          RectF(left, y, right, y + 18), Face::Tiny, rgb(theme::Muted));
        }
        y += 22;
        painter_.text(L"On", RectF(left, y, left + 24, y + 16), Face::Tiny, rgb(theme::Muted));
        painter_.text(L"Code", RectF(left + 30, y, source_left - 6, y + 16), Face::Tiny,
                      rgb(theme::Muted));
        painter_.text(L"From", RectF(source_left, y, source_right, y + 16), Face::Tiny,
                      rgb(theme::Muted), TextAlign::Trailing);
        y += 20;

        const std::size_t rows = code_rows();
        if (rows > 0 && code_offset_ + rows > code_entries_.size())
            code_offset_ = code_entries_.size() - rows;
        if (rows == 0) code_offset_ = 0;
        for (std::size_t i = 0; i < rows; ++i) {
            const CodeEntry& entry = code_entries_[code_offset_ + i];
            Id id = static_cast<Id>(static_cast<int>(Id::CodeRow) + static_cast<int>(i));
            const bool usable = entry.runnable && !busy_;
            // A code this recompiler cannot run is listed so it can be read, and says so in
            // the row: a grey check with no reason beside it reads as a bug.
            std::wstring label = entry.name;
            if (!entry.runnable) label += L" · can't run here";
            ui_.checkbox(id, left, y + 3, ui_.elide(label, Face::Small, source_left - left - 30),
                     code_checked(entry.name), usable);
            painter_.text(entry.online ? L"guide" : L"yours",
                          RectF(source_left, y + 3, source_right, y + 19), Face::Tiny,
                          entry.runnable ? rgb(theme::Muted) : rgb(0x69707d),
                          TextAlign::Trailing);
            if (usable) ui_.register_hit(id, Rect{left - 4, y - 3, right - left + 8, 24});
            y += 22;
        }

        // The line under the table: what Apply did, or what the row under the cursor is.
        std::wstring note = codes_note_;
        if (note.empty()) {
            for (std::size_t i = 0; i < rows; ++i) {
                if (ui_.hover() !=
                    static_cast<Id>(static_cast<int>(Id::CodeRow) + static_cast<int>(i)))
                    continue;
                const CodeEntry& entry = code_entries_[code_offset_ + i];
                if (!entry.runnable) {
                    note = entry.warning.empty()
                               ? std::wstring(L"This code does nothing this game can run.")
                               : entry.warning;
                } else {
                    note = format(L"%zu operation(s) run once a frame.", entry.ops);
                }
                break;
            }
        }
        if (note.empty()) {
            const bool staged = pending_codes_ != enabled_codes_;
            note = staged ? L"Apply to write these changes to the game."
                          : format(L"%zu code(s) on for this game.", enabled_codes_.size());
        }
        if (code_entries_.size() > rows && codes_note_.empty() && ui_.hover() == Id::None) {
            // A list longer than the panel says so, because the wheel is the only sign of it.
            note = format(L"%zu codes; scroll for the rest. %zu on for this game.",
                          code_entries_.size(), enabled_codes_.size());
        }

        // The box a block pasted from an editor goes into, and what that block becomes.
        const float box_h = 66.0f;
        Rect editor{left, panel.y + panel.h - 162, right - left, box_h};
        painter_.fill_round(editor.box(), 6, rgb(gecko_editing_ ? 0x1d222b : 0x171b21));
        painter_.stroke_round(editor.box(), 6,
                              rgb(gecko_editing_ ? theme::Accent : theme::BorderStrong), 1);
        const float line_h = painter_.line_height(Face::Tiny);
        const int fits = std::max(1, static_cast<int>((box_h - 10.0f) / line_h));
        if (gecko_edit_.empty()) {
            painter_.text(L"Paste a block of codes here, then Apply.",
                          RectF(editor.x + 8, editor.y + 6, editor.x + editor.w - 8,
                                editor.y + 6 + line_h),
                          Face::Tiny, rgb(0x69707d));
        } else {
            auto lines = split(gecko_edit_, L'\n');
            const int first = std::max(0, static_cast<int>(lines.size()) - fits);
            float line_y = editor.y + 6;
            for (int i = first; i < static_cast<int>(lines.size()); ++i) {
                painter_.text_clipped(lines[static_cast<size_t>(i)],
                                      RectF(editor.x + 8, line_y, editor.x + editor.w - 8,
                                            line_y + line_h),
                                      Face::Tiny, rgb(theme::Text));
                line_y += line_h;
            }
        }
        // The box takes the click whether or not it has focus: that is how it gets focus.
        if (!busy_) ui_.register_hit(Id::CodeBox, editor);

        // The guide's list is fetched on its own, so the only thing wanted here is a way to
        // ask again -- after a fetch that failed, or for a guide that has been updated.
        const std::wstring refresh = codes_fetching_ ? L"Fetching…" : L"Refresh list";
        const float refresh_w = ui_.text_width(refresh, Face::Tiny) + 10.0f;
        ui_.link(Id::CodeRefresh, Rect{right - refresh_w, panel.y + panel.h - 86, refresh_w, 18},
             refresh, project_ != std::nullopt && !codes_fetching_);
        painter_.text(ui_.elide(note, Face::Small, right - left - refresh_w - 16.0f),
                      RectF(left, panel.y + panel.h - 84, right - refresh_w - 12.0f,
                            panel.y + panel.h - 66),
                      Face::Small, rgb(theme::Muted));
        const bool staged = pending_codes_ != enabled_codes_ || !gecko_edit_.empty();
        ui_.button(Id::CodeApply,
               Rect{panel.x + panel.w - 26 - 130 - 8 - 110, panel.y + panel.h - 56, 110, 36},
               L"Apply", true, !busy_ && project_ != std::nullopt && staged);
    } else {
        painter_.text(overlay_.text(), RectF(panel.x + 26, panel.y + 60, panel.x + panel.w - 26,
                                                 panel.y + panel.h - 66),
                      Face::Body, rgb(theme::Muted));
    }

    float button_right = panel.x + panel.w - 26;
    const float button_y = panel.y + panel.h - 56;
    button_right -= 130;
    ui_.button(Id::OverlayAction, Rect{button_right, button_y, 130, 36}, overlay_.action(), true,
           true);
    if (overlay_.confirm()) {
        button_right -= 8 + 100;
        ui_.button(Id::OverlayCancel, Rect{button_right, button_y, 100, 36}, L"Cancel", false,
               true);
    }
    if (!overlay_.second().empty()) {
        const float second_w =
            std::max(110.0f, ui_.text_width(overlay_.second(), Face::Button) + 26.0f);
        button_right -= 8 + second_w;
        ui_.button(Id::OverlaySecondary, Rect{button_right, button_y, second_w, 36},
               overlay_.second(), false, true);
    }
}

// What a player row offers: nothing, the keyboard, then each controller the game sees. A
// row near the bottom of the panel opens its menu upwards, so the panel hides nothing.
void App::paint_pad_menu() {
    const std::vector<std::wstring> options = device_map_.pad_options();
    const int count = static_cast<int>(options.size());
    Rect menu{device_map_.menu_anchor().x, device_map_.menu_anchor().y + device_map_.menu_anchor().h + 6, device_map_.menu_anchor().w,
              count * 30.0f + 8};
    if (menu.y + menu.h > device_map_.menu_panel().y + device_map_.menu_panel().h - 10)
        menu.y = device_map_.menu_anchor().y - 6 - menu.h;
    painter_.fill_round(menu.box(), 10, rgb(0x1b1e25));
    painter_.stroke_round(menu.box(), 10, rgb(theme::BorderStrong), 1);
    const int player = device_map_.menu_row() + 1;
    int current = 0;
    if (device_map_.player_is_keyboard(player)) {
        current = 1;
    } else if (const int device = device_map_.player_device(player); device >= 0) {
        current = 2 + device;
    }
    for (int i = 0; i < count; ++i) {
        Rect item{menu.x + 4, menu.y + 4 + i * 30.0f, menu.w - 8, 30};
        Id id = static_cast<Id>(static_cast<int>(Id::PadOption) + i);
        if (ui_.hover() == id) painter_.fill_round(item.box(), 6, rgb(0x2b3340));
        painter_.text(ui_.elide(options[static_cast<size_t>(i)], Face::Small, item.w - 20),
                      RectF(item.x + 10, item.y, item.x + item.w - 10, item.y + item.h),
                      Face::Small,
                      i == current ? rgb(theme::Accent)
                                   : rgb(i == 0 ? theme::Muted : theme::Text),
                      TextAlign::Leading, TextVAlign::Center, false);
        ui_.register_hit(id, item);
    }
}

void App::paint_scale_menu() {    Rect menu{scale_anchor_.x, scale_anchor_.y + scale_anchor_.h + 6, scale_anchor_.w, 4 * 32 + 8};
    painter_.fill_round(menu.box(), 10, rgb(0x1b1e25));
    painter_.stroke_round(menu.box(), 10, rgb(theme::BorderStrong), 1);
    for (int index = 0; index < 4; ++index) {
        Rect item{menu.x + 4, menu.y + 4 + index * 32.0f, menu.w - 8, 32};
        Id id = static_cast<Id>(static_cast<int>(Id::Scale1) + index);
        if (ui_.hover() == id) painter_.fill_round(item.box(), 6, rgb(0x2b3340));
        painter_.text(scale_label(index + 1),
                      RectF(item.x + 10, item.y, item.x + item.w - 10, item.y + item.h),
                      Face::Small,
                      settings_.scale == index + 1 ? rgb(theme::Accent) : rgb(theme::Text),
                      TextAlign::Leading, TextVAlign::Center,
                      false);
        ui_.register_hit(id, item);
    }
}

// -- pieces ----------------------------------------------------------------

void App::paint_picker() {
    painter_.fill_rect(RectF(0, 0, width_, height_), rgb(0x000000, 0.62f));
    float panel_w = std::min(760.0f, width_ - 60);
    float panel_h = std::min(560.0f, height_ - 60);
    Rect panel{(width_ - panel_w) / 2, (height_ - panel_h) / 2, panel_w, panel_h};
    painter_.fill_round(panel.box(), 16, rgb(0x171a21));
    painter_.stroke_round(panel.box(), 16, rgb(theme::BorderStrong), 1);

    painter_.text(picker_title_,
                  RectF(panel.x + 24, panel.y + 18, panel.x + panel.w - 24, panel.y + 46),
                  Face::CardTitle, rgb(theme::Text));

    float left = panel.x + 24;
    float content_w = panel.w - 48;

    Rect field{left, panel.y + 56, content_w - 76, 34};
    painter_.fill_round(field.box(), 8, rgb(picker_editing_ ? 0x20252d : 0x1b1e25));
    painter_.stroke_round(field.box(), 8,
                          rgb(picker_editing_ ? theme::Accent : theme::BorderStrong), 1);
    painter_.text(ui_.elide(picker_path_, Face::Small, field.w - 24),
                  RectF(field.x + 10, field.y, field.x + field.w - 10, field.y + field.h),
                  Face::Small, rgb(theme::Text), TextAlign::Leading, TextVAlign::Center,
                  false);
    ui_.register_hit(Id::PickerPath, field);
    ui_.button(Id::PickerUp, Rect{field.x + field.w + 10, field.y, 66, 34}, L"Up", false, true);

    Rect list{left, panel.y + 100, content_w, panel.h - 100 - 76};
    painter_.fill_round(list.box(), 10, rgb(theme::Log));
    painter_.stroke_round(list.box(), 10, rgb(0x242932), 1);

    const float row_height = 26.0f;
    int visible = std::max(1, static_cast<int>((list.h - 12) / row_height));
    int maximum_offset = std::max(0, static_cast<int>(picker_entries_.size()) - visible);
    picker_offset_ = std::min(picker_offset_, maximum_offset);
    for (int row = 0; row < visible; ++row) {
        int index = picker_offset_ + row;
        if (index >= static_cast<int>(picker_entries_.size())) break;
        const auto& entry = picker_entries_[static_cast<size_t>(index)];
        Rect item{list.x + 6, list.y + 6 + row * row_height, list.w - 12, row_height};
        Id id = static_cast<Id>(static_cast<int>(Id::PickerEntry) + index);
        if (picker_selected_ == index) {
            painter_.fill_round(item.box(), 6, rgb(0x2b3340));
        } else if (ui_.hover() == id) {
            painter_.fill_round(item.box(), 6, rgb(0x222732));
        }
        std::wstring label = entry.name;
        if (entry.directory) label = entry.name == L".." ? L"Parent folder" : label + L"/";
        painter_.text(label, RectF(item.x + 10, item.y, item.x + item.w - 10,
                                  item.y + item.h),
                      Face::Small, entry.directory ? rgb(0xc8cede) : rgb(theme::Text),
                      TextAlign::Leading, TextVAlign::Center, false);
        ui_.register_hit(id, item);
    }
    if (picker_entries_.empty()) {
        painter_.text(picker_ == PickerKind::Image
                          ? L"No disc images here. Type the path, or drop the image on the "
                            L"window."
                          : L"No sub-folders here. Type the path, or use this folder.",
                      RectF(list.x + 14, list.y + 10, list.x + list.w - 14, list.y + 34),
                      Face::Small, rgb(theme::Muted));
    }

    std::wstring note = picker_note_;
    if (note.empty()) {
        note = picker_ == PickerKind::Folder ? L"Choose this folder, or open a sub-folder."
                                             : L"Choose a disc image, or type its path.";
    }
    painter_.text(note,
                  RectF(left, panel.y + panel.h - 66, left + content_w - 300,
                       panel.y + panel.h - 44),
                  Face::Tiny, picker_note_.empty() ? rgb(theme::Muted) : rgb(theme::Warn));

    ui_.button(Id::PickerCancel,
           Rect{panel.x + panel.w - 24 - 100, panel.y + panel.h - 52, 100, 34}, L"Cancel",
           false, true);
    ui_.button(Id::PickerAccept,
           Rect{panel.x + panel.w - 24 - 100 - 8 - 176, panel.y + panel.h - 52, 176, 34},
           picker_ == PickerKind::Folder ? L"Use This Folder" : L"Choose Image", true, true);
}

}  // namespace nla
