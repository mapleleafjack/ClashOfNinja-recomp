#pragma once

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
#include "jobs.h"
#include "project.h"
#include "settings.h"
#include "tools.h"
#include "update.h"
#include "window.h"

namespace nla {

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;

    RectF box() const { return RectF{x, y, x + w, y + h}; }
    bool contains(float px, float py) const {
        return px >= x && px <= x + w && py >= y && py <= y + h;
    }
    Rect inset(float amount) const {
        return {x + amount, y + amount, w - 2 * amount, h - 2 * amount};
    }
};

enum class Id {
    None,
    Launch,
    Stop,
    LogToggle,
    CopyLog,
    ClearLog,
    OpenSettings,
    SettingsClose,
    SettingsProject,
    SettingsOpenProject,
    SettingsImage,
    SettingsOpenDisc,
    SettingsOpenSaves,
    SettingsOpenControls,
    SettingsResetControls,
    SettingsRebuild,
    SettingsRescan,
    SettingsTools,
    SettingsFetchTools,
    SettingsDesktopShortcut,
    SettingsStartShortcut,
    SettingsUpdateProject,
    SettingsToggleUpdates,
    SettingsCheckNow,
    SettingsInstallUpdate,
    UpdateBanner,
    Scale,
    Scale1,
    Scale2,
    Scale3,
    Scale4,
    ToggleMute,
    ToggleInputLog,
    SettingsPlayers,
    SettingsRefreshDevices,
    PadRow,                       // a player row in Settings: +0 the keyboard, then the controllers
    PadRowEnd = PadRow + 8,
    PadOption,                    // the pads a row may be set to: +0 Automatic, then 1 to 4
    PadOptionEnd = PadOption + 8,
    OverlayAction,
    OverlayCancel,
    PickerUp,
    PickerPath,
    PickerAccept,
    PickerCancel,
    PickerEntry,  // the first entry row; the rest follow it
    GameCover,    // the first game's cover on the main page; the rest follow it
};

class App {
  public:
    App() = default;
    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    bool create();
    int run();
    // Renders one frame of the main window into a BMP file, without a window. Used to
    // check the drawing on a machine with no desktop, and by the launcher's own tests.
    int render_to(const std::string& path, float scale, const std::string& view = "main");

  private:
    struct Hit {
        Rect rect;
        Id id;
    };

    struct PickerEntry {
        std::wstring name;
        bool directory = false;
    };

    // A game's cover art, read from its folder once and kept for later frames.
    struct Cover {
        bool loaded = false;
        int width = 0;
        int height = 0;
        std::vector<std::uint32_t> pixels;
    };
    enum class Pill { Muted, Ok, Warn, Bad };
    enum class PickerKind { None, Folder, Image };
    // What came of looking for a newer release.
    enum class UpdateState { Never, Checking, UpToDate, Available, Failed };

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

    void button(Id id, const Rect& rect, const std::wstring& label, bool primary,
                bool enabled, bool danger = false, Face face = Face::Button);
    void link(Id id, const Rect& rect, const std::wstring& label, bool enabled = true);
    void checkbox(Id id, float x, float y, const std::wstring& label, bool checked,
                  bool enabled = true);
    void scale_combo(Id id, const Rect& rect, bool enabled);
    // A row in Settings: a name on the left, the pad it plays as on the right.
    void pad_combo(Id id, const Rect& rect, const std::wstring& value, bool enabled);
    void pill(const Rect& rect, const std::wstring& text, Pill state);
    void card_box(const Rect& rect);

    float text_width(const std::wstring& text, Face face);
    std::wstring elide(const std::wstring& text, Face face, float width);
    void register_hit(Id id, const Rect& rect);
    Id hit_at(float x, float y) const;

    void refresh();
    void locate_project();
    // The games the project folder supplies, in the launcher's fixed order.
    std::vector<GameInfo> available_games() const;
    // A game's cover art from its own folder, or nullptr when it has none.
    const Cover* cover(const std::wstring& folder);
    // The art drawn in a cover's place until one is put in the game folder.
    const Cover& placeholder_cover();
    void select_game(const std::wstring& folder);
    void launch_game(const std::wstring& folder);
    std::wstring current_title() const;
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
    void open_path(const std::filesystem::path& path);
    void reset_controls();
    // The controllers the game would see, and the pads they play as, from the game's
    // --list-devices. Row 0 is the keyboard.
    void open_players();
    void refresh_devices();
    void write_pad_pins();
    void activate(Id id);
    bool modal_active() const { return settings_open_ || overlay_open_ || picker_ != PickerKind::None; }

