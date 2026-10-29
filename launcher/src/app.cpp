#include "app.h"

#include "disc.h"
#include "git.h"
#include "image.h"
#include "shortcut.h"
#include "tools_package.h"
#include "util.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <thread>

namespace nla {
namespace {

constexpr wchar_t kTitle[] = L"Naruto: Clash of Ninja";
constexpr wchar_t kSubtitle[] = L"GameCube static recompilation";

std::wstring scale_label(int factor) {
    return format(L"%d×   %d × %d", factor, 640 * factor, 480 * factor);
}

bool is_disc_image(const fs::path& path) {
    for (const wchar_t* extension :
         {L".iso", L".ciso", L".rvz", L".wia", L".gcz", L".wbfs", L".tgc", L".nfs"}) {
        if (ends_with_ci(path.wstring(), extension)) return true;
    }
    return false;
}

std::wstring picker_join(const std::wstring& folder, const std::wstring& name) {
    return (fs::path(folder) / name).wstring();
}

}  // namespace

App::~App() {
    // A check in flight posts its answer to the dispatcher, so it must be finished before
    // anything it touches goes away. The request has a short timeout of its own.
    if (update_thread_.joinable()) update_thread_.join();
    job_.shutdown();
    game_.shutdown();
}

bool App::create() {
    settings_ = Settings::load();
    tools_ = check_tools();
    // What an earlier update could not remove once its helper had finished.
    clean_up_after_update();

    auto root = Project::locate(settings_.project_root);
    if (root) project_.emplace(*root, settings_.game);
    if (project_) settings_.game = project_->game();

    std::wstring error;
    std::wstring window_title = current_title() + L" — Launcher";
    if (!window_.create(window_title, settings_.window_width, settings_.window_height,
                        &error)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Naruto: Clash of Ninja",
                                 narrow(error).c_str(), nullptr);
        return false;
    }
    window_.set_min_size(940, 700);
    dispatcher_.initialise();

    if (!painter_.initialise(
            [this](const std::uint32_t* pixels, int pixel_width, int pixel_height) {
                window_.present(pixels, pixel_width, pixel_height);
            },
            &error)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Naruto: Clash of Ninja",
                                 narrow(error).c_str(), window_.handle());
        return false;
    }

    resize_surface();
    refresh();
    window_.show();
    invalidate();

    if (settings_.check_updates) start_update_check(false);

    if (!project_) {
        append_log(L"[launcher] project folder not located yet; Launch the game asks "
                   L"for it.\n");
    } else if (!status_.disc.verified) {
        append_log(L"[launcher] Launch the game asks for your own disc image, then "
                   L"extracts and builds the game.\n");
    }
    return true;
}

int App::run() {
    bool running = true;
    while (running && !quit_) {
        animate();
        if (window_.take_invalidated()) paint();

        SDL_Event event;
        int timeout = (busy_ && progress_ < 0) ? 16 : 250;
        if (!window_.wait(event, timeout)) {
            dispatcher_.drain();
            continue;
        }
        do {
            if (event.type == SDL_QUIT) {
                running = false;
                break;
            }
            handle_event(event);
        } while (window_.poll(event));
        dispatcher_.drain();
    }

    int pixel_width = 0, pixel_height = 0;
    window_.pixel_size(&pixel_width, &pixel_height);
    float scale = window_.scale();
    settings_.window_width = static_cast<int>(std::lround(pixel_width / scale));
    settings_.window_height = static_cast<int>(std::lround(pixel_height / scale));
    save_settings();
    job_.shutdown();
    game_.shutdown();
    window_.destroy();
    return 0;
}

void App::resize_surface() {
    int pixel_width = 0, pixel_height = 0;
    window_.pixel_size(&pixel_width, &pixel_height);
    float scale = window_.scale();
    painter_.resize(pixel_width, pixel_height, scale);
    width_ = static_cast<float>(pixel_width) / scale;
    height_ = static_cast<float>(pixel_height) / scale;
    invalidate();
}

void App::animate() {
    if (!busy_ || progress_ >= 0) return;
    std::uint32_t now = SDL_GetTicks();
    if (now < next_frame_) return;
    next_frame_ = now + 33;
    progress_phase_ += 0.025f;
    if (progress_phase_ > 1.0f) progress_phase_ -= 1.0f;
    invalidate();
}

void App::handle_event(const SDL_Event& event) {
    if (dispatcher_.event_type() && event.type == dispatcher_.event_type()) {
        dispatcher_.drain();
        return;
    }

    const float factor = window_.pointer_scale();

    switch (event.type) {
        case SDL_WINDOWEVENT:
            switch (event.window.event) {
                case SDL_WINDOWEVENT_SIZE_CHANGED:
                case SDL_WINDOWEVENT_RESIZED:
                case SDL_WINDOWEVENT_DISPLAY_CHANGED:
                    resize_surface();
                    break;
                case SDL_WINDOWEVENT_EXPOSED:
                    invalidate();
                    break;
                case SDL_WINDOWEVENT_FOCUS_GAINED:
                    refresh();
                    break;
                case SDL_WINDOWEVENT_LEAVE:
                    if (hover_ != Id::None || log_hovered_) {
                        hover_ = Id::None;
                        log_hovered_ = false;
                        invalidate();
                    }
                    break;
                default:
                    break;
            }
            return;
        case SDL_MOUSEMOTION: {
            float x = static_cast<float>(event.motion.x) / factor;
            float y = static_cast<float>(event.motion.y) / factor;
            Id hit = hit_at(x, y);
            bool over_log = log_visible_ && log_rect_.contains(x, y);
            if (hit != hover_ || over_log != log_hovered_) {
                hover_ = hit;
                log_hovered_ = over_log;
                invalidate();
            }
            window_.set_hand_cursor(hit != Id::None);
            return;
        }
        case SDL_MOUSEBUTTONDOWN:
            if (event.button.button != SDL_BUTTON_LEFT) return;
            pressed_ = hit_at(static_cast<float>(event.button.x) / factor,
                              static_cast<float>(event.button.y) / factor);
            invalidate();
            return;
        case SDL_MOUSEBUTTONUP: {
            if (event.button.button != SDL_BUTTON_LEFT) return;
            Id hit = hit_at(static_cast<float>(event.button.x) / factor,
                            static_cast<float>(event.button.y) / factor);
            Id pressed = pressed_;
            pressed_ = Id::None;
            if (pressed != Id::None && pressed == hit) activate(hit);
            invalidate();
            return;
        }
        case SDL_MOUSEWHEEL: {
            int mouse_x = 0, mouse_y = 0;
            SDL_GetMouseState(&mouse_x, &mouse_y);
            float x = static_cast<float>(mouse_x) / factor;
            float y = static_cast<float>(mouse_y) / factor;
            if (picker_ != PickerKind::None) {
                picker_offset_ = std::max(0, picker_offset_ - event.wheel.y);
                invalidate();
            } else if (log_visible_ && log_rect_.contains(x, y) && !log_.empty()) {
                log_offset_ = std::max(0, log_offset_ + (event.wheel.y > 0 ? 3 : -3));
                invalidate();
            }
            return;
        }
        case SDL_KEYDOWN:
            handle_key(event.key);
            return;
        case SDL_TEXTINPUT:
            handle_text(event.text);
            return;
        case SDL_DROPFILE: {
            fs::path dropped(widen(event.drop.file));
            SDL_free(event.drop.file);
            std::error_code ec;
            if (dropped.empty()) return;
            if (fs::is_directory(dropped, ec)) {
                Project candidate(dropped, settings_.game);
                if (candidate.valid()) {
                    project_.emplace(dropped, settings_.game);
                    covers_.clear();
                    settings_.project_root = candidate.root().wstring();
                    settings_.game = project_->game();
                    play_status_.clear();
                    save_settings();
                    auto text = dropped.wstring();
                    append_log(format(L"[launcher] project: %ls\n", text.c_str()));
                    window_.set_title(current_title() + L" — Launcher");
                    refresh();
                    if (launch_wanted_) {
                        launch_wanted_ = false;
                        launch();
                    }
                } else {
                    auto text = dropped.wstring();
                    show_message(L"Not a project folder",
                                 std::wstring(L"That folder does not hold the game "
                                              L"folders and tools/extract_disc.py:\n\n") +
                                     text);
                }
            } else if (is_disc_image(dropped)) {
                start_setup(dropped);
            }
            return;
        }
        default:
            return;
    }
}

