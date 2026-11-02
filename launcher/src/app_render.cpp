// The off-screen render: one frame written to a file with no window, so the drawing can be
// checked on a machine with no desktop and by the launcher's own tests. It is not part of
// painting the window - it drives the same painters and clicks the same hit list, which is
// what makes it worth having.

#include "app.h"
#include "app_shared.h"

#include "util.h"

#include <algorithm>
#include <cstdio>

namespace nla {

namespace {

// What a render writes: a 32-bit BMP, bottom row first, which is the one format every image
// viewer reads and nothing needs a library for.
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
    state_.load_settings();
    tools_ = check_tools();
    if (auto root = Project::locate()) state_.set_project(*root, state_.settings().game);
    if (state_.project()) state_.settings().game = state_.project()->game();
    state_.reload_status();
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
        ReleaseInfo stand_in;
        stand_in.version = L"1.1.0";
        stand_in.page_url = L"https://example.invalid/releases/tag/v1.1.0";
        stand_in.asset_name = L"NarutoLauncher-macOS-arm64-v1.1.0.zip";
        stand_in.asset_url = L"https://example.invalid/" + stand_in.asset_name;
        update_.set_stand_in(std::move(stand_in));
    };

    if (view == "settings") {
        open_settings();
    } else if (view == "players") {
        players_.open();
    } else if (view.rfind("players-click:", 0) == 0) {
        players_.open();
        if (!click(view.substr(14))) {
            std::fprintf(stderr, "usage: players-click:<x>,<y>\n");
            return 1;
        }
    } else if (view == "mods") {
        mods_.open();
    } else if (view.rfind("mods-click:", 0) == 0) {
        mods_.open();
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
            state_.set_project(scratch, game);
            // A disc stands extracted, or the page would not offer the button at all.
            ProjectStatus stand_in;
            stand_in.disc.extracted = true;
            state_.set_status(stand_in);
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
            if (!overlay_.is(OverlayPage::Mods) || overlay_.title() != wanted) {
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
            if (overlay_.showing()) {
                std::fprintf(stderr,
                             "mods-from-game: Escape did not return to the game\n");
                return 1;
            }
        }
    } else if (view == "geckos") {
        geckos_.open();
    } else if (view == "geckos-paste") {
        // The box is clicked and then handed text the way the window hands it over, so the
        // path being checked is the one a paste takes, not a shortcut into the buffer.
        geckos_.open();
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
        if (!geckos_.editing()) {
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
        geckos_.open();
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
            state_.set_project(scratch, game);
            // A disc stands extracted, or the page would not offer the button at all.
            ProjectStatus stand_in;
            stand_in.disc.extracted = true;
            state_.set_status(stand_in);
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
            if (!overlay_.is(OverlayPage::Geckos) || overlay_.title() != wanted) {
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
            if (overlay_.showing()) {
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
            state_.project() ? std::optional<fs::path>(state_.project()->covers_dir()) : std::nullopt;
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
        state_.set_project(scratch, state_.settings().game);
        if (covers) {
            std::error_code link_ec;
            fs::create_directory_symlink(*covers, scratch / L"covers", link_ec);
        }
        // A game with its disc extracted stands behind everything but `setup` and
        // `needs-disc`; the two live states are that page with a step in flight.
        const bool installed = state != "setup" && state != "needs-disc";
        ProjectStatus stand_in;
        stand_in.disc.extracted = installed;
        // A wrong disc is one that was extracted and did not match.
        stand_in.disc.verified = installed && state != "wrong-disc";
        stand_in.built = state == "mods-changed" || state == "ready";
        stand_in.stale_code = state == "mods-changed";
        state_.set_status(stand_in);
        if (state == "setup") state_.clear_project();
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
        players_.open();
        if (!click_control(Id::PadRow, last ? kPads - 1 : 0)) return 1;
    } else if (view == "picker") {
        picker_.choose_path(PickerKind::Image, L"Choose your own disc image",
                            state_.project() ? state_.project()->root() : home_dir(),
                            [](const fs::path&) {});
    } else if (view == "update") {
        fake_update();
        update_.show_prompt();
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
        state_.set_project(scratch, state_.settings().game);

        auto games = games_.available_games();
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
        if (!state_.project() || state_.project()->game() != wanted.folder) {
            std::fprintf(stderr, "cover-click: the page is on %s, not %s\n",
                         state_.project() ? narrow(state_.project()->game()).c_str() : "no game",
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


}  // namespace nla
