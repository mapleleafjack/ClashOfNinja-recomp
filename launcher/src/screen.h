#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "jobs.h"
#include "picker.h"
#include "settings.h"
#include "ui.h"

namespace nla {

// What the platform's own chooser answered. `shown` keeps "the player cancelled" apart from
// "this system has no chooser, or would not open one", where the launcher falls back to its
// own list.
struct NativeAnswer {
    bool shown = false;
    // The system has a chooser but would not open one. Worth saying, because the list that
    // follows is not what was asked for; a render has no window and is not this.
    bool refused = false;
    std::optional<std::filesystem::path> path;
};

// What a screen may ask the window for: a redraw, the prompts, the job runner, and the
// settings. A screen holds one of these rather than the App, so what it depends on is
// written down here instead of being whatever the window class happened to expose.
//
// The callbacks are filled in by App once, in its constructor, and are only ever called
// later, so a screen cannot see a half-built window.
struct ScreenHost {
    std::function<void()> invalidate;
    // Hands work to the UI thread, which is how a worker's answer gets back.
    std::function<void(std::function<void()>)> post;
    // The window's text, for a page with a field that pastes.
    std::function<std::wstring()> clipboard;
    // Whether the window is taking typed text, which only a field wants.
    std::function<void(bool)> text_input;
    // The platform's own chooser, which is the window's to show. It answers `shown` false
    // when there is no window to show one over.
    std::function<NativeAnswer(PickerKind, const std::wstring&, const std::filesystem::path&)>
        native_chooser;
    // This page is now the one showing: whatever was over the main page closes, so two
    // pages are never drawn on top of each other.
    std::function<void()> take_over;

    std::function<void(const std::wstring&)> log;
    std::function<void(const std::wstring& title, const std::wstring& text)> message;
    // A prompt about a newer release, which also remembers that closing it means "not this
    // one", so the same version is not offered again on the next start.
    std::function<void(const std::wstring& title, const std::wstring& text,
                       const std::wstring& action, std::function<void()> on_action)>
        update_prompt;

    std::function<bool()> busy;
    std::function<bool()> game_running;
    std::function<Settings&()> settings;
    std::function<void(const std::wstring&)> settings_note;
    // A launch that was waiting on a chooser's answer is given up when it is cancelled.
    std::function<void()> launch_cancelled;
    std::function<void(std::vector<Task>, JobOptions)> start_job;
    // A path cut in the middle to fit the message panel, which needs the font to measure.
    std::function<std::wstring(const std::wstring&)> elide_path;
};

// A key as a page understands it, rather than as the platform reports it: a screen has no
// window and no events, so what it is told is the decision, not the hardware.
enum class ScreenKey { Escape, Enter, Tab, Backspace, Up, Down, Paste };

// One page's worth of state, and the ids it answers to. The window keeps the events and the
// drawing; a screen keeps everything else about its page, so what it knows can be checked
// without a window.
class Screen {
  public:
    virtual ~Screen() = default;

    // Which ids this screen owns. Each screen's ids are a contiguous block of Id, so this
    // is a test rather than a switch, and the window can ask without knowing the page.
    virtual bool handles(Id id) const = 0;
    // Called only when handles(id) said yes.
    virtual void activate(Id id) = 0;

    // A page with a field takes the keyboard and the wheel while it is showing, before the
    // window's own shortcuts see anything.
    virtual bool capturing() const { return false; }
    // True when the page took the key; false leaves it to the window.
    virtual bool key(ScreenKey key) { return false; }
    // Typed text, for a page with a field.
    virtual bool text(const std::wstring& typed) { return false; }
    // The wheel, in notches, counted towards the page.
    virtual bool wheel(int notches) { return false; }
};

}  // namespace nla