void App::handle_key(const SDL_KeyboardEvent& key) {
    if (key.repeat && picker_ == PickerKind::None) return;
    const bool command = (key.keysym.mod & (KMOD_CTRL | KMOD_GUI)) != 0;

    switch (key.keysym.sym) {
        case SDLK_ESCAPE:
            if (picker_ != PickerKind::None) {
                launch_wanted_ = false;
                close_picker();
            } else if (scale_open_) {
                scale_open_ = false;
                invalidate();
            } else if (overlay_open_) {
                // Escape out of the players panel lands back in Settings, which is where
                // it was opened from; the other panels are all there is under them.
                const bool back_to_settings = overlay_players_;
                close_overlay();
                if (back_to_settings) open_settings();
            } else if (settings_open_) {
                settings_open_ = false;
                invalidate();
            }
            return;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            if (picker_ != PickerKind::None) accept_picker();
            return;
        case SDLK_TAB:
            if (picker_ != PickerKind::None) {
                picker_editing_ = !picker_editing_;
                invalidate();
            }
            return;
        case SDLK_BACKSPACE:
            if (picker_ != PickerKind::None && picker_editing_ && !picker_path_.empty()) {
                picker_path_.pop_back();
                // A UTF-16 surrogate pair must not be split.
                if (sizeof(wchar_t) == 2 && !picker_path_.empty() &&
                    picker_path_.back() >= 0xD800 && picker_path_.back() <= 0xDBFF) {
                    picker_path_.pop_back();
                }
                invalidate();
            }
            return;
        case SDLK_v:
            if (command && picker_ != PickerKind::None) {
                auto text = window_.clipboard();
                if (!text.empty()) {
                    picker_editing_ = true;
                    picker_path_ += text;
                    invalidate();
                }
            }
            return;
        case SDLK_UP:
            if (picker_ != PickerKind::None && !picker_entries_.empty()) {
                picker_selected_ = std::max(0, picker_selected_ - 1);
                invalidate();
            }
            return;
        case SDLK_DOWN:
            if (picker_ != PickerKind::None && !picker_entries_.empty()) {
                picker_selected_ = std::min(static_cast<int>(picker_entries_.size()) - 1,
                                            picker_selected_ + 1);
                invalidate();
            }
            return;
        default:
            return;
    }
}

void App::handle_text(const SDL_TextInputEvent& text) {
    if (picker_ == PickerKind::None) return;
    picker_editing_ = true;
    picker_path_ += widen(text.text);
    invalidate();
}

