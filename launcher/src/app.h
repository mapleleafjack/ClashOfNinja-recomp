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
#include "catalog.h"
#include "devices.h"
#include "codes.h"
#include "jobs.h"
#include "mods.h"
#include "project.h"
#include "settings.h"
#include "tools.h"
#include "update.h"
#include "window.h"

namespace nla {

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
    // A window size may be given, so the layout can be checked as a large window draws it.
    int render_to(const std::string& path, float scale, const std::string& view = "main",
                  int window_width = 0, int window_height = 0);

    // The width of the panel a message is shown in. A message written for it can be cut to
    // fit before it is stored, which is how a long path stays on one line.
    static constexpr float kMessagePanelWidth = 620.0f;

  private:
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
    enum class PickerKind { None, Folder, Image, Patch };
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
    // The controllers the game would see, and the pads they play as, from the game's
    // --list-devices. Row 0 is the keyboard.
    void open_players();
    // The Mods panel: the mods this game's disc allows, from `mods.json` and the folders
    // beside the games, with the ones that will be written when Apply is pressed.
    void open_mods();
    // The mods this disc allows and their state on disk, plus what is applied from settings.
    void refresh_mods();
    // Checks or unchecks a row. Nothing is written until apply_mods(): closing the panel
    // without it leaves the game as it was.
    void toggle_mod(const std::wstring& id);
    bool mod_checked(const std::wstring& id) const;
    // Whether a row can be applied at all: its files are here, or the catalog says where
    // to get them.
    bool mod_downloadable(const std::wstring& id) const;
    std::wstring mods_summary() const;
    // How many mod rows the panel draws, so a folder with a dozen mods still opens.
    std::size_t mod_rows() const;
    const ModChoice* find_mod(const std::wstring& id) const;
    // Fetches the checked mods this machine does not have, then writes the overlay and
    // the patch the runtime reads.
    void apply_mods();
    void apply_mods_now();
    void commit_mods(const fs::path& delta, const PatchFit& fit);
    void download_mods(const std::vector<std::wstring>& ids);
    // Lays the applied mods' files over the extracted disc, as the list the runtime reads.
    void write_mod_overlay();
    // The disc patch this game is played with: what the tree carries, else what was chosen.
    void refresh_patch();
    void choose_patch();
    // Adds a delta from this machine to the table, checked, without applying it.
    void stage_patch(const fs::path& delta);

    // The Geckos panel: this disc's own codes and the ones its community guide holds, each
    // switched on by itself. Unlike a mod, a code adds nothing to the disc, so the panel
    // writes the same `mods.gecko`/`mods.code` the mods do, alongside theirs.
    void open_geckos();
    // What this disc offers and what settings has switched on, read back from disk.
    void refresh_geckos();
    void toggle_code(const std::wstring& name);
    bool code_checked(const std::wstring& name) const;
    std::size_t code_rows() const;
    const CodeEntry* find_code(const std::wstring& name) const;
    // Reads the box, adds what can run, then writes the checks and the program.
    void apply_geckos();
    // Fetches the guide's answer for this disc into geckos/online/, without blocking the
    // window: the panel shows the list it has until the answer arrives.
    void fetch_online_codes();
    // The codes switched on, as the program builder wants them, in the order they were
    // switched on. Read from disk so a run that never opened the panel still gets them.
    std::vector<CodeEntry> enabled_code_entries() const;

    // Asks the game which controllers it sees. Deciding who plays as whom is DeviceMap's.
    void refresh_devices();
    // The pads this picker owns, written to controls.txt beside the player's own mapping.
    void write_pad_pins();
    void activate(Id id);
    bool modal_active() const {
        return settings_open_ || overlay_.open() || picker_ != PickerKind::None;
    }

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
    UiContext ui_{painter_};
    Overlay overlay_;
    DeviceMap device_map_;

