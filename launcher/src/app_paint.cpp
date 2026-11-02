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
            status_ = ProjectStatus{};
            paint();
            Rect mods{};
            bool found = false;
            for (const auto& hit : hits_) {
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
            if (!overlay_open_ || !overlay_mods_ || settings_open_ ||
                overlay_title_ != wanted) {
                std::fprintf(stderr, "mods-from-game: %s\n",
                             narrow(overlay_mods_ ? L"the mods panel says \"" +
                                                        overlay_title_ + L"\""
                                                  : L"the Mods button opened no panel")
                                 .c_str());
                return 1;
            }
            SDL_Event escape{};
            escape.type = SDL_KEYDOWN;
            escape.key.keysym.sym = SDLK_ESCAPE;
            handle_event(escape);
            if (overlay_open_ || settings_open_) {
                std::fprintf(stderr,
                             "mods-from-game: Escape did not return to the game\n");
                return 1;
            }
        }
    } else if (view == "picker") {
        open_picker(PickerKind::Image, L"Choose your own disc image",
                    project_ ? project_->root() : home_dir(), [](const fs::path&) {});
    } else if (view == "update") {
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
            for (const auto& hit : hits_) {
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
    hits_.clear();

    paint_header();
    paint_games();

    if (settings_open_) {
        paint_settings();
    } else if (overlay_open_) {
        paint_overlay();
    }
    if (picker_ != PickerKind::None) paint_picker();
    if (scale_open_) paint_scale_menu();
    if (pad_menu_row_ >= 0) paint_pad_menu();

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
    button(Id::OpenSettings, settings_button, L"Settings", false, true);

    // The games are the big covers below, so the header only carries the title of the
    // one they would launch, and what the launcher found about it.
    float title_right = settings_button.x - 16;
    if (update_state_ == UpdateState::Available && !update_release_.version.empty()) {
        std::wstring label = L"Update " + update_release_.version;
        float badge_w = text_width(label, Face::Button) + 30;
        Rect badge{settings_button.x - 8 - badge_w, 24, badge_w, 34};
        button(Id::UpdateBanner, badge, label, true, !busy_);
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

    std::wstring title;
    std::wstring detail;
    std::wstring pill_text;
    Pill pill_state = Pill::Warn;

    if (busy_) {
        title = stage_.empty() ? L"Working…" : stage_;
        detail = L"The launcher is running this step. Details holds its output.";
        pill_text = L"working";
        pill_state = Pill::Muted;
    } else if (game_.running()) {
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
        if (progress_ >= 0) {
            float fraction = std::min(1.0f, static_cast<float>(progress_) / 100.0f);
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
    // What the page offers once a game is picked: one button to start it and one to change
    // what it plays. A cover only chooses the game, so nothing starts by accident.
    auto draw_game_buttons = [&](float center_x, float top, float width = 360.0f) {
        const bool waiting = busy_ || game_.running();
        // Play asks for a disc when there is no verified tree, which is what launch() does:
        // the button says so rather than starting and then asking.
        const bool needs_disc = !project_ || !status_.disc.verified;
        const float button_w = std::min(180.0f, width * 0.5f - 8.0f);
        const float gap = 16.0f;
        const float left = center_x - button_w - gap / 2;
        button(Id::Play, Rect{left, top, button_w, 50},
               needs_disc ? L"Set up the disc…" : L"Play", true, !waiting, false,
               Face::ButtonLarge);
        button(Id::Mods, Rect{left + button_w + gap, top, button_w, 50}, L"Mods", false,
               !waiting && project_ != std::nullopt, false, Face::ButtonLarge);
    };

    auto draw_controls = [&](float center_x, float top, float width) {
        float bottom = top;
        if (busy_) {
            progress_bar(Rect{center_x - width / 2, top + 4, width, 6});
            if (job_.running()) {
                float cancel_w = text_width(L"Cancel", Face::Tiny);
                link(Id::Stop, Rect{center_x - cancel_w / 2, top + 20, cancel_w, 18},
                     L"Cancel");
            }
            bottom = top + 44;
        } else if (game_.running()) {
            button(Id::Stop, Rect{center_x - 110, top, 220, 40}, L"Stop the game", true, true,
                   true);
            bottom = top + 46;
        }
        if (!compact) {
            std::wstring toggle = L"Show details";
            float toggle_w = text_width(toggle, Face::Tiny);
            link(Id::LogToggle, Rect{center_x - toggle_w / 2, bottom + 4, toggle_w, 18},
                 toggle);
            bottom += 24;
        }
        if (!play_status_.empty() && !busy_) {
            painter_.text(play_status_,
                          RectF(center_x - 330, bottom + 2, center_x + 330, bottom + 20),
                          Face::Tiny, rgb(theme::Muted), TextAlign::Center);
        }
    };

    if (games.empty()) {
        constexpr float kBlockHeight = 214.0f;
        float center_x = width_ / 2;
        float top =
            kHeaderBottom + std::max(24.0f, (height_ - kHeaderBottom - kBlockHeight) * 0.5f);
        float pill_w = text_width(pill_text, Face::Small) + 32;
        pill(Rect{center_x - pill_w / 2, top, pill_w, 22}, pill_text, pill_state);
        painter_.text(title, RectF(center_x - 380, top + 26, center_x + 380, top + 78),
                      Face::Hero, rgb(theme::Text), TextAlign::Center);
        painter_.text(detail, RectF(center_x - 340, top + 82, center_x + 340, top + 136),
                      Face::Body, rgb(theme::Muted), TextAlign::Center);
        button(Id::Launch, Rect{center_x - 130, top + 152, 260, 50}, L"Choose a folder",
               true, !busy_, false, Face::ButtonLarge);
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

    float max_tile_width = (width_ - 96.0f - gap * (count - 1)) / count;
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

    bool can_launch = !busy_ && !game_.running();
    float x = left;
    for (int index = 0; index < count; ++index) {
        const auto& game = games[static_cast<size_t>(index)];
        bool selected = project_ && project_->game() == game.folder;
        Rect tile{x, top, tile_width, tile_height};
        paint_cover(static_cast<Id>(static_cast<int>(Id::GameCover) + index), tile, game,
                    selected, can_launch);
        painter_.text(game.short_title,
                      RectF(tile.x, tile.y + tile.h + 4, tile.x + tile.w,
                            tile.y + tile.h + 4 + label_height),
                      Face::Button, selected ? rgb(theme::Accent) : rgb(theme::Muted),
                      TextAlign::Center, TextVAlign::Center, false);
        x += tile_width + gap;
    }

    if (compact) {
        float status_left = left + row_width + 28.0f;
        float status_right = width_ - 24.0f;
        float pill_w = text_width(pill_text, Face::Small) + 32;
        pill(Rect{status_left, top + 2, pill_w, 22}, pill_text, pill_state);
        painter_.text(title,
                      RectF(status_left, top + 32, status_right, top + 60),
                      Face::CardTitle, rgb(theme::Text));
        painter_.text(detail, RectF(status_left, top + 66, status_right, top + 132),
                      Face::Body, rgb(theme::Muted));
        draw_game_buttons((status_left + status_right) / 2, top + 136, 280.0f);
        draw_controls((status_left + status_right) / 2, top + 196,
                      std::min(420.0f, status_right - status_left));
        paint_activity(top + tile_height + label_height + 26.0f);
        return;
    }

    float status_top = top + tile_height + label_height + 22.0f;
    float center_x = width_ / 2;
    float pill_w = text_width(pill_text, Face::Small) + 32;
    pill(Rect{center_x - pill_w / 2, status_top, pill_w, 22}, pill_text, pill_state);
    painter_.text(title,
                  RectF(center_x - 380, status_top + 26, center_x + 380, status_top + 74),
                  Face::Hero, rgb(theme::Text), TextAlign::Center);
    painter_.text(detail,
                  RectF(center_x - 350, status_top + 78, center_x + 350, status_top + 124),
                  Face::Body, rgb(theme::Muted), TextAlign::Center);
    draw_game_buttons(center_x, status_top + 130);
    draw_controls(center_x, status_top + 192, 420.0f);
}

void App::paint_cover(Id id, const Rect& rect, const GameInfo& game, bool selected,
                      bool enabled) {
    bool hovered = enabled && hover_ == id;
    bool down = enabled && pressed_ == id;
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
    if (enabled) register_hit(id, rect);
}

void App::paint_activity(float top) {
    float card_height = std::max(140.0f, height_ - top - 24.0f);
    Rect card{24, top, width_ - 48, card_height};
    card_box(card);

    painter_.text(L"Activity", RectF(card.x + 20, card.y + 16, card.x + 200,
                                           card.y + 40),
                  Face::CardTitle, rgb(theme::Text));

    std::wstring toggle = log_visible_ ? L"Hide" : L"Show";
    float clear_w = text_width(L"Clear", Face::Tiny);
    float copy_w = text_width(L"Copy", Face::Tiny);
    float toggle_w = text_width(toggle, Face::Tiny);
    float right = card.x + card.w - 20;
    link(Id::LogToggle, Rect{right - toggle_w, card.y + 18, toggle_w, 18}, toggle);
    right -= toggle_w + 16;
    link(Id::CopyLog, Rect{right - copy_w, card.y + 18, copy_w, 18}, L"Copy");
    right -= copy_w + 16;
    link(Id::ClearLog, Rect{right - clear_w, card.y + 18, clear_w, 18}, L"Clear");

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
        if (log_.empty()) {
            painter_.text(L"Setup and build output appears here.",
                          RectF(log_area.x + 12, log_area.y + 8, log_area.x + log_area.w - 12,
                                      log_area.y + 28),
                          Face::Mono, rgb(0x646d7d));
        } else {
            const float line_height = painter_.line_height(Face::Mono);
            int visible = static_cast<int>((log_area.h - 16) / line_height);
            visible = std::max(1, visible);
            int total = static_cast<int>(log_.size());
            int max_offset = std::max(0, total - visible);
            log_offset_ = std::min(log_offset_, max_offset);
            int end = total - log_offset_;
            int start = std::max(0, end - visible);
            for (int index = start; index < end; ++index) {
                float y = log_area.y + 8 + (index - start) * line_height;
                Color color = rgb(0xc8cede);
                const auto& line = log_[static_cast<size_t>(index)];
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
        std::wstring label = stage_.empty() ? L"Working…" : stage_;
        painter_.text(label, RectF(card.x + 20, bottom - 30, card.x + card.w - 160,
                                         bottom - 10),
                      Face::Tiny, rgb(theme::Muted));
        Rect cancel{card.x + card.w - 20 - 86, bottom - 32, 86, 24};
        if (job_.running()) {
            painter_.text(L"Cancel", RectF(cancel.x, cancel.y, cancel.x + cancel.w,
                                                 cancel.y + cancel.h),
                          Face::Tiny, rgb(theme::Link), TextAlign::Center,
                          TextVAlign::Center);
            register_hit(Id::Stop, cancel);
        }

        Rect bar{card.x + 20, bar_y, card.w - 40, 6};
        painter_.fill_round(bar.box(), 3, rgb(0x242932));
        if (progress_ >= 0) {
            float fraction = std::min(1.0f, static_cast<float>(progress_) / 100.0f);
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
    link(Id::SettingsClose, Rect{panel.x + panel.w - 26 - 40, panel.y + 20, 40, 20}, L"Close");

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
    painter_.text(elide(L"Which controller plays as which player.", Face::Small,
                        content_w - 130),
                  RectF(left, y + 7, left + content_w - 130, y + 29), Face::Small,
                  rgb(theme::Muted));
    button(Id::SettingsPlayers, Rect{right - 110, y, 110, 32}, L"Players…", false, !busy_);
    y += 36;
    separator();

    section(L"RESOLUTION");
    painter_.text(L"Window scale", RectF(left, y + 8, left + 96, y + 28), Face::Small,
                  rgb(theme::Muted));
    scale_combo(Id::Scale, Rect{left + 96, y, 180, 32}, !busy_);
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
    painter_.text(elide(update_line, Face::Small, content_w - 250),
                  RectF(left, y + 7, left + content_w - 250, y + 29), Face::Small,
                  update_color);
    button(Id::SettingsCheckNow, Rect{right - 100, y, 100, 32}, L"Check now", false,
           !busy_ && update_state_ != UpdateState::Checking);
    if (update_state_ == UpdateState::Available) {
        button(Id::SettingsInstallUpdate, Rect{right - 100 - 8 - 128, y, 128, 32},
               L"Install update", true, !busy_);
    }
    y += 36;
    checkbox(Id::SettingsToggleUpdates, left, y + 4, L"Check when the launcher starts",
             settings_.check_updates, !busy_);
    y += 24;

    if (!settings_note_.empty()) {
        painter_.text(settings_note_, RectF(left, y, left + content_w, y + 22),
                      Face::Tiny, rgb(theme::Ok));
    }

    button(Id::SettingsClose, Rect{right - 110, panel.y + panel.h - 54, 110, 36}, L"Done",
           true, true);
}

void App::paint_overlay() {
    painter_.fill_rect(RectF(0, 0, width_, height_), rgb(0x000000, 0.62f));
    float panel_w = overlay_players_ ? 660.0f : overlay_mods_ ? 720.0f : 620.0f;
    // A row for each of the console's four pads, and the note above them.
    float panel_h = overlay_players_ ? 168.0f + 34.0f * kPads
                    : overlay_mods_  ? 250.0f + 34.0f * static_cast<float>(mod_rows())
                                     : 336.0f;
    Rect panel{(width_ - panel_w) / 2, (height_ - panel_h) / 2, panel_w, panel_h};
    pad_menu_panel_ = panel;
    painter_.fill_round(panel.box(), 16, rgb(0x171a21));
    painter_.stroke_round(panel.box(), 16, rgb(theme::BorderStrong), 1);

    painter_.text(overlay_title_, RectF(panel.x + 26, panel.y + 20, panel.x + panel.w - 26,
                                              panel.y + 50),
                  Face::CardTitle, rgb(theme::Text));
    if (overlay_players_) {
        // One row per pad, and the device that plays as it: the picks go to the game's
        // controls.txt, which is what the runtime reads.
        float left = panel.x + 26;
        float right = panel.x + panel.w - 26;
        float y = panel.y + 60;
        painter_.text(L"Pick the device each player uses. Controllers take the first free pad.",
                      RectF(left, y, right - 70, y + 20), Face::Tiny, rgb(theme::Muted));
        link(Id::SettingsRefreshDevices, Rect{right - 62, y, 62, 18}, L"Refresh");
        y += 26;
        auto pad_row = [&](Id id, const std::wstring& name, const std::wstring& value,
                           bool empty) {
            painter_.text(elide(name, Face::Small, right - left - 200),
                          RectF(left, y + 5, right - left - 200 + left, y + 27), Face::Small,
                          rgb(theme::Text));
            pad_combo(id, Rect{right - 190, y, 190, 28}, value, !busy_, empty);
            y += 34;
        };
        for (int player = 1; player <= kPads; ++player) {
            const std::wstring value = player_device_label(player);
            pad_row(static_cast<Id>(static_cast<int>(Id::PadRow) + player - 1),
                    L"Player " + std::to_wstring(player), value, value == L"Not set");
        }
        if (!devices_note_.empty()) {
            painter_.text(elide(devices_note_, Face::Tiny, right - left),
                          RectF(left, y, right, y + 18), Face::Tiny, rgb(theme::Muted));
        }
    } else if (overlay_mods_) {
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
        for (size_t i = 0; i < mod_rows(); ++i) {
            const ModChoice& entry = mod_entries_[i];
            Id id = static_cast<Id>(static_cast<int>(Id::ModRow) + static_cast<int>(i));
            const bool available = mod_downloadable(entry.id);
            std::wstring label = entry.name;
            if (entry.patch) label += L" · disc patch";
            else if (!entry.mod.gecko_files.empty()) label += L" · codes";
            if (!entry.installed) label += L" · download";
            checkbox(id, left, y + 6, elide(label, Face::Small, version_left - left - 30),
                     mod_checked(entry.id), !busy_ && available);
            painter_.text(elide(entry.version, Face::Tiny, 84),
                          RectF(version_left, y + 4, version_right - 6, y + 26), Face::Tiny,
                          rgb(theme::Muted), TextAlign::Trailing);
            painter_.text(elide(entry.author, Face::Tiny, right - version_right),
                          RectF(version_right, y + 4, right, y + 26), Face::Tiny,
                          rgb(theme::Muted), TextAlign::Trailing);
            // The whole row is the checkbox, so a click anywhere on it is the same choice.
            if (available && !busy_) register_hit(id, Rect{left - 4, y - 4, right - left + 8, 30});
            y += 34;
        }

        // What the row under the cursor is, or what the panel will do when Apply is
        // pressed. The description is here rather than in a column because it is the one
        // thing about a mod a table cannot keep short.
        const ModChoice* hovered = nullptr;
        for (size_t i = 0; i < mod_rows(); ++i) {
            if (hover_ == static_cast<Id>(static_cast<int>(Id::ModRow) + static_cast<int>(i)))
                hovered = &mod_entries_[i];
        }
        const bool staged =
            pending_mods_ != enabled_mods_ || pending_patch_ != applied_patch_id_;
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
        painter_.text(elide(note, Face::Small, right - left),
                      RectF(left, y + 8, right, y + 30), Face::Small, rgb(theme::Muted));
        if (!patch_note_.empty()) {
            painter_.text(elide(patch_note_, Face::Tiny, right - left),
                          RectF(left, y + 30, right, y + 48), Face::Tiny, rgb(theme::Muted));
        } else if (mod_rows() < mod_entries_.size()) {
            painter_.text(format(L"… and %zu more in the mods folder.",
                                 mod_entries_.size() - mod_rows()),
                          RectF(left, y + 30, right, y + 48), Face::Tiny, rgb(theme::Muted));
        }
        link(Id::ModsFolder, Rect{left, panel.y + panel.h - 84, 160, 18},
             L"Open the mods folder");
        link(Id::ModChoose, Rect{left + 172, panel.y + panel.h - 84, 190, 18},
             L"Choose a patch file…");
        button(Id::ModApply,
               Rect{panel.x + panel.w - 26 - 130 - 8 - 110, panel.y + panel.h - 56, 110, 36},
               L"Apply", true, !busy_ && staged);
    } else {
        painter_.text(overlay_text_, RectF(panel.x + 26, panel.y + 60, panel.x + panel.w - 26,
                                                 panel.y + panel.h - 66),
                      Face::Body, rgb(theme::Muted));
    }

    button(Id::OverlayAction, Rect{panel.x + panel.w - 26 - 130, panel.y + panel.h - 56, 130,
                                   36},
           overlay_action_, true, true);
    if (overlay_confirm_) {
        button(Id::OverlayCancel, Rect{panel.x + panel.w - 26 - 130 - 8 - 100,
                                       panel.y + panel.h - 56, 100, 36},
               L"Cancel", false, true);
    }
}

void App::pad_combo(Id id, const Rect& rect, const std::wstring& value, bool enabled,
                    bool empty) {
    bool hovered = enabled && hover_ == id;
    painter_.fill_round(rect.box(), 8, rgb(hovered ? 0x2c333e : 0x242932));
    painter_.stroke_round(rect.box(), 8, rgb(theme::BorderStrong), 1);
    painter_.text(value, RectF(rect.x + 10, rect.y, rect.x + rect.w - 26, rect.y + rect.h),
                  Face::Small,
                  enabled ? rgb(empty ? theme::Muted : theme::Text) : rgb(0x69707d),
                  TextAlign::Leading, TextVAlign::Center, false);
    float chevron_x = rect.x + rect.w - 16;
    float chevron_y = rect.y + rect.h / 2 - 2;
    painter_.line(chevron_x - 4, chevron_y, chevron_x, chevron_y + 4, rgb(theme::Muted), 1.6f);
    painter_.line(chevron_x, chevron_y + 4, chevron_x + 4, chevron_y, rgb(theme::Muted), 1.6f);
    if (enabled) register_hit(id, rect);
}

// What a player row offers: nothing, the keyboard, then each controller the game sees. A
// row near the bottom of the panel opens its menu upwards, so the panel hides nothing.
void App::paint_pad_menu() {
    const std::vector<std::wstring> options = pad_options();
    const int count = static_cast<int>(options.size());
    Rect menu{pad_menu_anchor_.x, pad_menu_anchor_.y + pad_menu_anchor_.h + 6, pad_menu_anchor_.w,
              count * 30.0f + 8};
    if (menu.y + menu.h > pad_menu_panel_.y + pad_menu_panel_.h - 10)
        menu.y = pad_menu_anchor_.y - 6 - menu.h;
    painter_.fill_round(menu.box(), 10, rgb(0x1b1e25));
    painter_.stroke_round(menu.box(), 10, rgb(theme::BorderStrong), 1);
    const int player = pad_menu_row_ + 1;
    int current = 0;
    if (player_is_keyboard(player)) {
        current = 1;
    } else if (const int device = player_device(player); device >= 0) {
        current = 2 + device;
    }
    for (int i = 0; i < count; ++i) {
        Rect item{menu.x + 4, menu.y + 4 + i * 30.0f, menu.w - 8, 30};
        Id id = static_cast<Id>(static_cast<int>(Id::PadOption) + i);
        if (hover_ == id) painter_.fill_round(item.box(), 6, rgb(0x2b3340));
        painter_.text(elide(options[static_cast<size_t>(i)], Face::Small, item.w - 20),
                      RectF(item.x + 10, item.y, item.x + item.w - 10, item.y + item.h),
                      Face::Small,
                      i == current ? rgb(theme::Accent)
                                   : rgb(i == 0 ? theme::Muted : theme::Text),
                      TextAlign::Leading, TextVAlign::Center, false);
        register_hit(id, item);
    }
}

void App::paint_scale_menu() {    Rect menu{scale_anchor_.x, scale_anchor_.y + scale_anchor_.h + 6, scale_anchor_.w, 4 * 32 + 8};
    painter_.fill_round(menu.box(), 10, rgb(0x1b1e25));
    painter_.stroke_round(menu.box(), 10, rgb(theme::BorderStrong), 1);
    for (int index = 0; index < 4; ++index) {
        Rect item{menu.x + 4, menu.y + 4 + index * 32.0f, menu.w - 8, 32};
        Id id = static_cast<Id>(static_cast<int>(Id::Scale1) + index);
        if (hover_ == id) painter_.fill_round(item.box(), 6, rgb(0x2b3340));
        painter_.text(scale_label(index + 1),
                      RectF(item.x + 10, item.y, item.x + item.w - 10, item.y + item.h),
                      Face::Small,
                      settings_.scale == index + 1 ? rgb(theme::Accent) : rgb(theme::Text),
                      TextAlign::Leading, TextVAlign::Center,
                      false);
        register_hit(id, item);
    }
}

// -- pieces ----------------------------------------------------------------

void App::button(Id id, const Rect& rect, const std::wstring& label, bool primary,
                 bool enabled, bool danger, Face face) {
    bool hovered = enabled && hover_ == id;
    bool down = enabled && pressed_ == id;
    Color background;
    Color border;
    Color text;
    if (primary) {
        unsigned base = danger ? 0xa8402c : theme::Accent;
        unsigned hover = danger ? 0xbb4b35 : theme::AccentHover;
        background = enabled ? rgb(hovered ? hover : base) : rgb(0x513427);
        border = background;
        text = enabled ? rgb(0xffffff) : rgb(0xa08a7c);
    } else {
        background = !enabled ? rgb(0x1a1d23)
                              : rgb(hovered ? (down ? 0x20252d : 0x2c333e) : 0x242932);
        border = !enabled ? rgb(0x242932) : rgb(theme::BorderStrong);
        text = !enabled ? rgb(0x69707d)
                        : (danger ? rgb(0xff9a86) : rgb(theme::Text));
    }
    painter_.fill_round(rect.box(), 8, background);
    painter_.stroke_round(rect.box(), 8, border, 1);
    painter_.text_clipped(label, RectF(rect.x + 6, rect.y, rect.x + rect.w - 6,
                                             rect.y + rect.h),
                          face, text, TextAlign::Center);
    if (enabled) register_hit(id, rect);
}

void App::link(Id id, const Rect& rect, const std::wstring& label, bool enabled) {
    bool hovered = enabled && hover_ == id;
    bool down = enabled && pressed_ == id;
    Color text = enabled ? rgb(hovered ? (down ? 0x8fb4ff : 0xb6cdff) : theme::Link)
                         : rgb(0x69707d);
    painter_.text_clipped(label, RectF(rect.x, rect.y, rect.x + rect.w, rect.y + rect.h),
                          Face::Tiny, text, TextAlign::Center);
    if (enabled) register_hit(id, rect);
}

void App::checkbox(Id id, float x, float y, const std::wstring& label, bool checked,
                   bool enabled) {
    Rect box{x, y, 17, 17};
    Color border = enabled ? (checked ? rgb(theme::Accent) : rgb(theme::BorderStrong))
                           : rgb(0x2c313a);
    if (checked) {
        painter_.fill_round(box.box(), 4, enabled ? rgb(theme::Accent) : rgb(0x513427));
    } else {
        painter_.fill_round(box.box(), 4, rgb(0x1b1e25));
    }
    painter_.stroke_round(box.box(), 4, border, 1);
    if (checked) {
        painter_.line(box.x + 4, box.y + 8.5f, box.x + 7, box.y + 12, rgb(0xffffff), 1.8f);
        painter_.line(box.x + 7, box.y + 12, box.x + 13, box.y + 5, rgb(0xffffff), 1.8f);
    }
    float label_width = text_width(label, Face::Small);
    painter_.text(label, RectF(x + 24, y - 2, x + 24 + label_width + 8, y + 20),
                  Face::Small, enabled ? rgb(theme::Text) : rgb(0x69707d));
    if (enabled) register_hit(id, Rect{x, y - 4, 24 + label_width + 4, 25});
}

void App::scale_combo(Id id, const Rect& rect, bool enabled) {
    bool hovered = enabled && hover_ == id;
    painter_.fill_round(rect.box(), 8, rgb(hovered ? 0x2c333e : 0x242932));
    painter_.stroke_round(rect.box(), 8, rgb(theme::BorderStrong), 1);
    painter_.text(scale_label(settings_.scale),
                  RectF(rect.x + 10, rect.y, rect.x + rect.w - 26, rect.y + rect.h),
                  Face::Small, enabled ? rgb(theme::Text) : rgb(0x69707d),
                  TextAlign::Leading, TextVAlign::Center, false);
    float chevron_x = rect.x + rect.w - 16;
    float chevron_y = rect.y + rect.h / 2 - 2;
    painter_.line(chevron_x - 4, chevron_y, chevron_x, chevron_y + 4, rgb(theme::Muted), 1.6f);
    painter_.line(chevron_x, chevron_y + 4, chevron_x + 4, chevron_y, rgb(theme::Muted), 1.6f);
    if (enabled) register_hit(id, rect);
}

void App::pill(const Rect& rect, const std::wstring& text, Pill state) {
    Color dot = rgb(theme::Muted);
    switch (state) {
        case Pill::Ok: dot = rgb(theme::Ok); break;
        case Pill::Warn: dot = rgb(theme::Warn); break;
        case Pill::Bad: dot = rgb(theme::Bad); break;
        case Pill::Muted: break;
    }
    painter_.circle(rect.x + 7, rect.y + rect.h / 2, 3.5f, dot);
    painter_.text(text, RectF(rect.x + 16, rect.y, rect.x + rect.w, rect.y + rect.h),
                  Face::Small, rgb(0xcfd5e2), TextAlign::Leading,
                  TextVAlign::Center, false);
}

void App::card_box(const Rect& rect) {
    painter_.fill_round(rect.box(), 14, rgb(theme::Card));
    painter_.stroke_round(rect.box(), 14, rgb(theme::Border), 1);
}

float App::text_width(const std::wstring& text, Face face) {
    return painter_.measure(text, face);
}

std::wstring App::elide(const std::wstring& text, Face face, float width) {
    if (width <= 0) return L"";
    if (text_width(text, face) <= width) return text;
    std::wstring suffix = L"…";
    size_t low = 0, high = text.size();
    while (low < high) {
        size_t mid = (low + high + 1) / 2;
        if (text_width(text.substr(0, mid) + suffix, face) <= width) {
            low = mid;
        } else {
            high = mid - 1;
        }
    }
    return text.substr(0, low) + suffix;
}

void App::register_hit(Id id, const Rect& rect) { hits_.push_back({rect, id}); }

Id App::hit_at(float x, float y) const {
    for (auto it = hits_.rbegin(); it != hits_.rend(); ++it) {
        if (it->rect.contains(x, y)) return it->id;
    }
    return Id::None;
}

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
    painter_.text(elide(picker_path_, Face::Small, field.w - 24),
                  RectF(field.x + 10, field.y, field.x + field.w - 10, field.y + field.h),
                  Face::Small, rgb(theme::Text), TextAlign::Leading, TextVAlign::Center,
                  false);
    register_hit(Id::PickerPath, field);
    button(Id::PickerUp, Rect{field.x + field.w + 10, field.y, 66, 34}, L"Up", false, true);

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
        } else if (hover_ == id) {
            painter_.fill_round(item.box(), 6, rgb(0x222732));
        }
        std::wstring label = entry.name;
        if (entry.directory) label = entry.name == L".." ? L"Parent folder" : label + L"/";
        painter_.text(label, RectF(item.x + 10, item.y, item.x + item.w - 10,
                                  item.y + item.h),
                      Face::Small, entry.directory ? rgb(0xc8cede) : rgb(theme::Text),
                      TextAlign::Leading, TextVAlign::Center, false);
        register_hit(id, item);
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

    button(Id::PickerCancel,
           Rect{panel.x + panel.w - 24 - 100, panel.y + panel.h - 52, 100, 34}, L"Cancel",
           false, true);
    button(Id::PickerAccept,
           Rect{panel.x + panel.w - 24 - 100 - 8 - 176, panel.y + panel.h - 52, 176, 34},
           picker_ == PickerKind::Folder ? L"Use This Folder" : L"Choose Image", true, true);
}

}  // namespace nla
