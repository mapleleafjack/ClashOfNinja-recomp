#pragma once

#include <functional>
#include <string>
#include <vector>

#include "jobs.h"
#include "settings.h"
#include "ui.h"

namespace nla {

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
    std::function<void(std::vector<Task>, JobOptions)> start_job;
    // A path cut in the middle to fit the message panel, which needs the font to measure.
    std::function<std::wstring(const std::wstring&)> elide_path;
};

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
};

}  // namespace nla
