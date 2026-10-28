#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <SDL.h>

#include "painter.h"
#include "jobs.h"
#include "project.h"
#include "settings.h"
#include "tools.h"
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
    Scale,
    Scale1,
    Scale2,
    Scale3,
    Scale4,
    ToggleMute,
    ToggleInputLog,
    OverlayAction,
    OverlayCancel,
    PickerUp,
    PickerPath,
    PickerAccept,
    PickerCancel,
    PickerEntry,  // the first entry row; the rest follow it
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

    enum class Pill { Muted, Ok, Warn, Bad };
    enum class PickerKind { None, Folder, Image };

    void handle_event(const SDL_Event& event);
    void handle_key(const SDL_KeyboardEvent& key);
    void handle_text(const SDL_TextInputEvent& text);
    void resize_surface();

    void paint();
    void paint_header();
    void paint_launch_card();
    void paint_activity();
    void paint_settings();
    void paint_overlay();
    void paint_picker();
    void paint_scale_menu();

    void button(Id id, const Rect& rect, const std::wstring& label, bool primary,
                bool enabled, bool danger = false, Face face = Face::Button);
    void link(Id id, const Rect& rect, const std::wstring& label, bool enabled = true);
    void checkbox(Id id, float x, float y, const std::wstring& label, bool checked,
                  bool enabled = true);
    void scale_combo(Id id, const Rect& rect, bool enabled);
    void pill(const Rect& rect, const std::wstring& text, Pill state);
    void card_box(const Rect& rect);

    float text_width(const std::wstring& text, Face face);
    std::wstring elide(const std::wstring& text, Face face, float width);
    void register_hit(Id id, const Rect& rect);
    Id hit_at(float x, float y) const;

    void refresh();
    void locate_project();
    void choose_image();
    void start_setup(const std::filesystem::path& image);
    void begin_setup(const std::filesystem::path& image, bool then_launch);
    void start_build(bool then_launch = false);
    std::vector<Task> build_tasks();
    bool check_required_tools();
    void install_tools(bool then_launch);
    void launch();
    void continue_launch();
    void play();
    void open_path(const std::filesystem::path& path);
    void reset_controls();
    void activate(Id id);
    bool modal_active() const { return settings_open_ || overlay_open_ || picker_ != PickerKind::None; }

    void start_job(std::vector<Task> tasks, bool then_launch = false);
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

    // The in-app replacements for the platform's folder and file pickers.
    void open_picker(PickerKind kind, const std::wstring& title, const fs::path& start,
                     std::function<void(const fs::path&)> accept);
    void close_picker();
    void list_picker();
    void accept_picker();
    void fill_picker_path();

    void invalidate() { window_.invalidate(); }
    void post(std::function<void()> task) { dispatcher_.post(std::move(task)); }
    void animate();

    Window window_;
    Dispatcher dispatcher_;
    Painter painter_;

    Settings settings_;
    std::optional<Project> project_;
    ProjectStatus status_;

    Job job_;
    GameProcess game_;

    std::deque<std::wstring> log_;
    bool log_visible_ = false;
    int log_offset_ = 0;
    bool busy_ = false;
    bool job_then_launch_ = false;
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

    bool overlay_open_ = false;
    bool overlay_confirm_ = false;
    std::wstring overlay_title_;
    std::wstring overlay_text_;
    std::wstring overlay_action_;
    std::function<void()> overlay_callback_;

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