namespace {

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

int App::render_to(const std::string& path, float scale, const std::string& view) {
    rendering_ = true;
    settings_ = Settings::load();
    tools_ = check_tools();
    if (auto root = Project::locate(settings_.project_root)) project_.emplace(*root, settings_.game);
    if (project_) settings_.game = project_->game();
    status_ = project_ ? project_->status() : ProjectStatus{};
    append_log(L"[launcher] render check");

    std::wstring error;
    if (!painter_.initialise(nullptr, &error)) {
        std::fprintf(stderr, "%s\n", narrow(error).c_str());
        return 1;
    }
    float factor = scale > 0.1f ? scale : 1.0f;
    width_ = 1060;
    height_ = 820;
    painter_.resize(static_cast<int>(width_ * factor), static_cast<int>(height_ * factor),
                    factor);

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
        title = L"Find the game folder";
        detail = L"Put the launcher beside the project folder, or pick it once here: the "
                 L"launcher remembers the choice.";
        pill_text = L"setup";
        pill_state = Pill::Warn;
    } else if (!status_.disc.extracted) {
        title = L"Choose your disc image";
        detail = L"Click a cover and pick your own disc image. The launcher extracts the "
                 L"files the build and runtime read; nothing from the disc is included in "
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
    } else {
        title = L"Ready to play";
        detail = L"Click a cover to start that game. Escape quits in the game.";
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
        button(Id::Launch, Rect{center_x - 130, top + 152, 260, 50}, L"Find the game folder",
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
        draw_controls((status_left + status_right) / 2, top + 140,
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
    draw_controls(center_x, status_top + 130, 420.0f);
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
        painter_.text(L"Drop cover.png in " + game.folder + L"/",
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
            const float line_height = 16.0f;
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
    float panel_h = std::min(640.0f, height_ - 40);
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

    section(L"PROJECT");
    std::wstring root_text = project_ ? elide(project_->root().wstring(), Face::Small,
                                              content_w - 220)
                                      : L"not located";
    painter_.text(root_text, RectF(left, y + 7, left + content_w - 220, y + 29),
                  Face::Small, project_ ? rgb(theme::Text) : rgb(theme::Warn));
    button(Id::SettingsProject, Rect{right - 110, y, 110, 32}, L"Change…", false, !busy_);
    button(Id::SettingsOpenProject, Rect{right - 110 - 8 - 84, y, 84, 32}, L"Open", false,
           project_.has_value());
    y += 36;

    std::wstring source_text;
    Color source_color = rgb(theme::Muted);
    if (!project_) {
        source_text = L"Game source: no project folder yet";
    } else if (!git_found_) {
        source_text = L"Game source: git was not found";
    } else if (!project_checkout_) {
        source_text = L"Game source: this folder is not a git checkout";
    } else if (source_revision_.empty()) {
        source_text = L"Game source: git could not read this folder";
    } else if (project_behind_) {
        source_text = L"Game source " + source_revision_ +
                      L" is older than this launcher — update it, then Rebuild";
        source_color = rgb(theme::Warn);
    } else {
        source_text = L"Game source: " + source_revision_;
    }
    painter_.text(elide(source_text, Face::Small, content_w - 170),
                  RectF(left, y + 7, left + content_w - 170, y + 29), Face::Small,
                  source_color);
    button(Id::SettingsUpdateProject, Rect{right - 150, y, 150, 32}, L"Update project", false,
           !busy_ && git_found_ && project_checkout_);
    y += 36;
    separator();

    section(L"DISC IMAGE");
    std::wstring image_text = settings_.last_image.empty()
                                  ? L"no image chosen yet"
                                  : elide(settings_.last_image, Face::Small, content_w - 320);
    painter_.text(image_text, RectF(left, y + 7, left + content_w - 320, y + 29),
                  Face::Small, rgb(theme::Muted));
    button(Id::SettingsImage, Rect{right - 150, y, 150, 32}, L"Choose Image…", false,
           !busy_ && project_.has_value());
    button(Id::SettingsOpenDisc, Rect{right - 150 - 8 - 104, y, 104, 32}, L"Disc folder",
           false, !busy_ && project_.has_value());
    y += 36;
    separator();

    section(L"PLAY");
    painter_.text(L"Window scale", RectF(left, y + 8, left + 96, y + 28), Face::Small,
                  rgb(theme::Muted));
    scale_combo(Id::Scale, Rect{left + 96, y, 180, 32}, !busy_);
    checkbox(Id::ToggleMute, left + 300, y + 7, L"Mute music", settings_.mute_music, !busy_);
    checkbox(Id::ToggleInputLog, left + 408, y + 7, L"Input log", settings_.input_log,
             !busy_);
    // Who plays as whom, in its own panel: the console's four pads and one row each.
    button(Id::SettingsPlayers, Rect{right - 110, y, 110, 32}, L"Players…", false,
           !busy_);
    y += 36;
    separator();

    section(L"GAME FILES");
    button(Id::SettingsOpenSaves, Rect{left, y, 120, 32}, L"Saves folder", false, !busy_);
    button(Id::SettingsOpenControls, Rect{left + 128, y, 120, 32}, L"controls.txt", false,
           !busy_);
    button(Id::SettingsResetControls, Rect{left + 256, y, 128, 32}, L"Reset controls", false,
           !busy_, true);
    y += 36;
    separator();

    section(L"BUILD");
    std::wstring missing;
    for (const auto& tool : tools_) {
        if (tool.required && !tool.found()) {
            if (!missing.empty()) missing += L", ";
            missing += tool.name;
        }
    }
    std::wstring tools_line =
        missing.empty() ? L"All build tools found." : L"Missing " + missing + L".";
    bool offer_tools = !missing.empty() && tools_package_available();
    float reserve = offer_tools ? 500 : 360;
    painter_.text(elide(tools_line, Face::Small, content_w - reserve),
                  RectF(left, y + 7, left + content_w - reserve, y + 29), Face::Small,
                  missing.empty() ? rgb(theme::Ok) : rgb(theme::Warn));
    button(Id::SettingsRebuild, Rect{right - 100, y, 100, 32}, L"Rebuild", false,
           !busy_ && project_.has_value());
    button(Id::SettingsRescan, Rect{right - 100 - 8 - 78, y, 78, 32}, L"Rescan", false,
           true);
    button(Id::SettingsTools, Rect{right - 100 - 8 - 78 - 8 - 132, y, 132, 32},
           L"Open tools folder", false, true);
    if (offer_tools) {
        button(Id::SettingsFetchTools, Rect{right - 100 - 8 - 78 - 8 - 132 - 8 - 150, y, 150, 32},
               L"Get build tools", false, !busy_);
    }
    y += 36;
    separator();

    section(L"SHORTCUTS");
    button(Id::SettingsDesktopShortcut, Rect{left, y, 168, 32}, L"Desktop shortcut", false,
           true);
    button(Id::SettingsStartShortcut, Rect{left + 176, y, 190, 32},
           shortcut_folder_label(), false, true);
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
    float panel_w = overlay_players_ ? 660.0f : 620.0f;
    float panel_h = overlay_players_ ? 168.0f + 34.0f * static_cast<float>(1 + devices_.size())
                                     : 300.0f;
    Rect panel{(width_ - panel_w) / 2, (height_ - panel_h) / 2, panel_w, panel_h};
    painter_.fill_round(panel.box(), 16, rgb(0x171a21));
    painter_.stroke_round(panel.box(), 16, rgb(theme::BorderStrong), 1);

    painter_.text(overlay_title_, RectF(panel.x + 26, panel.y + 20, panel.x + panel.w - 26,
                                              panel.y + 50),
                  Face::CardTitle, rgb(theme::Text));
    if (overlay_players_) {
        // The keyboard, then each controller the game can see, and the pad each plays as.
        // The picks go to the game's controls.txt, which is what the runtime reads.
        float left = panel.x + 26;
        float right = panel.x + panel.w - 26;
        float y = panel.y + 60;
        painter_.text(L"A controller takes the first free pad unless it says otherwise here.",
                      RectF(left, y, right - 70, y + 20), Face::Tiny, rgb(theme::Muted));
        link(Id::SettingsRefreshDevices, Rect{right - 62, y, 62, 18}, L"Refresh");
        y += 26;
        auto pad_row = [&](Id id, const std::wstring& name, const std::wstring& value) {
            painter_.text(elide(name, Face::Small, right - left - 200),
                          RectF(left, y + 5, right - left - 200 + left, y + 27), Face::Small,
                          rgb(theme::Text));
            pad_combo(id, Rect{right - 190, y, 190, 28}, value, !busy_);
            y += 34;
        };
        pad_row(static_cast<Id>(static_cast<int>(Id::PadRow)), L"Keyboard",
                L"Player " + std::to_wstring(keyboard_pad_));
        for (size_t i = 0; i < devices_.size(); ++i) {
            const DeviceRow& device = devices_[i];
            std::wstring label = device.name;
            if (device.nth > 1) label += L" #" + std::to_wstring(device.nth);
            pad_row(static_cast<Id>(static_cast<int>(Id::PadRow) + 1 + static_cast<int>(i)),
                    label, device.pad == 0 ? L"Automatic"
                                           : L"Player " + std::to_wstring(device.pad));
        }
        if (devices_.empty()) {
            painter_.text(elide(devices_note_, Face::Small, right - left),
                          RectF(left, y + 4, right, y + 26), Face::Small, rgb(theme::Muted));
        }
    } else {
        painter_.text(overlay_text_, RectF(panel.x + 26, panel.y + 60, panel.x + panel.w - 26,
                                                 panel.y + 200),
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

void App::pad_combo(Id id, const Rect& rect, const std::wstring& value, bool enabled) {
    bool hovered = enabled && hover_ == id;
    painter_.fill_round(rect.box(), 8, rgb(hovered ? 0x2c333e : 0x242932));
    painter_.stroke_round(rect.box(), 8, rgb(theme::BorderStrong), 1);
    painter_.text(value, RectF(rect.x + 10, rect.y, rect.x + rect.w - 26, rect.y + rect.h),
                  Face::Small, enabled ? rgb(theme::Text) : rgb(0x69707d),
                  TextAlign::Leading, TextVAlign::Center, false);
    float chevron_x = rect.x + rect.w - 16;
    float chevron_y = rect.y + rect.h / 2 - 2;
    painter_.line(chevron_x - 4, chevron_y, chevron_x, chevron_y + 4, rgb(theme::Muted), 1.6f);
    painter_.line(chevron_x, chevron_y + 4, chevron_x + 4, chevron_y, rgb(theme::Muted), 1.6f);
    if (enabled) register_hit(id, rect);
}

// The pads a row can be set to: the keyboard always plays as one, a controller may be left
// to the first pad that is free, which is what "Automatic" writes.
void App::paint_pad_menu() {
    const bool keyboard = pad_menu_row_ == 0;
    const int count = keyboard ? 4 : 5;
    Rect menu{pad_menu_anchor_.x, pad_menu_anchor_.y + pad_menu_anchor_.h + 6, pad_menu_anchor_.w,
              count * 30.0f + 8};
    painter_.fill_round(menu.box(), 10, rgb(0x1b1e25));
    painter_.stroke_round(menu.box(), 10, rgb(theme::BorderStrong), 1);
    const int current = keyboard ? keyboard_pad_
                                 : devices_[static_cast<size_t>(pad_menu_row_ - 1)].pad;
    for (int i = 0; i < count; ++i) {
        const int pad = keyboard ? i + 1 : i;  // 0 is Automatic
        Rect item{menu.x + 4, menu.y + 4 + i * 30.0f, menu.w - 8, 30};
        Id id = static_cast<Id>(static_cast<int>(Id::PadOption) + i);
        if (hover_ == id) painter_.fill_round(item.box(), 6, rgb(0x2b3340));
        painter_.text(pad == 0 ? L"Automatic" : L"Player " + std::to_wstring(pad),
                      RectF(item.x + 10, item.y, item.x + item.w - 10, item.y + item.h),
                      Face::Small, pad == current ? rgb(theme::Accent) : rgb(theme::Text),
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

// -- state -----------------------------------------------------------------

void App::refresh() {
    if (project_) status_ = project_->status();
    invalidate();
}

bool App::check_required_tools() {
    std::wstring lines;
    for (const auto& tool : tools_) {
        if (tool.required && !tool.found()) {
            if (!lines.empty()) lines += L"\n";
            lines += L"  " + tool.name + L": " + tool.hint;
        }
    }
    if (lines.empty()) return true;
    append_log(L"[launcher] missing build tools:\n" + lines + L"\n");
    if (tools_package_available()) {
        show_confirm(L"Build tools needed",
                     L"The build needs these tools, which were not found on PATH or beside "
                     L"the launcher:\n\n" + lines + L"\n\nThe launcher can download them (" +
                         tools_package_size() + L") into tools\\ beside itself.",
                     L"Download", [this] { install_tools(true); });
        return false;
    }
    show_message(L"Missing build tools",
                 L"The build needs these tools, which were not found on PATH or beside "
                 L"the launcher:\n\n" + lines);
    return false;
}

void App::install_tools(bool then_launch) {
    if (busy_ || !tools_package_available()) return;
    append_log(L"[launcher] fetching the build tools\n");
    start_job(tools_package_tasks(), then_launch);
}

void App::start_job(std::vector<Task> tasks, bool then_launch, bool exits,
                    std::function<void(int)> done) {
    if (busy_ || tasks.empty()) return;
    busy_ = true;
    job_then_launch_ = then_launch;
    job_exits_ = exits;
    job_done_ = std::move(done);
    progress_ = -1;
    stage_.clear();
    log_visible_ = true;
    log_offset_ = 0;
    next_frame_ = 0;
    job_.start(
        std::move(tasks),
        [this](const std::wstring& text) { post([this, text] { on_log(text); }); },
        [this](const std::wstring& text) { post([this, text] { on_stage(text); }); },
        [this](int percent) { post([this, percent] { on_progress(percent); }); },
        [this](int code) { post([this, code] { on_job_done(code); }); });
    refresh();
}

void App::on_log(const std::wstring& text) { append_log(text); }

void App::on_stage(const std::wstring& text) {
    stage_ = text;
    invalidate();
}

void App::on_progress(int percent) {
    progress_ = percent > 0 ? percent : -1;
    invalidate();
}

void App::on_job_done(int code) {
    busy_ = false;
    progress_ = -1;
    stage_.clear();
    bool then_launch = job_then_launch_;
    bool exits = job_exits_;
    job_then_launch_ = false;
    job_exits_ = false;
    if (code == 0) {
        append_log(L"[launcher] done\n");
        // A job can be the one that installed the tools, so look for them again.
        tools_ = check_tools();
        // Back to the clean one-button view when there is nothing to read.
        log_visible_ = false;
    } else if (code == 130) {
        append_log(L"[launcher] cancelled\n");
    } else {
        append_log(format(L"[launcher] failed (status %d); see the output above\n", code));
        show_message(L"The step did not finish",
                     L"See Activity for the command and the output it printed. The "
                     L"details there usually name the missing file or tool.");
    }
    refresh();
    if (code == 0 && then_launch) continue_launch();
    // The helper that swaps in the new version is waiting for this process to go.
    if (code == 0 && exits) quit_ = true;
    if (job_done_) {
        auto follow_up = std::move(job_done_);
        job_done_ = nullptr;
        follow_up(code);
    }
}

void App::append_log(const std::wstring& text) {
    std::wstring current;
    for (wchar_t character : text) {
        if (character == L'\r') continue;
        if (character == L'\n') {
            log_.push_back(current);
            current.clear();
        } else {
            current.push_back(character);
        }
    }
    if (!current.empty()) log_.push_back(current);
    while (log_.size() > 4000) log_.pop_front();
    invalidate();
}

void App::copy_log() {
    std::wstring text;
    for (const auto& line : log_) {
        text += line;
        text += L"\r\n";
    }
    window_.set_clipboard(text);
}

// -- actions ---------------------------------------------------------------

void App::activate(Id id) {
    if (id == Id::None) return;

    auto anchor_scale = [this] {
        for (auto it = hits_.rbegin(); it != hits_.rend(); ++it) {
            if (it->id == Id::Scale) {
                scale_anchor_ = it->rect;
                return;
            }
        }
    };

    if (picker_ != PickerKind::None) {
        int index = static_cast<int>(id) - static_cast<int>(Id::PickerEntry);
        if (id == Id::PickerCancel) {
            launch_wanted_ = false;
            close_picker();
        } else if (id == Id::PickerAccept) {
            accept_picker();
        } else if (id == Id::PickerUp) {
            fs::path folder(picker_folder_);
            std::error_code ec;
            if (folder.has_parent_path() && folder.parent_path() != folder) {
                picker_folder_ = folder.parent_path().wstring();
                picker_path_ = picker_folder_;
                picker_editing_ = false;
                list_picker();
                invalidate();
            }
        } else if (id == Id::PickerPath) {
            picker_editing_ = true;
            invalidate();
        } else if (index >= 0 && index < static_cast<int>(picker_entries_.size())) {
            const auto& entry = picker_entries_[static_cast<size_t>(index)];
            if (entry.directory) {
                fs::path folder(picker_folder_);
                fs::path next =
                    entry.name == L".." ? folder.parent_path() : (folder / entry.name);
                std::error_code ec;
                if (fs::is_directory(next, ec)) {
                    picker_folder_ = next.wstring();
                    picker_path_ = picker_folder_;
                    picker_editing_ = false;
                    list_picker();
                }
            } else {
                picker_selected_ = index;
                picker_path_ = picker_join(picker_folder_, entry.name);
                picker_editing_ = true;
            }
            invalidate();
        }
        return;
    }

    if (overlay_open_ && overlay_players_) {
        if (pad_menu_row_ >= 0) {
            const int row = pad_menu_row_;
            const bool keyboard = row == 0;
            const int picked = static_cast<int>(id) - static_cast<int>(Id::PadOption);
            if (id >= Id::PadOption && id < Id::PadOptionEnd && picked < (keyboard ? 4 : 5)) {
                const int pad = keyboard ? picked + 1 : picked;
                if (keyboard) keyboard_pad_ = pad;
                else devices_[static_cast<size_t>(row - 1)].pad = pad;
                pad_menu_row_ = -1;
                write_pad_pins();
                return;
            }
            pad_menu_row_ = -1;  // anything else closes the menu, then does its own work
        }
        if (id >= Id::PadRow && id < Id::PadRowEnd) {
            const int row = static_cast<int>(id) - static_cast<int>(Id::PadRow);
            if (row >= 0 && row < 1 + static_cast<int>(devices_.size())) {
                pad_menu_row_ = row;
                for (auto it = hits_.rbegin(); it != hits_.rend(); ++it) {
                    if (it->id == id) {
                        pad_menu_anchor_ = it->rect;
                        break;
                    }
                }
                invalidate();
                return;
            }
        }
        switch (id) {
            case Id::SettingsRefreshDevices:
                refresh_devices();
                invalidate();
                return;
            case Id::OverlayAction:
                // Back to Settings, which is where the panel was opened from.
                close_overlay();
                open_settings();
                return;
            default:
                return;
        }
    }

    if (overlay_open_) {
        if (id == Id::OverlayAction) {
            auto callback = overlay_callback_;
            close_overlay();
            if (callback) callback();
        } else if (id == Id::OverlayCancel) {
            // "Not now" on the update prompt is remembered for that version only, so a
            // later release asks again.
            if (overlay_update_ && !update_release_.version.empty()) {
                settings_.skipped_update = update_release_.version;
                save_settings();
            }
            close_overlay();
        }
        return;
    }

    if (settings_open_) {
        switch (id) {
            case Id::SettingsClose:
                settings_open_ = false;
                refresh();
                break;
            case Id::SettingsProject:
                settings_open_ = false;
                locate_project();
                return;
            case Id::SettingsOpenProject:
                if (project_) open_path(project_->root());
                return;
            case Id::SettingsImage:
                settings_open_ = false;
                choose_image();
                return;
            case Id::SettingsOpenDisc:
                if (project_) open_path(project_->disc());
                return;
            case Id::SettingsOpenSaves:
                if (project_) open_path(project_->saves());
                return;
            case Id::SettingsOpenControls:
                if (project_) {
                    std::error_code ec;
                    bool present = fs::exists(project_->controls(), ec);
                    open_path(present ? project_->controls() : project_->game_dir());
                }
                return;
            case Id::SettingsResetControls: reset_controls(); return;
            case Id::SettingsRebuild:
                settings_open_ = false;
                start_build();
                return;
            case Id::SettingsFetchTools:
                settings_open_ = false;
                install_tools(false);
                return;
            case Id::SettingsRescan:
                rescan_environment();
                tools_ = check_tools();
                // Covers the user dropped in since the launcher started.
                covers_.clear();
                settings_note_ = L"Tools rescanned.";
                break;
            case Id::SettingsTools: {
                fs::path dir = launcher_tools_dir();
                std::error_code ec;
                fs::create_directories(dir, ec);
                open_path(dir);
                return;
            }
            case Id::SettingsUpdateProject:
                update_project();
                return;
            case Id::SettingsToggleUpdates:
                settings_.check_updates = !settings_.check_updates;
                save_settings();
                break;
            case Id::SettingsCheckNow:
                start_update_check(true);
                return;
            case Id::SettingsInstallUpdate:
                settings_open_ = false;
                begin_update();
                return;
            case Id::SettingsDesktopShortcut: create_shortcut(false); break;
            case Id::SettingsStartShortcut: create_shortcut(true); break;
            case Id::Scale:
                scale_open_ = !scale_open_;
                if (scale_open_) anchor_scale();
                break;
            case Id::Scale1:
            case Id::Scale2:
            case Id::Scale3:
            case Id::Scale4:
                settings_.scale = static_cast<int>(id) - static_cast<int>(Id::Scale1) + 1;
                scale_open_ = false;
                save_settings();
                break;
            case Id::ToggleMute:
                settings_.mute_music = !settings_.mute_music;
                save_settings();
                break;
            case Id::ToggleInputLog:
                settings_.input_log = !settings_.input_log;
                save_settings();
                break;
            case Id::SettingsPlayers:
                open_players();
                return;
            default:
                break;
        }
        invalidate();
        return;
    }

    if (scale_open_) {
        if (id >= Id::Scale1 && id <= Id::Scale4) {
            settings_.scale = static_cast<int>(id) - static_cast<int>(Id::Scale1) + 1;
            scale_open_ = false;
            save_settings();
            invalidate();
            return;
        }
        scale_open_ = false;
    }

    switch (id) {
        case Id::Launch: launch(); break;
        case Id::Stop:
            if (job_.running()) {
                job_.cancel();
            } else {
                game_.stop();
            }
            break;
        case Id::LogToggle:
            log_visible_ = !log_visible_;
            break;
        case Id::CopyLog: copy_log(); break;
        case Id::ClearLog:
            log_.clear();
            log_offset_ = 0;
            break;
        case Id::OpenSettings: open_settings(); break;
        case Id::UpdateBanner: show_update_prompt(); break;
        case Id::Scale:
            scale_open_ = !scale_open_;
            if (scale_open_) anchor_scale();
            break;
        case Id::ToggleMute:
            settings_.mute_music = !settings_.mute_music;
            save_settings();
            break;
        case Id::ToggleInputLog:
            settings_.input_log = !settings_.input_log;
            save_settings();
            break;
        default: {
            // A cover on the main page: it stands for that game and starts it.
            int game_index = static_cast<int>(id) - static_cast<int>(Id::GameCover);
            auto games = available_games();
            if (game_index >= 0 && game_index < static_cast<int>(games.size()))
                launch_game(games[static_cast<size_t>(game_index)].folder);
            break;
        }
    }
    invalidate();
}

void App::launch() {
    if (busy_) return;
    if (game_.running()) {
        game_.stop();
        return;
    }
    if (!project_) {
        launch_wanted_ = true;
        locate_project();
        return;
    }
    refresh();
    if (!status_.disc.verified) {
        launch_wanted_ = true;
        choose_image();
        return;
    }
    continue_launch();
}

void App::continue_launch() {
    if (!project_ || busy_) return;
    refresh();
    if (!status_.disc.verified) return;
    if (status_.built) {
        play();
        return;
    }
    start_build(true);
}

void App::locate_project() {
    fs::path start = project_ ? project_->root() : home_dir();
    open_picker(PickerKind::Folder, L"Choose the folder that holds the game folders and tools/", start,
                [this](const fs::path& chosen) {
                    Project candidate(chosen, settings_.game);
                    auto text = chosen.wstring();
                    if (candidate.valid()) {
                        project_.emplace(chosen, settings_.game);
                        covers_.clear();
                        settings_.project_root = candidate.root().wstring();
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
    std::vector<GameInfo> out;
    if (!project_) return out;
    for (const auto& game : Project::games()) {
        std::error_code ec;
        if (fs::is_regular_file(project_->root() / game.folder / L"CMakeLists.txt", ec))
            out.push_back(game);
    }
    return out;
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
    for (const wchar_t* name : {L"cover.png", L"cover.jpg", L"cover.jpeg"}) {
        fs::path path = project_->root() / folder / name;
        if (!fs::is_regular_file(path, ec)) continue;
        art.loaded = load_image(path, &art.width, &art.height, art.pixels);
        break;
    }
    Cover& stored = covers_.emplace(key, std::move(art)).first->second;
    return stored.loaded ? &stored : nullptr;
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
    Project next(project_->root(), folder);
    if (!next.valid()) return;
    project_ = std::move(next);
    settings_.game = project_->game();
    play_status_.clear();
    save_settings();
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

void App::choose_image() {
    if (!project_) return;
    fs::path start =
        settings_.last_image.empty() ? project_->root() : fs::path(settings_.last_image);
    open_picker(PickerKind::Image, L"Choose your own disc image", start,
                [this](const fs::path& chosen) { start_setup(chosen); });
}

// -- the picker ------------------------------------------------------------

void App::open_picker(PickerKind kind, const std::wstring& title, const fs::path& start,
                      std::function<void(const fs::path&)> accept) {
    std::error_code ec;
    fs::path folder = start;
    if (!folder.empty() && !fs::is_directory(folder, ec)) folder = folder.parent_path();
    if (folder.empty() || !fs::is_directory(folder, ec)) folder = home_dir();
    picker_ = kind;
    picker_title_ = title;
    picker_accept_ = std::move(accept);
    picker_folder_ = folder.wstring();
    picker_path_ = picker_folder_;
    picker_editing_ = false;
    list_picker();
    settings_open_ = false;
    overlay_open_ = false;
    scale_open_ = false;
    window_.set_text_input(true);
    invalidate();
}

void App::close_picker() {
    picker_ = PickerKind::None;
    picker_accept_ = nullptr;
    picker_entries_.clear();
    picker_editing_ = false;
    picker_note_.clear();
    window_.set_text_input(false);
    invalidate();
}

void App::list_picker() {
    picker_entries_.clear();
    std::error_code ec;
    fs::path folder(picker_folder_);
    bool readable = fs::is_directory(folder, ec);
    if (readable && folder.has_parent_path() && folder.parent_path() != folder) {
        picker_entries_.push_back({L"..", true});
    }
    std::vector<PickerEntry> directories, files;
    if (readable) {
        for (const auto& entry : fs::directory_iterator(
                 folder, fs::directory_options::skip_permission_denied, ec)) {
            std::error_code item_ec;
            auto name = entry.path().filename().wstring();
            if (name.empty() || name[0] == L'.') continue;
            if (entry.is_directory(item_ec)) {
                directories.push_back({name, true});
            } else if (picker_ == PickerKind::Image && is_disc_image(entry.path())) {
                files.push_back({name, false});
            }
        }
    }
    auto by_name = [](const PickerEntry& left, const PickerEntry& right) {
        return to_lower(left.name) < to_lower(right.name);
    };
    std::sort(directories.begin(), directories.end(), by_name);
    std::sort(files.begin(), files.end(), by_name);
    picker_entries_.insert(picker_entries_.end(), directories.begin(), directories.end());
    picker_entries_.insert(picker_entries_.end(), files.begin(), files.end());
    picker_selected_ = -1;
    picker_offset_ = 0;
    picker_note_.clear();
}

void App::accept_picker() {
    if (picker_ == PickerKind::None) return;
    std::error_code ec;
    fs::path chosen;
    bool listed = picker_selected_ >= 0 &&
                  picker_selected_ < static_cast<int>(picker_entries_.size()) &&
                  !picker_entries_[static_cast<size_t>(picker_selected_)].directory;

    if (listed) {
        chosen = fs::path(picker_folder_) /
                 picker_entries_[static_cast<size_t>(picker_selected_)].name;
    } else if (!picker_path_.empty()) {
        chosen = fs::path(picker_path_);
        if (fs::is_directory(chosen, ec)) {
            picker_folder_ = chosen.wstring();
            picker_path_ = picker_folder_;
            picker_editing_ = false;
            list_picker();
            invalidate();
            return;
        }
    }
    if (chosen.empty()) {
        picker_note_ = L"Type a path, or choose one from the list.";
        invalidate();
        return;
    }
    if (picker_ == PickerKind::Folder) {
        if (!fs::is_directory(chosen, ec)) {
            picker_note_ = L"That folder does not exist.";
            invalidate();
            return;
        }
    } else if (!fs::is_regular_file(chosen, ec)) {
        picker_note_ = L"That file does not exist.";
        invalidate();
        return;
    }

    auto accept = picker_accept_;
    close_picker();
    if (accept) accept(chosen);
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

void App::start_setup(const fs::path& image) {
    if (!project_ || busy_) return;
    bool then_launch = launch_wanted_;
    launch_wanted_ = false;
    std::error_code ec;
    if (!fs::is_regular_file(image, ec)) {
        show_message(L"Disc image", L"No such file:\n\n" + image.wstring());
        return;
    }
    if (fs::exists(project_->dol(), ec)) {
        show_confirm(L"Replace the extracted disc?",
                     L"A disc is already extracted. Extract again from this image and "
                     L"replace it?",
                     L"Extract", [this, image, then_launch] { begin_setup(image, then_launch); });
        return;
    }
    begin_setup(image, then_launch);
}

void App::begin_setup(const fs::path& image, bool then_launch) {
    if (!project_ || busy_) return;
    settings_.last_image = image.wstring();
    save_settings();
    append_log(format(L"[launcher] image: %ls\n", image.c_str()));

    fs::path disc_dir = project_->disc();
    std::wstring disc_id = project_->disc_id();
    std::wstring expected = project_->expected_sha1();
    bool convert = needs_nodtool(image);

    auto converted = std::make_shared<std::wstring>();
    std::vector<Task> tasks;

    if (convert) {
        tasks.push_back({
            L"Converting the image to ISO…",
            [this, image, converted](Job& job) -> int {
                auto nodtool = find_tool(L"nodtool" + wstring(exe_suffix()));
                if (nodtool.empty()) {
                    job.log(L"[launcher] this image needs nodtool, which was not found. "
                            L"Install it with: cargo install --locked nodtool\n");
                    return 127;
                }
                fs::path output = fs::temp_directory_path() / L"naruto-launcher" / L"disc.iso";
                std::error_code ec;
                fs::create_directories(output.parent_path(), ec);
                fs::remove(output, ec);
                job.log(L"$ nodtool convert " + image.wstring() + L" " + output.wstring() +
                        L"\n");
                int code = job.run_process(nodtool, {L"convert", image.wstring(),
                                                     output.wstring()},
                                           project_ ? project_->root() : fs::path());
                if (code == 0) *converted = output.wstring();
                return code;
            }});
    }

    tasks.push_back({
        L"Extracting the disc…",
        [this, image, converted, convert, disc_dir, disc_id, expected](Job& job) -> int {
            fs::path source = convert ? fs::path(*converted) : image;
            if (source.empty()) {
                job.log(L"[launcher] the converted image is missing\n");
                return 1;
            }
            ExtractRequest request{source, disc_dir, disc_id, expected};
            auto progress = [&job](int percent, const std::wstring& stage_text) {
                job.progress(percent);
                job.stage(stage_text);
            };
            auto outcome = extract_disc(request, progress, job.cancel_flag());
            if (!outcome.ok) {
                job.log(L"[launcher] " + outcome.error + L"\n");
                return 1;
            }
            job.log(format(L"[launcher] %d files, %.1f MiB extracted\n", outcome.file_count,
                           static_cast<double>(outcome.bytes) / (1024.0 * 1024.0)));
            job.log(format(L"[launcher] main.dol %ls matches the supported disc\n",
                           outcome.dol_sha1.substr(0, 12).c_str()));
            if (convert) {
                std::error_code remove_ec;
                fs::remove(source, remove_ec);
            }
            return 0;
        }});

    start_job(std::move(tasks), then_launch);
}

std::vector<Task> App::build_tasks() {
    std::vector<Task> tasks;
    if (!project_) return tasks;
    auto cmake = find_tool(L"cmake" + wstring(exe_suffix()));
    auto ninja = find_tool(L"ninja" + wstring(exe_suffix()));
    if (cmake.empty() || ninja.empty()) return tasks;

    fs::path game_dir = project_->game_dir();
    fs::path build = project_->build_dir();
    fs::path root = project_->root();
    std::vector<std::wstring> configure = {
        L"-S", game_dir.wstring(), L"-B", build.wstring(), L"-G", L"Ninja",
        L"-DCMAKE_BUILD_TYPE=RelWithDebInfo",
    };
    auto sdl = bundled_sdl2_prefix();
    if (!sdl.empty()) configure.push_back(L"-DGCN_SDL2_PREFIX=" + sdl.wstring());

    // In a release, name the bundled interpreter so CMake does not prefer a Python
    // registered elsewhere on the machine (or the Microsoft Store alias).
    auto python = find_tool(L"python" + wstring(exe_suffix()));
    if (python.empty()) python = find_tool(L"python3" + wstring(exe_suffix()));
    if (!python.empty()) {
        auto python_key = to_lower(python);
        auto tools_key = to_lower(launcher_tools_dir().wstring());
        if (!tools_key.empty() && python_key.rfind(tools_key, 0) == 0)
            configure.push_back(L"-DPython3_EXECUTABLE=" + python);
    }
#ifdef _WIN32
    // The OpenXR loader (used by --vr) does not compile with MinGW-w64. Without MSVC's
    // cl.exe the launcher configures the game without the PC VR frontend, as the rest of
    // the desktop build does not need it.
    if (find_tool(L"cl.exe").empty()) configure.push_back(L"-DGCN_OPENXR=OFF");
#endif

    tasks.push_back({L"Configuring CMake…", [cmake, configure, root](Job& job) -> int {
                         job.log(L"$ " + command_line(cmake, configure) + L"\n");
                         return job.run_process(cmake, configure, root);
                     }});
    tasks.push_back({L"Compiling the game…", [ninja, build, root](Job& job) -> int {
                         std::vector<std::wstring> args = {L"-C", build.wstring()};
                         job.log(L"$ " + command_line(ninja, args) + L"\n");
                         return job.run_process(ninja, args, root);
                     }});
    return tasks;
}

void App::start_build(bool then_launch) {
    if (!project_ || busy_) return;
    if (!check_required_tools()) return;
    auto tasks = build_tasks();
    if (tasks.empty()) {
        show_message(L"Cannot build", L"cmake and ninja were not found.");
        return;
    }
    if (!status_.disc.extracted) {
        show_message(L"Build the game",
                     L"The build needs the extracted disc. Press Launch the game to choose "
                     L"your disc image first.");
        return;
    }
    start_job(std::move(tasks), then_launch);
}

void App::play() {
    if (!project_ || busy_ || game_.running()) return;
    if (!status_.built || !status_.disc.verified) {
        refresh();
        return;
    }
    std::vector<std::wstring> args = {
        L"--scale=" + std::to_wstring(settings_.scale),
    };
    if (settings_.mute_music) args.push_back(L"--no-music");
    if (!settings_.input_log) args.push_back(L"--no-input-log");
    // One pad per controller, and never fewer than the pads the picker names.
    int players = settings_.players;
    for (const auto& device : devices_) players = std::max(players, device.pad);
    args.push_back(L"--players=" + std::to_wstring(std::clamp(players, 1, 4)));
    args.push_back(L"--controls=" + project_->controls().wstring());

    settings_.project_root = project_->root().wstring();
    save_settings();
    append_log(L"\n$ " + command_line(project_->binary().wstring(), args) + L"\n");
    play_status_ = L"Starting…";
    bool started =
        game_.start(project_->binary().wstring(), args, project_->game_dir(),
                    [this](const std::wstring& text) {
                        post([this, text] { append_log(text); });
                    },
                    [this](int code, bool stopped) {
                        post([this, code, stopped] {
                            play_status_ = stopped
                                               ? L"Stopped from the launcher."
                                               : format(L"The game exited with status %d.",
                                                        code);
                            append_log(stopped
                                           ? L"[launcher] game stopped\n"
                                           : format(L"[launcher] game exited with status %d\n",
                                                    code));
                            refresh();
                        });
                    });
    if (!started) {
        play_status_.clear();
        show_message(L"The game did not start",
                     L"The launcher could not start the game. Build it first, then try "
                     L"again.");
    } else {
        play_status_ = L"Press Stop the game to end it.";
    }
    refresh();
}

void App::open_path(const fs::path& path) {
    if (path.empty()) return;
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        show_message(L"Not there yet", L"Nothing at:\n\n" + path.wstring());
        return;
    }
    std::wstring error;
    if (!nla::open_path(path, &error)) {
        show_message(L"Cannot open", error.empty() ? L"This system could not open:\n\n" +
                                                         path.wstring()
                                                   : error);
    }
}

// Who plays as whom: the keyboard and each controller the game can see, one row each.
void App::open_players() {
    refresh_devices();
    overlay_open_ = true;
    overlay_players_ = true;
    overlay_confirm_ = false;
    overlay_update_ = false;
    overlay_callback_ = nullptr;
    overlay_title_ = L"Players";
    overlay_action_ = L"Done";
    settings_open_ = false;
    scale_open_ = false;
    pad_menu_row_ = -1;
    invalidate();
}

void App::refresh_devices() {
    devices_.clear();
    keyboard_pad_ = 1;
    devices_note_.clear();
    // A stand-in list for the renders: the same two rows whatever this machine has
    // plugged in, so the drawing is the same everywhere it is checked.
    if (rendering_) {
        devices_ = {{L"Xbox Wireless Controller", 1, 0}, {L"PS4 Controller", 1, 2}};
        return;
    }
    std::error_code ec;
    // The game answers for itself: its own SDL sees the devices, and it names them exactly
    // as the pad pins in controls.txt do.
    if (!project_ || !fs::exists(project_->binary(), ec)) {
        devices_note_ = L"Build the game first: the launcher asks it which controllers it sees.";
        return;
    }
    std::string out;
    if (!capture_output(project_->binary().wstring(),
                        {L"--list-devices", L"--controls=" + project_->controls().wstring()}, &out))
        return;
    for (const auto& line : split(widen(out), L'\n')) {
        auto fields = split(line, L'\t');
        if (fields.size() >= 2 && fields[0] == L"keyboard") {
            keyboard_pad_ = std::clamp(std::atoi(narrow(fields[1]).c_str()), 1, 4);
        } else if (fields.size() >= 4 && fields[0] == L"device") {
            DeviceRow device;
            device.nth = std::atoi(narrow(fields[1]).c_str());
            device.pad = std::atoi(narrow(fields[2]).c_str());
            device.name = fields[3];
            devices_.push_back(device);
        }
    }
    if (devices_.empty()) devices_note_ = L"No controller found. Plug one in and press Refresh.";
}

// The pads this picker owns are the `pad = "<device>" <n>` and `keyboard_pad = <n>` lines.
// Everything else in controls.txt is the player's own mapping and is kept as written, and
// so is a pin for a controller that is unplugged right now: it is still who plays next
// time it is plugged in.
void App::write_pad_pins() {
    // A render checks the panel, not the player's disk: it runs with no game to read back
    // from, so its device list is a stand-in and must not reach controls.txt.
    if (!project_ || rendering_) return;
    const fs::path path = project_->controls();
    std::wstring text;
    std::error_code ec;
    if (fs::exists(path, ec)) text = read_text_file(path);
    std::map<std::wstring, int> pins;
    std::wstring kept;
    for (const auto& line : split(text, L'\n')) {
        auto words = split(trim(line), L' ');
        if (!words.empty() && words[0] == L"pad") {
            const size_t open = line.find(L'"', line.find(L"pad"));
            const size_t close = line.rfind(L'"');
            if (open != std::wstring::npos && close != std::wstring::npos && close > open) {
                const int pad = std::atoi(narrow(line.substr(close + 1)).c_str());
                if (pad >= 1 && pad <= 4) pins[line.substr(open + 1, close - open - 1)] = pad;
            }
            continue;
        }
        if (!words.empty() && words[0] == L"keyboard_pad") continue;
        kept += line;
        kept += L'\n';
    }
    for (const auto& device : devices_) {
        std::wstring label = device.name;
        if (device.nth > 1) label += L" #" + std::to_wstring(device.nth);
        if (device.pad == 0) pins.erase(label);
        else pins[label] = device.pad;
    }
    if (keyboard_pad_ != 1)
        kept += L"keyboard_pad = " + std::to_wstring(keyboard_pad_) + L"\n";
    int highest = 1;
    for (const auto& [label, pad] : pins) {
        kept += L"pad = \"" + label + L"\" " + std::to_wstring(pad) + L"\n";
        highest = std::max(highest, pad);
    }
    if (!write_text_file(path, narrow(kept))) {
        show_message(L"Player pads", L"The launcher could not write\n\n" + path.wstring());
        return;
    }
    // A pad past the cap would be ignored by the game, so the cap follows the picks.
    settings_.players = std::max(settings_.players, highest);
    save_settings();
    append_log(L"[launcher] players: keyboard is player " + std::to_wstring(keyboard_pad_) + L"\n");
    for (const auto& [label, pad] : pins)
        append_log(L"[launcher] players: \"" + label + L"\" is player " +
                   std::to_wstring(pad) + L"\n");
    refresh_devices();  // so the rows show what the game now reads back
    invalidate();
}

void App::reset_controls() {
    if (!project_) return;
    std::error_code ec;
    if (!fs::exists(project_->controls(), ec)) {
        show_message(L"Controls", L"There is no controls.txt; the game uses its built-in "
                                  L"mapping.");
        return;
    }
    show_confirm(L"Reset controls?",
                 L"Delete controls.txt and go back to the game's built-in keyboard and "
                 L"gamepad mapping?",
                 L"Reset", [this] {
                     std::error_code remove_ec;
                     fs::remove(project_->controls(), remove_ec);
                     append_log(L"[launcher] controls.txt removed\n");
                     refresh();
                 });
}

void App::show_message(const std::wstring& title, const std::wstring& text,
                       const std::wstring& action) {
    settings_open_ = false;
    overlay_open_ = true;
    overlay_confirm_ = false;
    overlay_title_ = title;
    overlay_text_ = text;
    overlay_action_ = action;
    overlay_callback_ = nullptr;
    overlay_update_ = false;
    overlay_players_ = false;
    scale_open_ = false;
    invalidate();
}

void App::show_confirm(const std::wstring& title, const std::wstring& text,
                       const std::wstring& action, std::function<void()> confirmed) {
    settings_open_ = false;
    overlay_open_ = true;
    overlay_confirm_ = true;
    overlay_title_ = title;
    overlay_text_ = text;
    overlay_action_ = action;
    overlay_callback_ = std::move(confirmed);
    overlay_update_ = false;
    overlay_players_ = false;
    scale_open_ = false;
    invalidate();
}

void App::close_overlay() {
    overlay_open_ = false;
    overlay_confirm_ = false;
    overlay_update_ = false;
    overlay_players_ = false;
    pad_menu_row_ = -1;
    overlay_callback_ = nullptr;
    invalidate();
}

void App::refresh_source_status() {
    git_found_ = !git_tool().empty();
    project_checkout_ = false;
    project_behind_ = false;
    source_revision_.clear();
    if (!project_) return;
    project_checkout_ = project_is_checkout(project_->root());
    if (!project_checkout_) return;
    source_revision_ = project_revision(project_->root());
    project_behind_ =
        git_found_ && project_is_older_than(project_->root(), launcher_source(),
                                            launcher_source_time());
}

void App::update_project() {
    if (!project_ || busy_) return;
    if (git_tool().empty()) {
        show_message(L"git was not found",
                     L"Updating the game source needs git, and it is not installed. Get the "
                     L"newest source yourself, then press Rebuild.");
        return;
    }
    if (!project_checkout_) {
        show_message(L"Not a git checkout",
                     L"This project folder is not a git checkout, so the launcher cannot "
                     L"update it. Download the newest source and point Settings at that "
                     L"folder.");
        return;
    }
    std::wstring reason;
    if (!project_is_clean(project_->root(), &reason)) {
        show_message(L"Cannot update the project", reason);
        return;
    }
    settings_open_ = false;
    start_job(source_tasks(project_->root()), false, false, [this](int code) {
        refresh_source_status();
        refresh();
        if (code != 0) return;
        settings_note_ = L"Game source updated.";
        if (status_.built) {
            // The binary on disk was built from the source that just changed.
            show_confirm(L"Game source updated",
                         L"The project is now at " + project_revision(project_->root()) +
                             L". Rebuild the game with the new source?",
                         L"Rebuild", [this] { start_build(); });
        } else {
            show_message(L"Game source updated",
                         L"Launch the game builds it with the new source.");
        }
    });
}

void App::start_update_check(bool manual) {
    if (update_state_ == UpdateState::Checking) return;
    if (!manual && !settings_.check_updates) return;
    if (development_build()) {
        update_state_ = UpdateState::Failed;
        update_error_ = L"This build was not made from a release tag.";
        if (manual) show_message(L"Updates", update_error_);
        invalidate();
        return;
    }
    if (update_thread_.joinable()) update_thread_.join();
    update_state_ = UpdateState::Checking;
    update_error_.clear();
    append_log(format(L"[launcher] checking for updates (version %ls)\n",
                      launcher_version().c_str()));
    update_thread_ = std::thread([this, manual] {
        auto result = check_for_update();
        post([this, result, manual] { on_update_checked(result, manual); });
    });
    invalidate();
}

void App::on_update_checked(UpdateCheck result, bool manual) {
    switch (result.state) {
        case UpdateCheck::State::Available:
            update_state_ = UpdateState::Available;
            update_release_ = result.release;
            append_log(format(L"[launcher] version %ls is available (this is %ls)\n",
                              result.release.version.c_str(), launcher_version().c_str()));
            // At start the prompt is an interruption, so a version the player already
            // answered "not now" to waits behind the header button instead, and a step
            // that is running is never talked over.
            if (!busy_ && (manual || settings_.skipped_update != result.release.version)) {
                show_update_prompt();
            }
            break;
        case UpdateCheck::State::UpToDate:
            update_state_ = UpdateState::UpToDate;
            append_log(L"[launcher] this is the newest release\n");
            if (manual) {
                settings_note_ = L"GitHub was checked: this is the newest release.";
            }
            break;
        case UpdateCheck::State::Failed:
            update_state_ = UpdateState::Failed;
            update_error_ = result.error;
            append_log(L"[launcher] update check failed: " + result.error + L"\n");
            if (manual) show_message(L"Cannot check for updates", result.error);
            break;
    }
    invalidate();
}

void App::show_update_prompt() {
    if (update_release_.version.empty()) return;
    auto target = install_target();
    bool writable = install_writable();
    if (update_release_.asset_url.empty() || !writable) {
        std::wstring text = format(L"You have version %ls. ", launcher_version().c_str());
        text += update_release_.asset_url.empty()
                    ? L"This release has no download for this system, so the launcher "
                      L"cannot install it itself."
                    : L"This copy of the launcher sits in a folder it cannot write to, so "
                      L"it cannot replace itself.";
        text += L" The release page has the download.";
        show_confirm(format(L"Version %ls is available", update_release_.version.c_str()),
                     text, L"Open release page", [this] { open_update_page(); });
        overlay_update_ = true;
        return;
    }
    std::wstring text = format(L"You have version %ls. The launcher can download version "
                               L"%ls, replace this copy and start it again.",
                               launcher_version().c_str(),
                               update_release_.version.c_str());
    if (!target.empty()) text += L"\n\nIt replaces:\n" + path_display(target);
    show_confirm(format(L"Version %ls is available", update_release_.version.c_str()), text,
                 L"Update now", [this] { begin_update(); });
    overlay_update_ = true;
}

void App::begin_update() {
    if (busy_ || update_release_.version.empty()) return;
    if (game_.running()) {
        // Replacing the launcher ends this process, and the game goes with it.
        show_message(L"Close the game first",
                     L"The update restarts the launcher, which would end the game. Stop it, "
                     L"then install the update.");
        return;
    }
    if (update_release_.asset_url.empty() || !install_writable()) {
        open_update_page();
        return;
    }
    start_job(update_tasks(update_release_), false, true);
}

void App::open_update_page() {
    if (update_release_.page_url.empty()) {
        show_message(L"Updates", L"This release has no page to open.");
        return;
    }
    std::wstring error;
    if (!open_url(update_release_.page_url, &error)) {
        show_message(L"Cannot open", error.empty() ? update_release_.page_url : error);
    }
}

void App::open_settings() {
    settings_open_ = true;
    scale_open_ = false;
    pad_menu_row_ = -1;
    settings_note_.clear();
    tools_ = check_tools();
    refresh_source_status();
    refresh_devices();
    invalidate();
}

void App::create_shortcut(bool system_wide) {
    fs::path folder = system_wide ? applications_folder() : desktop_folder();
    if (folder.empty()) {
        show_message(L"Shortcut", L"This system did not report a shortcut folder.");
        return;
    }
    fs::path link = folder / shortcut_name();
    std::wstring error;
    if (nla::create_shortcut(link, fs::path(exe_path()), fs::path(exe_dir()),
                             L"Naruto: Clash of Ninja launcher", &error)) {
        settings_note_ = L"Created " + path_display(link);
        append_log(L"[launcher] " + settings_note_ + L"\n");
    } else {
        show_message(L"Shortcut", error);
    }
    invalidate();
}

void App::save_settings() {
    if (project_) settings_.project_root = project_->root().wstring();
    settings_.save();
}

}  // namespace nla

