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


}  // namespace

void App::paint() {
    if (!painter_.begin()) return;
    painter_.clear(rgb(theme::Background));
    ui_.begin_frame();

    const PaintContext ctx = canvas();
    paint_header();
    paint_games();

    if (overlay_.is(OverlayPage::Settings)) {
        paint_settings();
    } else if (overlay_.is_overlay()) {
        paint_overlay();
    }
    if (picker_.chooser().is_open()) picker_.paint(ctx);
    if (overlay_.scale_menu()) paint_scale_menu();
    if (players_.menu_open()) players_.paint_menu(ctx);

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
    if (update_.state() == UpdateScreen::State::Available && !update_.version().empty()) {
        std::wstring label = L"Update " + update_.version();
        float badge_w = ui_.text_width(label, Face::Button) + 30;
        Rect badge{settings_button.x - 8 - badge_w, 24, badge_w, 34};
        ui_.button(Id::UpdateBanner, badge, label, true, !busy_);
        title_right = std::min(title_right, badge.x - 16);
    }

    painter_.text(games_.current_title(), RectF(82, 14, title_right, 46), Face::Title,
                  rgb(theme::Text));
    painter_.text(kSubtitle, RectF(83, 48, title_right, 68), Face::Subtitle,
                  rgb(theme::Muted));

    painter_.line(24, 86, width_ - 24, 86, rgb(theme::Border));
}

