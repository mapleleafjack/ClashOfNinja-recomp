#pragma once

#include <string>
#include <thread>

#include "screen.h"
#include "update.h"

namespace nla {

// Looking for a newer release of the launcher, installing it, and what the Settings page
// says about it. Apart from the window: the check runs on its own thread, and what it finds
// is state that the page, the header button and the prompt all read.
class UpdateScreen : public Screen {
  public:
    // What the page says about the last check. Separate from UpdateCheck::State, which is
    // one check's outcome: this is where the panel is now, including "not asked yet" and
    // "asking".
    enum class State { Never, Checking, UpToDate, Available, Failed };

    explicit UpdateScreen(ScreenHost& host) : host_(host) {}

    // Joins the check's thread. The window calls this before it goes away, because the
    // answer it posts would otherwise arrive at an object that is no longer there.
    void shutdown();

    // `manual` is the "Check now" button: it asks even when the setting is off, and reports
    // a failure. A start-up check says nothing unless there is something to install.
    void start(bool manual);
    void on_checked(UpdateCheck result, bool manual);

    // The prompt offering the release, when there is one that can be installed here.
    void show_prompt();
    void begin();
    void open_page();

    // A release to draw, for a render with no network: the state the page would be in after
    // a check had found one.
    void set_stand_in(ReleaseInfo release);

    State state() const { return state_; }
    // Empty until a check has found a release.
    const std::wstring& version() const { return release_.version; }
    const std::wstring& error() const { return error_; }
    // The line under UPDATES on the Settings page. The colour it is drawn in is the page's,
    // which is why this is the words only.
    std::wstring line() const;

    bool handles(Id id) const override;
    void activate(Id id) override;

  private:
    ScreenHost& host_;
    State state_ = State::Never;
    ReleaseInfo release_;
    std::wstring error_;
    std::thread thread_;
};

}  // namespace nla