    // `exits` ends the launcher once the steps succeed: an update hands over to its
    // helper, which cannot swap this copy while it is running. `done` runs after the
    // usual reporting, for a step that has its own next move.
    void start_job(std::vector<Task> tasks, bool then_launch = false, bool exits = false,
                   std::function<void(int)> done = nullptr);
    void on_log(const std::wstring& text);
    void on_stage(const std::wstring& text);
    void on_progress(int percent);
    void on_job_done(int code);
    void append_log(const std::wstring& text);
    void copy_log();

    void show_message(const std::wstring& title, const std::wstring& text,
                      const std::wstring& action = L"Close");
    void show_confirm(const std::wstring& title, const std::wstring& text,
                      const std::wstring& action, std::function<void()> confirmed);
    void close_overlay();

    void open_settings();
    void create_shortcut(bool system_wide);
    void save_settings();

    void refresh_source_status();
    void update_project();
    void start_update_check(bool manual);
    void on_update_checked(UpdateCheck check, bool manual);
    void show_update_prompt();
    void begin_update();
    void open_update_page();

    // Asks for a folder or a disc image: the platform's own chooser where there is one,
    // and the launcher's picker where there is not.
    void choose_path(PickerKind kind, const std::wstring& title, const fs::path& start,
                     std::function<void(const fs::path&)> accept);
    // The launcher's own picker, the fallback on systems with no native chooser.
    void open_picker(PickerKind kind, const std::wstring& title, const fs::path& start,
                     std::function<void(const fs::path&)> accept);
    void close_picker();
    void list_picker();
    void accept_picker();
    void fill_picker_path();

    void invalidate() { window_.invalidate(); }    void post(std::function<void()> task) { dispatcher_.post(std::move(task)); }
    void animate();

    Window window_;
    Dispatcher dispatcher_;
    Painter painter_;

    Settings settings_;
    std::optional<Project> project_;
    ProjectStatus status_;
    std::map<std::wstring, Cover> covers_;
    Cover placeholder_;

    Job job_;
    GameProcess game_;

    std::deque<std::wstring> log_;
    bool log_visible_ = false;
    int log_offset_ = 0;
    bool busy_ = false;
    bool quit_ = false;
    bool job_exits_ = false;
    bool job_then_launch_ = false;
    std::function<void(int)> job_done_;
    bool launch_wanted_ = false;
    int progress_ = -1;
    std::wstring stage_;
    std::wstring play_status_;
    float progress_phase_ = 0;
    std::uint32_t next_frame_ = 0;

    bool settings_open_ = false;
    bool scale_open_ = false;
    Rect scale_anchor_{};
    std::wstring settings_note_;
    std::vector<ToolInfo> tools_;

    // A controller SDL reports, as the game names it, and the pad it plays as: 1 to 4, or
    // 0 for the first one free.
    struct DeviceRow {
        std::wstring name;
        int nth = 1;
        int pad = 0;
    };
    std::vector<DeviceRow> devices_;
    std::wstring devices_note_;  // why the list is empty, when it is
    int keyboard_pad_ = 1;
    int pad_menu_row_ = -1;  // the row whose menu is open, -1 for none
    Rect pad_menu_anchor_{};
    bool rendering_ = false;  // --render: no game to ask, so the rows are stand-ins

    bool overlay_open_ = false;
    bool overlay_confirm_ = false;
    bool overlay_update_ = false;
    bool overlay_players_ = false;
    std::wstring overlay_title_;
    std::wstring overlay_text_;
    std::wstring overlay_action_;
    std::function<void()> overlay_callback_;

    // The newest release, and what came of looking for it.
    UpdateState update_state_ = UpdateState::Never;
    ReleaseInfo update_release_;
    std::wstring update_error_;
    std::thread update_thread_;

    // The project folder as a git checkout, and whether its source is older than this
    // launcher, which would build the game from source the launcher does not expect.
    bool git_found_ = false;
    bool project_checkout_ = false;
    bool project_behind_ = false;
    std::wstring source_revision_;

    // Set by render_to(), where there is no window: nothing may open a native dialog.
    bool headless_ = false;

    PickerKind picker_ = PickerKind::None;
    std::wstring picker_title_;
    std::wstring picker_folder_;
    std::wstring picker_path_;
    std::wstring picker_note_;
    bool picker_editing_ = false;
    int picker_selected_ = -1;
    int picker_offset_ = 0;
    std::vector<PickerEntry> picker_entries_;
    std::function<void(const fs::path&)> picker_accept_;

    Id hover_ = Id::None;
    Id pressed_ = Id::None;
    std::vector<Hit> hits_;
    float width_ = 1060;
    float height_ = 820;
    Rect log_rect_{};
    bool log_hovered_ = false;
};

}  // namespace nla