void App::paint_games() {
    const PaintContext ctx = canvas();
    constexpr float kHeaderBottom = 86.0f;
    auto games = games_.available_games();
    bool compact = activity_.visible();
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
    } else if (!state_.project()) {
        title = L"Set up your game folder";
        detail = L"Pick a folder and the launcher writes the game folders into it, then "
                 L"extracts your disc. It remembers the choice.";
        pill_text = L"setup";
        pill_state = Pill::Warn;
    } else if (!state_.status().disc.extracted) {
        title = L"Choose your disc image";
        detail = L"Click a cover and pick your own disc image. The launcher extracts the "                 L"files the build and runtime read; nothing from the disc is included in "
                 L"the launcher.";
        pill_text = L"needs disc";
        pill_state = Pill::Warn;
    } else if (!state_.status().disc.verified) {
        title = L"This is not the supported disc";
        detail = L"The extracted main.dol does not match the supported USA release. Click "
                 L"the cover and pick the correct image.";
        pill_text = L"wrong disc";
        pill_state = Pill::Bad;
    } else if (!state_.status().built) {
        title = L"Ready to build";
        detail = L"A cover builds that game first, then starts it. A first build takes a "
                 L"few minutes.";
        pill_text = L"not built";
        pill_state = Pill::Warn;
    } else if (state_.status().stale_code) {
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
    auto progress_bar = [this, &ctx](const Rect& bar) {
        activity_.paint_bar(ctx, bar);
    };
    // What the page offers once a game is picked: one button to start it, and one for each
    // way of changing what it plays -- the mods, and the Gecko codes chosen on their own. A
    // cover only chooses the game, so nothing starts by accident.
    //
    // Play asks for a disc when there is no verified tree, which is what launch() does: the
    // button says so rather than starting and then asking.
    const std::wstring play_label =
        (!state_.project() || !state_.status().disc.verified) ? L"Set up the disc\u2026" : L"Play";
    // The mods and the codes change what an extracted disc plays. Before there is one there
    // is nothing to change, so the page offers Play on its own.
    const bool changeable = state_.project() != std::nullopt && state_.status().disc.extracted;

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
        bool selected = state_.project() && state_.project()->game() == game.folder;
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
        activity_.paint(ctx, top + block_h + 26.0f, busy_, job_.running());
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

    const Cover* art = games_.cover(game.folder);
    if (art && art->loaded) {
        painter_.draw_image(box, art->pixels.data(), art->width, art->height, kRadius);
    } else {
        // Without art of its own, the tile still names the game and says where the cover
        // goes, so the page works on a fresh checkout.
        const Cover& fallback = games_.placeholder_tile();
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
    ui_.combo(Id::Scale, Rect{left + 96, y, 180, 32}, scale_label(state_.settings().scale), !busy_);
    y += 36;
    separator();

    section(L"UPDATES");
    // The words are the screen's; only the colour they are read in is the page's.
    const std::wstring update_line = update_.line();
    Color update_color = rgb(theme::Muted);
    switch (update_.state()) {
        case UpdateScreen::State::UpToDate:
            update_color = rgb(theme::Ok);
            break;
        case UpdateScreen::State::Available:
            update_color = rgb(theme::Accent);
            break;
        case UpdateScreen::State::Failed:
            update_color = rgb(theme::Warn);
            break;
        case UpdateScreen::State::Checking:
        case UpdateScreen::State::Never:
            break;
    }
    painter_.text(ui_.elide(update_line, Face::Small, content_w - 250),
                  RectF(left, y + 7, left + content_w - 250, y + 29), Face::Small,
                  update_color);
    ui_.button(Id::SettingsCheckNow, Rect{right - 100, y, 100, 32}, L"Check now", false,
           !busy_ && update_.state() != UpdateScreen::State::Checking);
    if (update_.state() == UpdateScreen::State::Available) {
        ui_.button(Id::SettingsInstallUpdate, Rect{right - 100 - 8 - 128, y, 128, 32},
               L"Install update", true, !busy_);
    }
    y += 36;
    ui_.checkbox(Id::SettingsToggleUpdates, left, y + 4, L"Check when the launcher starts",
             state_.settings().check_updates, !busy_);
    y += 24;

    if (!settings_note_.empty()) {
        painter_.text(settings_note_, RectF(left, y, left + content_w, y + 22),
                      Face::Tiny, rgb(theme::Ok));
    }

    ui_.button(Id::SettingsClose, Rect{right - 110, panel.y + panel.h - 54, 110, 36}, L"Done",
           true, true);
}

void App::paint_overlay() {
    const PaintContext ctx = canvas();
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
        geckos_.set_rows_fit(static_cast<std::size_t>(fit));
    }
    // A message is as tall as what it says: the fixed 620×336 panel left half of itself
    // empty for a short prompt, and would have cut a long one off at the bottom.
    const float message_lines =
        static_cast<float>(wrapped_lines(painter_, overlay_.text(), Face::Body,
                                         panel_w - 52.0f));
    float panel_h = overlay_.is(OverlayPage::Players) ? 168.0f + 34.0f * kPads
                    : overlay_.is(OverlayPage::Mods)
                        ? 250.0f + 34.0f * static_cast<float>(mods_.selection().rows())
                    : overlay_.is(OverlayPage::Geckos)
                        ? 308.0f + 22.0f * static_cast<float>(geckos_.codes().rows(geckos_.rows_fit()))
                        : std::clamp(60.0f + message_lines * painter_.line_height(Face::Body) +
                                         16.0f + 56.0f + 24.0f,
                                     220.0f, height_ - 80.0f);
    Rect panel{(width_ - panel_w) / 2, (height_ - panel_h) / 2, panel_w, panel_h};

    painter_.fill_round(panel.box(), 16, rgb(0x171a21));
    painter_.stroke_round(panel.box(), 16, rgb(theme::BorderStrong), 1);

    painter_.text(overlay_.title(), RectF(panel.x + 26, panel.y + 20, panel.x + panel.w - 26,
                                              panel.y + 50),
                  Face::CardTitle, rgb(theme::Text));
    if (overlay_.is(OverlayPage::Players)) {
        players_.paint_table(ctx, panel, busy_);
    } else if (overlay_.is(OverlayPage::Mods)) {
        mods_.paint_table(ctx, panel, busy_);
    } else if (overlay_.is(OverlayPage::Geckos)) {
        geckos_.paint_table(ctx, panel, busy_);
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
                      state_.settings().scale == index + 1 ? rgb(theme::Accent) : rgb(theme::Text),
                      TextAlign::Leading, TextVAlign::Center,
                      false);
        ui_.register_hit(id, item);
    }
}

// -- pieces ----------------------------------------------------------------


}  // namespace nla