    Settings settings_;
    std::optional<Project> project_;
    ProjectStatus status_;
    std::map<std::wstring, Cover> covers_;
    Cover placeholder_;
    // The box art download, and whether one is in flight. Quitting sets the stop flag so
    // the worker does not hold the exit up for the rest of its downloads.
    std::thread covers_thread_;
    std::atomic_bool covers_stop_{false};
    bool covers_fetching_ = false;

    Job job_;
    GameProcess game_;

    Activity activity_;
    bool log_visible_ = false;
    int log_offset_ = 0;
    bool busy_ = false;
    bool quit_ = false;
    bool job_exits_ = false;
    bool job_then_launch_ = false;
    // The job in flight is a build, so what it compiled in is recorded when it succeeds.
    bool job_build_ = false;
    // A mod download or patch check says why it failed in its own panel, so the generic
    // "the step did not finish" prompt would only close that panel as it appeared.
    bool job_quiet_ = false;
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

    bool rendering_ = false;  // --render: no game to ask, so the rows are stand-ins
    // A render has no window to click in, so the page's two live states -- a step running,
    // and the game itself up -- are asked for by name rather than found on the machine.
    bool forced_busy_ = false;
    bool forced_running_ = false;

    // The Mods table. `mod_entries_` is what this disc may play; `pending_mods_` and
    // `pending_patch_` are the checks the panel shows, and `enabled_mods_` and
    // `applied_patch_id_` are what has been written. A later mod wins where two replace
    // the same file, so `enabled_mods_` keeps the order they were switched on.
    std::vector<CatalogMod> catalog_mods_;
    std::vector<ModChoice> mod_entries_;
    std::vector<std::wstring> enabled_mods_;
    std::vector<std::wstring> pending_mods_;
    std::wstring applied_patch_id_;
    std::wstring pending_patch_;
    std::wstring mods_note_;   // the line under the table: why it is empty, or what Apply did
    std::wstring mods_log_;    // what the Activity log already says about the mods
    std::wstring catalog_log_; // the catalog error Activity already reported
    // The disc patch the game is played with, and what checking it against the extracted
    // disc found. The delta lives wherever the player keeps it; only its path is recorded.
    fs::path patch_delta_;
    PatchFit patch_fit_;
    bool patch_checking_ = false;
    std::wstring patch_note_;


    // The Geckos table. `code_entries_` is every code this disc offers; `pending_codes_` is
    // the staged choice the panel shows, and `enabled_codes_` is what has been written. The
    // order is the order they were switched on, so a later code wins at a shared address.
    std::vector<CodeEntry> code_entries_;
    std::vector<std::wstring> enabled_codes_;
    std::vector<std::wstring> pending_codes_;
    // How many rows the panel has room for at this window size. The panel is a list of
    // unknown length rather than a fixed one, so the count comes from the height it is
    // drawn in: a short window shows fewer rows instead of a panel taller than the window.
    std::size_t code_rows_fit_ = kCodeRows;
    std::wstring codes_note_;  // the line under the table: why it is empty, or what Apply did
    std::size_t code_offset_ = 0;  // the first row drawn, for a list longer than the panel
    std::wstring online_label_;    // the guide the cached list came from, when it did
    // The box a block of codes is pasted into. There is no caret to place: the panel takes
    // what is typed at the end, and Backspace takes it back.
    std::wstring gecko_edit_;
    bool gecko_editing_ = false;
    bool codes_fetching_ = false;
    // A guide that could not be fetched is not asked for again this run: refresh() is called
    // after every change, and a machine with no network would otherwise retry each time.
    // Opening the panel clears it, because with no button to press that is how a player asks
    // for another try.
    bool codes_fetch_tried_ = false;
    bool codes_fetch_failed_ = false;
    std::thread codes_thread_;
    std::atomic_bool codes_stop_{false};


    // The newest release, and what came of looking for it.
    UpdateState update_state_ = UpdateState::Never;
    ReleaseInfo update_release_;
    std::wstring update_error_;
    std::thread update_thread_;

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

    float width_ = 1060;
    float height_ = 820;
    Rect log_rect_{};
    bool log_hovered_ = false;
};

}  // namespace nla
