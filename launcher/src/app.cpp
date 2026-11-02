// The launcher's lifetime, its window, the input it takes, and the panels it opens.

#include "app.h"
#include "app_shared.h"

#include "util.h"

#include <algorithm>
#include <cmath>

namespace nla {

namespace {

std::wstring picker_join(const std::wstring& folder, const std::wstring& name) {
    return (fs::path(folder) / name).wstring();
}

}  // namespace

App::~App() {
    // Work in flight posts its answer to the dispatcher, so it must be finished before
    // anything it touches goes away. Both requests have a short timeout of their own, and
    // the box art worker stops between downloads.
    covers_stop_ = true;
    codes_stop_ = true;
    if (update_thread_.joinable()) update_thread_.join();
    if (covers_thread_.joinable()) covers_thread_.join();
    if (codes_thread_.joinable()) codes_thread_.join();
    job_.shutdown();
    game_.shutdown();
}

bool App::create() {
    settings_ = Settings::load();
    tools_ = check_tools();
    // What an earlier update could not remove once its helper had finished.
    clean_up_after_update();

    auto root = Project::locate();
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
    // Art for a game that has none, so the page is not bare on a fresh install.
    fetch_covers();

    // Which folder is in use, so the log never leaves where the games are a guess.
    if (project_) {
        append_log(format(L"[launcher] project: %ls\n", project_->root().c_str()));
    }
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
                    if (ui_.hover() != Id::None || log_hovered_) {
                        ui_.set_hover(Id::None);
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
            Id hit = ui_.hit_at(x, y);
            bool over_log = log_visible_ && log_rect_.contains(x, y);
            if (hit != ui_.hover() || over_log != log_hovered_) {
                ui_.set_hover(hit);
                log_hovered_ = over_log;
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
            if (picker_ != PickerKind::None) {
                picker_offset_ = std::max(0, picker_offset_ - event.wheel.y);
                invalidate();
            } else if (overlay_.is(OverlayPage::Geckos)) {
                // The guide's list is longer than the panel: the wheel is how the rest of
                // it is reached.
                const std::size_t rows = codes_.rows(code_rows_fit_);
                const std::size_t most =
                    codes_.entries().size() > rows ? codes_.entries().size() - rows : 0;
                const long wanted = static_cast<long>(code_offset_) -
                                    static_cast<long>(event.wheel.y) * 3;
                code_offset_ = static_cast<std::size_t>(
                    std::max(0L, std::min(static_cast<long>(most), wanted)));
                invalidate();
            } else if (log_visible_ && log_rect_.contains(x, y) && !activity_.empty()) {
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
                if (!candidate.complete()) {
                    std::wstring error;
                    if (!Project::create(dropped, &error)) {
                        show_message(L"Could not set the folder up", error);
                        return;
                    }
                    append_log(format(L"[launcher] wrote the game folders in %ls\n",
                                      dropped.wstring().c_str()));
                    candidate = Project(dropped, settings_.game);
                }
                if (candidate.valid()) {
                    project_.emplace(dropped, settings_.game);
                    covers_.clear();
                    fetch_covers();
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
    // The codes box takes held keys like a text field does: Backspace that repeats is how a
    // line is cleared.
    const bool typing = overlay_.is(OverlayPage::Geckos) && gecko_editing_;
    if (key.repeat && picker_ == PickerKind::None && !typing) return;
    const bool command = (key.keysym.mod & (KMOD_CTRL | KMOD_GUI)) != 0;

    // While the box has focus the keys it uses are its own: Return is a line in the block,
    // not a dialog's answer, and Escape leaves the box before it closes the panel.
    if (typing) {
        if (key.keysym.sym == SDLK_ESCAPE) {
            gecko_editing_ = false;
            window_.set_text_input(false);
            invalidate();
            return;
        }
        if (key.keysym.sym == SDLK_RETURN || key.keysym.sym == SDLK_KP_ENTER) {
            gecko_edit_ += L'\n';
            invalidate();
            return;
        }
        if (key.keysym.sym == SDLK_BACKSPACE) {
            if (!gecko_edit_.empty()) {
                gecko_edit_.pop_back();
                // A UTF-16 surrogate pair must not be split.
                if (sizeof(wchar_t) == 2 && !gecko_edit_.empty() &&
                    gecko_edit_.back() >= 0xD800 && gecko_edit_.back() <= 0xDBFF) {
                    gecko_edit_.pop_back();
                }
                invalidate();
            }
            return;
        }
        if (command && key.keysym.sym == SDLK_v) {
            auto text = window_.clipboard();
            if (!text.empty()) {
                gecko_edit_ += text;
                invalidate();
            }
            return;
        }
    }

    switch (key.keysym.sym) {
        case SDLK_ESCAPE:
            if (picker_ != PickerKind::None) {
                launch_wanted_ = false;
                close_picker();
            } else if (scale_open_) {
                scale_open_ = false;
                invalidate();
            } else if (overlay_.open()) {
                // The players panel is opened from Settings, so Escape lands back there;
                // the mods panel belongs to the game page, which is all that is under it.
                const bool back_to_settings = overlay_.is(OverlayPage::Players);
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
    if (overlay_.is(OverlayPage::Geckos) && gecko_editing_) {
        gecko_edit_ += widen(text.text);
        invalidate();
        return;
    }
    if (picker_ == PickerKind::None) return;
    picker_editing_ = true;
    picker_path_ += widen(text.text);
    invalidate();
}

// -- actions ---------------------------------------------------------------

void App::activate(Id id) {
    if (id == Id::None) return;

    auto anchor_scale = [this] {
        if (const auto rect = ui_.rect_of(Id::Scale)) scale_anchor_ = *rect;
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

    if (overlay_.is(OverlayPage::Geckos)) {
        const int row = static_cast<int>(id) - static_cast<int>(Id::CodeRow);
        if (id >= Id::CodeRow && id < Id::CodeRowEnd && row >= 0 &&
            row < static_cast<int>(codes_.rows(code_rows_fit_))) {
            toggle_code(codes_.entries()[code_offset_ + static_cast<size_t>(row)].name);
            return;
        }
        switch (id) {
            case Id::CodeBox:
                // The box holds what is typed at the end of it; a block pasted in is the
                // way a code gets here, so the field is the same one the picker uses.
                gecko_editing_ = true;
                window_.set_text_input(true);
                invalidate();
                return;
            case Id::CodeApply:
                apply_geckos();
                return;
            case Id::CodeRefresh:
                fetch_online_codes();
                return;
            case Id::OverlayAction:
                // Anything checked but not applied goes with it: the game is as it was.
                close_overlay();
                return;
            default:
                return;
        }
    }

    if (overlay_.is(OverlayPage::Mods)) {
        const int row = static_cast<int>(id) - static_cast<int>(Id::ModRow);
        if (id >= Id::ModRow && id < Id::ModRowEnd && row < static_cast<int>(mods_.rows())) {
            toggle_mod(mods_.entries()[static_cast<size_t>(row)].id);
            return;
        }
        switch (id) {
            case Id::ModsFolder:
                if (project_) open_path(project_->mods_dir());
                invalidate();
                return;
            case Id::ModChoose:
                choose_patch();
                return;
            case Id::ModApply:
                apply_mods();
                return;
            case Id::OverlayAction:
                // Anything checked but not applied goes with it: the game is as it was.
                close_overlay();
                return;
            default:
                return;
        }
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

    if (overlay_.open()) {
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
            if (overlay_.update() && !update_release_.version.empty()) {
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
            activity_.clear();
            log_offset_ = 0;
            break;
        case Id::OpenSettings: open_settings(); break;
        case Id::UpdateBanner: show_update_prompt(); break;
        case Id::Scale:
            scale_open_ = !scale_open_;
            if (scale_open_) anchor_scale();
            break;
        case Id::Play: launch(); break;
        case Id::Mods:
            if (project_) open_mods();
            else locate_project();
            break;
        case Id::Geckos:
            if (project_) open_geckos();
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
    settings_open_ = false;
    overlay_.show_message(title, text, action);
    gecko_editing_ = false;
    window_.set_text_input(false);
    scale_open_ = false;
    invalidate();
}

void App::show_confirm(const std::wstring& title, const std::wstring& text,
                       const std::wstring& action, std::function<void()> confirmed,
                       const std::wstring& second,
                       std::function<void()> second_action) {
    settings_open_ = false;
    overlay_.show_confirm(title, text, action, std::move(confirmed), second,
                          std::move(second_action));
    gecko_editing_ = false;
    window_.set_text_input(false);
    scale_open_ = false;
    invalidate();
}

void App::close_overlay() {
    overlay_.close();
    // The box stops taking keystrokes with the panel: the window hands text back to the
    // picker's own field, which is the only other place that types.
    gecko_editing_ = false;
    window_.set_text_input(false);
    device_map_.set_menu_row(-1);
    invalidate();
}

void App::open_settings() {
    settings_open_ = true;
    scale_open_ = false;
    device_map_.set_menu_row(-1);
    settings_note_.clear();
    invalidate();
}

void App::save_settings() { settings_.save(); }

}  // namespace nla
