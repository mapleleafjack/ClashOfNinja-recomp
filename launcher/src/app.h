#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <SDL.h>

#include "painter.h"
#include "ui.h"
#include "activity_panel.h"
#include "screen.h"
#include "catalog.h"
#include "geckos_screen.h"
#include "mods_screen.h"
#include "covers.h"
#include "devices.h"
#include "picker.h"
#include "picker_screen.h"
#include "codes.h"
#include "jobs.h"
#include "mods.h"
#include "project.h"
#include "project_state.h"
#include "settings.h"
#include "tools.h"
#include "update_screen.h"
#include "update.h"
#include "window.h"

namespace nla {

class App {
  public:
    App();
    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    bool create();
    int run();
    // Renders one frame of the main window into a BMP file, without a window. Used to
    // check the drawing on a machine with no desktop, and by the launcher's own tests.
    // A window size may be given, so the layout can be checked as a large window draws it.
    int render_to(const std::string& path, float scale, const std::string& view = "main",
                  int window_width = 0, int window_height = 0);

    // The width of the panel a message is shown in. A message written for it can be cut to
    // fit before it is stored, which is how a long path stays on one line.
    static constexpr float kMessagePanelWidth = 620.0f;

  private:

    void handle_event(const SDL_Event& event);
    void handle_key(const SDL_KeyboardEvent& key);
    void handle_text(const SDL_TextInputEvent& text);
    void resize_surface();

    void paint();
    void paint_header();
    void paint_games();
    void paint_cover(Id id, const Rect& rect, const GameInfo& game, bool selected,
                     bool enabled);
    void paint_activity(float top);
    void paint_settings();
    void paint_overlay();
    void paint_pad_menu();
    void paint_picker();
    void paint_scale_menu();

    void refresh();
    void locate_project();
    // The games the project folder supplies, in the launcher's fixed order.
    std::vector<GameInfo> available_games() const;
    // A game's cover art from covers/, or nullptr when it has none.
    const Cover* cover(const std::wstring& folder);
    // Fetches the box art for the games that have none, so a fresh install shows covers
    // without the launcher shipping art the publisher owns.
    void fetch_covers();
    // The art drawn in a cover's place until covers/ gets a file for that game.
    void select_game(const std::wstring& folder);
    void launch_game(const std::wstring& folder);
    std::wstring current_title() const;
    // Asks for a disc image for the project in use. The chooser itself is PickerScreen's;
    // where to start looking and what an answer starts are this page's.
    void choose_image();
    void start_setup(const std::filesystem::path& image);
    void begin_setup(const std::filesystem::path& image, bool then_launch);
    void start_build(bool then_launch = false);
    std::vector<Task> build_tasks();
    bool check_required_tools();
    void install_tools(bool then_launch);
    // macOS: opens Apple's installer for the command line tools, then waits for the
    // compiler it brings, because that is the one thing a release cannot carry.
    std::vector<Task> apple_tools_tasks();
    void launch();
    void continue_launch();
    void play();
    // The controllers the game would see, and the pads they play as, from the game's
    // --list-devices. Row 0 is the keyboard.
    void open_players();
    // Asks the game which controllers it sees. Deciding who plays as whom is DeviceMap's.
    void refresh_devices();
    // The pads this picker owns, written to controls.txt beside the player's own mapping.
    void write_pad_pins();
    void activate(Id id);
    // Closes the menus a new page would otherwise be drawn under: the scale menu, and the
    // row a player's device list was opened from.
    void close_popups();

    // `exits` ends the launcher once the steps succeed: an update hands over to its
    // helper, which cannot swap this copy while it is running. `done` runs after the
    // usual reporting, for a step that has its own next move.
    void start_job(std::vector<Task> tasks, JobOptions options = {});
    void on_log(const std::wstring& text);
    void on_stage(const std::wstring& text);
    void on_progress(int percent);
    void on_job_done(int code);
    void append_log(const std::wstring& text);
    void copy_log();

    void show_message(const std::wstring& title, const std::wstring& text,
                      const std::wstring& action = L"Close");
    // `second` adds a button to the left of the action's: a step that failed offers the
    // retry, and the other way out of it, side by side.
    void show_confirm(const std::wstring& title, const std::wstring& text,
                      const std::wstring& action, std::function<void()> confirmed,
                      const std::wstring& second = L"",
                      std::function<void()> second_action = nullptr);
    void close_overlay();

    // Writes whatever the project folder is missing -- the recompiler, the tool script, a
    // game's folder -- and puts the project back in place. False when that failed, with the
    // reason already shown.
    bool repair_project();

    void open_settings();
    void save_settings();

    void invalidate() { window_.invalidate(); }    void post(std::function<void()> task) { dispatcher_.post(std::move(task)); }
    void animate();

    Window window_;
    Dispatcher dispatcher_;
    Painter painter_;
    UiContext ui_{painter_};
    Overlay overlay_;
    DeviceMap device_map_;

    // The game in use, the settings it remembers, and what the project's files say. The three
    // move together, so they are held together.
    ProjectState state_;
    CoverCache art_;
    // The box art download, and whether one is in flight. Quitting sets the stop flag so
    // the worker does not hold the exit up for the rest of its downloads.
    std::thread covers_thread_;
    std::atomic_bool covers_stop_{false};
    bool covers_fetching_ = false;

    Job job_;
    GameProcess game_;

    ActivityPanel activity_;
    bool busy_ = false;
    bool quit_ = false;
    // What the job in flight was started to do, and what to run when it ends. Replaced only
    // by a job that actually starts, so a request that is turned away cannot change it.
    JobOptions job_run_;
    bool launch_wanted_ = false;
    int progress_ = -1;
    std::wstring stage_;
    std::wstring play_status_;
    float progress_phase_ = 0;
    std::uint32_t next_frame_ = 0;

    Rect scale_anchor_{};
    std::wstring settings_note_;
    std::vector<ToolInfo> tools_;

    bool rendering_ = false;  // --render: no game to ask, so the rows are stand-ins
    // A render has no window to click in, so the page's two live states -- a step running,
    // and the game itself up -- are asked for by name rather than found on the machine.
    bool forced_busy_ = false;
    bool forced_running_ = false;

    // What a screen may ask the window for, filled in before anything can ask. The screen
    // is declared after it, so it is handed a host that is already there.
    ScreenHost host_;
    UpdateScreen update_{host_};
    PickerScreen picker_{host_};
    GeckosScreen geckos_{host_};
    ModsScreen mods_{host_, picker_, geckos_};

    // Set by render_to(), where there is no window: nothing may open a native dialog.
    bool headless_ = false;


    float width_ = 1060;
    float height_ = 820;
};

}  // namespace nla
