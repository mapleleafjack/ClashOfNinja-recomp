// The launcher's lifetime, its window, the input it takes, and the panels it opens.

#include "app.h"
#include "app_shared.h"

#include "dialog.h"

#include "util.h"

#include <algorithm>
#include <cmath>

namespace nla {

namespace {

// The keys the window maps to a page's own vocabulary. Only a modified v is a paste, so a
// plain v is not offered to a page at all.
std::optional<ScreenKey> screen_key_for(SDL_Keycode sym, bool command) {
    switch (sym) {
        case SDLK_ESCAPE: return ScreenKey::Escape;
        case SDLK_RETURN:
        case SDLK_KP_ENTER: return ScreenKey::Enter;
        case SDLK_TAB: return ScreenKey::Tab;
        case SDLK_BACKSPACE: return ScreenKey::Backspace;
        case SDLK_UP: return ScreenKey::Up;
        case SDLK_DOWN: return ScreenKey::Down;
        case SDLK_v:
            if (command) return ScreenKey::Paste;
            return std::nullopt;
        default: return std::nullopt;
    }
}

}  // namespace

// The window fills the host before anything can ask for it: a screen is handed the same
// struct, so a callback it takes is the window's own method, not a copy of what one did when
// it was built.
App::App() {
    host_.invalidate = [this] { invalidate(); };
    host_.post = [this](std::function<void()> task) { post(std::move(task)); };
    host_.log = [this](const std::wstring& text) { append_log(text); };
    host_.message = [this](const std::wstring& title, const std::wstring& text) {
        show_message(title, text);
    };
    host_.update_prompt = [this](const std::wstring& title, const std::wstring& text,
                                 const std::wstring& action,
                                 std::function<void()> on_action) {
        show_confirm(title, text, action, std::move(on_action));
        // Closing the prompt means "not this version", which is what the overlay remembers.
        overlay_.mark_update();
    };
    host_.busy = [this] { return busy_; };
    host_.status = [this]() -> const ProjectStatus& { return state_.status(); };
    host_.game_running = [this] { return game_.running(); };
    host_.settings = [this]() -> Settings& { return state_.settings(); };
    host_.settings_note = [this](const std::wstring& text) { settings_note_ = text; };
    host_.start_job = [this](std::vector<Task> tasks, JobOptions options) {
        start_job(std::move(tasks), std::move(options));
    };
    host_.elide_path = [this](const std::wstring& text) {
        return ui_.elide_middle(text, Face::Body, kMessagePanelWidth - 52.0f);
    };
    host_.clipboard = [this] { return window_.clipboard(); };
    host_.text_input = [this](bool on) { window_.set_text_input(on); };
    host_.native_chooser = [this](PickerKind kind, const std::wstring& title,
                                  const fs::path& start) {
        NativeAnswer answer;
        // A render has no window and nothing to show a chooser over: the launcher's own
        // list is what it draws, and that is not a failure to report.
        if (headless_) return answer;
        const DialogKind dialog = kind == PickerKind::Folder ? DialogKind::Folder
                                  : kind == PickerKind::Patch ? DialogKind::Patch
                                                              : DialogKind::Image;
        auto shown = show_native_dialog(dialog, window_.handle(), title, start);
        answer.shown = shown.shown;
        answer.refused = !shown.shown;
        answer.path = shown.path;
        return answer;
    };
    host_.take_over = [this] {
        overlay_.close();
        close_popups();
    };
    host_.launch_cancelled = [this] { launch_wanted_ = false; };
    host_.project = [this]() -> const std::optional<Project>& { return state_.project(); };
    host_.rendering = [this] { return rendering_; };
    host_.refresh = [this] { refresh(); };
    host_.show_panel = [this](OverlayPage page, const std::wstring& title) {
        overlay_.show_panel(page, title);
        close_popups();
    };
}

App::~App() {
    // Work in flight posts its answer to the dispatcher, so it must be finished before
    // anything it touches goes away. Both requests have a short timeout of their own, and
    // the box art worker stops between downloads.
    covers_stop_ = true;
    update_.shutdown();
    geckos_.shutdown();
    if (covers_thread_.joinable()) covers_thread_.join();
    job_.shutdown();
    game_.shutdown();
}

bool App::create() {
    state_.load_settings();
    tools_ = check_tools();
    // What an earlier update could not remove once its helper had finished.
    clean_up_after_update();

    auto root = Project::locate();
    if (root) state_.set_project(*root, state_.settings().game);
    if (state_.project()) state_.settings().game = state_.project()->game();

    std::wstring error;
    std::wstring window_title = current_title() + L" — Launcher";
    if (!window_.create(window_title, state_.settings().window_width, state_.settings().window_height,
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

    if (state_.settings().check_updates) update_.start(false);
    // Art for a game that has none, so the page is not bare on a fresh install.
    fetch_covers();

    // Which folder is in use, so the log never leaves where the games are a guess.
    if (state_.project()) {
        append_log(format(L"[launcher] project: %ls\n", state_.project()->root().c_str()));
    }
    if (!state_.project()) {
        append_log(L"[launcher] project folder not located yet; Launch the game asks "
                   L"for it.\n");
    } else if (!state_.status().disc.verified) {
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
        int timeout = (busy_ && activity_.percent() < 0) ? 16 : 250;
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
    state_.settings().window_width = static_cast<int>(std::lround(pixel_width / scale));
    state_.settings().window_height = static_cast<int>(std::lround(pixel_height / scale));
    save_settings();
    job_.shutdown();
    game_.shutdown();
    window_.destroy();
    return 0;
}

void App::resize_surface() {
    int pixel_width = 0, pixel_height = 0;
    window_.pixel_size(&pixel_width, &pixel_height);
    float dpi = window_.scale();
    window_.set_ui_zoom(ui_zoom_for(pixel_width, pixel_height, dpi));

    float scale = dpi * window_.ui_zoom();
    painter_.resize(pixel_width, pixel_height, scale);
    width_ = static_cast<float>(pixel_width) / scale;
    height_ = static_cast<float>(pixel_height) / scale;
    invalidate();
}

void App::animate() {
    if (!busy_ || activity_.percent() >= 0) return;
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
                    if (ui_.hover() != Id::None || activity_.hovered()) {
                        ui_.set_hover(Id::None);
                        activity_.set_hovered(false);
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
            Id hit = ui_.hit_at(x, y);
            bool over_log = activity_.visible() && activity_.rect().contains(x, y);
            if (hit != ui_.hover() || over_log != activity_.hovered()) {
                ui_.set_hover(hit);
                activity_.set_hovered(over_log);
                invalidate();
            }
            window_.set_hand_cursor(hit != Id::None);
            return;
        }
        case SDL_MOUSEBUTTONDOWN:
            if (event.button.button != SDL_BUTTON_LEFT) return;
            ui_.set_pressed(ui_.hit_at(static_cast<float>(event.button.x) / factor,
                                       static_cast<float>(event.button.y) / factor));
            invalidate();
            return;
        case SDL_MOUSEBUTTONUP: {
            if (event.button.button != SDL_BUTTON_LEFT) return;
            Id hit = ui_.hit_at(static_cast<float>(event.button.x) / factor,
                                static_cast<float>(event.button.y) / factor);
            Id pressed = ui_.pressed();
            ui_.set_pressed(Id::None);
            if (pressed != Id::None && pressed == hit) activate(hit);
            invalidate();
            return;
        }
        case SDL_MOUSEWHEEL: {
            int mouse_x = 0, mouse_y = 0;
            SDL_GetMouseState(&mouse_x, &mouse_y);
            float x = static_cast<float>(mouse_x) / factor;
            float y = static_cast<float>(mouse_y) / factor;
            if (picker_.capturing()) {
                picker_.wheel(event.wheel.y);
            } else if (overlay_.is(OverlayPage::Geckos)) {
                // The guide's list is longer than the panel: the wheel is how the rest of
                // it is reached.
                geckos_.scroll(event.wheel.y);
            } else if (activity_.visible() && activity_.rect().contains(x, y) &&
                       !activity_.empty()) {
                activity_.scroll(event.wheel.y > 0 ? 1 : -1);
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
                Project candidate(dropped, state_.settings().game);
                if (!candidate.complete()) {
                    std::wstring error;
                    if (!Project::create(dropped, &error)) {
                        show_message(L"Could not set the folder up", error);
                        return;
                    }
                    append_log(format(L"[launcher] wrote the game folders in %ls\n",
                                      dropped.wstring().c_str()));
                    candidate = Project(dropped, state_.settings().game);
                }
                if (candidate.valid()) {
                    state_.set_project(dropped, state_.settings().game);
                    art_.clear();
                    fetch_covers();
                    state_.settings().game = state_.project()->game();
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
    // The codes box takes held keys like a text field does: Backspace that repeats is how a
    // line is cleared.
    const bool typing = geckos_.capturing();
    if (key.repeat && !picker_.capturing() && !typing) return;
    const bool command = (key.keysym.mod & (KMOD_CTRL | KMOD_GUI)) != 0;

    // A page with a field takes the keys it owns while it is showing, and the window's own
    // shortcuts only see what it leaves.
    if (picker_.capturing()) {
        if (auto pressed = screen_key_for(key.keysym.sym, command)) {
            picker_.key(*pressed);
            return;
        }
    }

    switch (key.keysym.sym) {
        case SDLK_ESCAPE:
            if (overlay_.scale_menu()) {
                overlay_.set_scale_menu(false);
                invalidate();
            } else if (overlay_.is_overlay()) {
                // The players panel is opened from Settings, so Escape lands back there;
                // the mods panel belongs to the game page, which is all that is under it.
                const bool back_to_settings = overlay_.is(OverlayPage::Players);
                close_overlay();
                if (back_to_settings) open_settings();
            } else if (overlay_.is(OverlayPage::Settings)) {
                overlay_.close();
                invalidate();
            }
            return;
        default:
            return;
    }
}

void App::handle_text(const SDL_TextInputEvent& text) {
    if (geckos_.capturing()) {
        geckos_.text(widen(text.text));
        return;
    }
    if (picker_.capturing()) picker_.text(widen(text.text));
}

// -- actions ---------------------------------------------------------------

void App::activate(Id id) {
    if (id == Id::None) return;

    auto anchor_scale = [this] {
        if (const auto rect = ui_.rect_of(Id::Scale)) scale_anchor_ = *rect;
    };

    // The picker is the topmost page: while it is open it takes every id, the way the block
    // this replaces took them before the launcher had more than one page.
    if (picker_.capturing()) {
        if (picker_.handles(id)) picker_.activate(id);
        return;
    }

    // The panel answers for its own ids and lets the overlay's own buttons through, which is
    // how Done closes it with whatever was checked left unapplied.
    if (overlay_.is(OverlayPage::Geckos) && geckos_.handles(id)) {
        geckos_.activate(id);
        return;
    }

    // The panel answers for its own ids and lets the overlay's own buttons through, which is
    // how Done closes it with whatever was checked left unapplied.
    if (overlay_.is(OverlayPage::Mods) && mods_.handles(id)) {
        mods_.activate(id);
        return;
    }

    if (overlay_.is(OverlayPage::Players)) {
        if (device_map_.menu_row() >= 0) {
            const int player = device_map_.menu_row() + 1;
            const int picked = static_cast<int>(id) - static_cast<int>(Id::PadOption);
            if (id >= Id::PadOption && id < Id::PadOptionEnd &&
                picked < static_cast<int>(device_map_.pad_options().size())) {
                device_map_.set_menu_row(-1);
                device_map_.assign(player, picked);
                write_pad_pins();
                return;
            }
            device_map_.set_menu_row(-1);  // anything else closes the menu, then does its own work
        }
        if (id >= Id::PadRow && id < Id::PadRowEnd) {
            const int row = static_cast<int>(id) - static_cast<int>(Id::PadRow);
            if (row >= 0 && row < kPads) {
                device_map_.set_menu_row(row);
                if (const auto rect = ui_.rect_of(id)) device_map_.set_menu_anchor(*rect);
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

    if (overlay_.is_overlay()) {
        if (id == Id::OverlayAction) {
            auto callback = overlay_.take_action();
            close_overlay();
            if (callback) callback();
        } else if (id == Id::OverlaySecondary) {
            auto callback = overlay_.take_second();
            close_overlay();
            if (callback) callback();
        } else if (id == Id::OverlayCancel) {
            // "Not now" on the update prompt is remembered for that version only, so a
            // later release asks again.
            if (overlay_.update() && !update_.version().empty()) {
                state_.settings().skipped_update = update_.version();
                save_settings();
            }
            close_overlay();
        }
        return;
    }

    if (overlay_.is(OverlayPage::Settings)) {
        switch (id) {
            case Id::SettingsClose:
                overlay_.close();
                refresh();
                break;
            case Id::SettingsToggleUpdates:
                state_.settings().check_updates = !state_.settings().check_updates;
                save_settings();
                break;
            case Id::SettingsCheckNow:
                update_.start(true);
                return;
            case Id::SettingsInstallUpdate:
                overlay_.close();
                update_.begin();
                return;
            case Id::Scale:
                overlay_.set_scale_menu(!overlay_.scale_menu());
                if (overlay_.scale_menu()) anchor_scale();
                break;
            case Id::Scale1:
            case Id::Scale2:
            case Id::Scale3:
            case Id::Scale4:
                state_.settings().scale = static_cast<int>(id) - static_cast<int>(Id::Scale1) + 1;
                overlay_.set_scale_menu(false);
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

    if (overlay_.scale_menu()) {
        if (id >= Id::Scale1 && id <= Id::Scale4) {
            state_.settings().scale = static_cast<int>(id) - static_cast<int>(Id::Scale1) + 1;
            overlay_.set_scale_menu(false);
            save_settings();
            invalidate();
            return;
        }
        overlay_.set_scale_menu(false);
    }

    // A screen answers for the ids it owns, so this switch only has the page's own.
    if (update_.handles(id)) {
        update_.activate(id);
        return;
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
            activity_.toggle();
            break;
        case Id::CopyLog: copy_log(); break;
        case Id::ClearLog:
            activity_.clear();
            break;
        case Id::OpenSettings: open_settings(); break;
        case Id::Scale:
            overlay_.set_scale_menu(!overlay_.scale_menu());
            if (overlay_.scale_menu()) anchor_scale();
            break;
        case Id::Play: launch(); break;
        case Id::Mods:
            if (state_.project()) mods_.open();
            else locate_project();
            break;
        case Id::Geckos:
            if (state_.project()) geckos_.open();
            else locate_project();
            break;
        default: {
            // A cover on the main page chooses that game; Play starts it.
            int game_index = static_cast<int>(id) - static_cast<int>(Id::GameCover);
            auto games = available_games();
            if (id >= Id::GameCover && id < Id::GameCoverEnd && game_index < static_cast<int>(games.size()))
                select_game(games[static_cast<size_t>(game_index)].folder);
            break;
        }
    }
    invalidate();
}

void App::show_message(const std::wstring& title, const std::wstring& text,
                       const std::wstring& action) {
    overlay_.show_message(title, text, action);
    close_popups();
    geckos_.stop_editing();
    window_.set_text_input(false);
    invalidate();
}

void App::show_confirm(const std::wstring& title, const std::wstring& text,
                       const std::wstring& action, std::function<void()> confirmed,
                       const std::wstring& second,
                       std::function<void()> second_action) {
    overlay_.show_confirm(title, text, action, std::move(confirmed), second,
                          std::move(second_action));
    close_popups();
    geckos_.stop_editing();
    window_.set_text_input(false);
    invalidate();
}

void App::close_overlay() {
    overlay_.close();
    // The box stops taking keystrokes with the panel: the window hands text back to the
    // picker's own field, which is the only other place that types.
    geckos_.stop_editing();
    window_.set_text_input(false);
    device_map_.set_menu_row(-1);
    invalidate();
}

void App::open_settings() {
    overlay_.show_page(OverlayPage::Settings);
    close_popups();
    settings_note_.clear();
    invalidate();
}

void App::close_popups() {
    overlay_.set_scale_menu(false);
    device_map_.set_menu_row(-1);
}

void App::save_settings() { state_.settings().save(); }

}  // namespace nla
