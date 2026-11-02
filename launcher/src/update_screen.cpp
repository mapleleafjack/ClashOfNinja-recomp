// Looking for a newer release of the launcher, and installing it.

#include "update_screen.h"

#include "util.h"
#include "version.h"

namespace nla {

void UpdateScreen::shutdown() {
    if (thread_.joinable()) thread_.join();
}

void UpdateScreen::start(bool manual) {
    if (state_ == State::Checking) return;
    if (!manual && !host_.settings().check_updates) return;
    if (development_build()) {
        state_ = State::Failed;
        error_ = L"This build was not made from a release tag.";
        if (manual) host_.message(L"Updates", error_);
        host_.invalidate();
        return;
    }
    shutdown();
    state_ = State::Checking;
    error_.clear();
    host_.log(format(L"[launcher] checking for updates (version %ls)\n",
                     launcher_version().c_str()));
    thread_ = std::thread([this, manual] {
        auto result = check_for_update();
        host_.post([this, result, manual] { on_checked(result, manual); });
    });
    host_.invalidate();
}

void UpdateScreen::on_checked(UpdateCheck result, bool manual) {
    switch (result.state) {
        case UpdateCheck::State::Available:
            state_ = State::Available;
            release_ = result.release;
            host_.log(format(L"[launcher] version %ls is available (this is %ls)\n",
                             result.release.version.c_str(),
                             launcher_version().c_str()));
            // At start the prompt is an interruption, so a version the player already
            // answered "not now" to waits behind the header button instead, and a step that
            // is running is never talked over.
            if (!host_.busy() &&
                (manual || host_.settings().skipped_update != result.release.version)) {
                show_prompt();
            }
            break;
        case UpdateCheck::State::UpToDate:
            state_ = State::UpToDate;
            host_.log(L"[launcher] this is the newest release\n");
            if (manual) host_.settings_note(L"GitHub was checked: this is the newest release.");
            break;
        case UpdateCheck::State::Failed:
            state_ = State::Failed;
            error_ = result.error;
            host_.log(L"[launcher] update check failed: " + result.error + L"\n");
            if (manual) host_.message(L"Cannot check for updates", result.error);
            break;
    }
    host_.invalidate();
}

void UpdateScreen::show_prompt() {
    if (release_.version.empty()) return;
    auto target = install_target();
    bool writable = install_writable();
    if (release_.asset_url.empty() || !writable) {
        std::wstring text = format(L"You have version %ls. ", launcher_version().c_str());
        text += release_.asset_url.empty()
                    ? L"This release has no download for this system, so the launcher "
                      L"cannot install it itself."
                    : L"This copy of the launcher sits in a folder it cannot write to, so "
                      L"it cannot replace itself.";
        text += L" The release page has the download.";
        host_.update_prompt(format(L"Version %ls is available", release_.version.c_str()),
                            text, L"Open release page", [this] { open_page(); });
        return;
    }
    std::wstring text = format(L"You have version %ls. The launcher can download version "
                               L"%ls, replace this copy and start it again.",
                               launcher_version().c_str(), release_.version.c_str());
    if (!target.empty()) {
        // The path is cut in the middle, not at the end: the folder it is in is as much
        // part of "what gets replaced" as the name is, and a path left to wrap breaks at a
        // slash in the middle of nothing.
        text += L"\n\nIt replaces:\n" + host_.elide_path(path_display(target));
    }
    host_.update_prompt(format(L"Version %ls is available", release_.version.c_str()), text,
                        L"Update now", [this] { begin(); });
}

void UpdateScreen::begin() {
    if (host_.busy() || release_.version.empty()) return;
    if (host_.game_running()) {
        // Replacing the launcher ends this process, and the game goes with it.
        host_.message(L"Close the game first",
                      L"The update restarts the launcher, which would end the game. Stop it, "
                      L"then install the update.");
        return;
    }
    if (release_.asset_url.empty() || !install_writable()) {
        open_page();
        return;
    }
    host_.start_job(update_tasks(release_), {.exits = true});
}

void UpdateScreen::open_page() {
    if (release_.page_url.empty()) {
        host_.message(L"Updates", L"This release has no page to open.");
        return;
    }
    std::wstring error;
    if (!open_url(release_.page_url, &error)) {
        host_.message(L"Cannot open", error.empty() ? release_.page_url : error);
    }
}

void UpdateScreen::set_stand_in(ReleaseInfo release) {
    state_ = State::Available;
    release_ = std::move(release);
}

std::wstring UpdateScreen::line() const {
    switch (state_) {
        case State::Checking:
            return L"Asking GitHub for the newest release…";
        case State::UpToDate:
            return L"Version " + launcher_version() + L" is the newest release.";
        case State::Available:
            return L"Version " + release_.version + L" is available.";
        case State::Failed:
            return L"Version " + launcher_version() + L". " + error_;
        case State::Never:
            break;
    }
    return L"Version " + launcher_version() + L".";
}

bool UpdateScreen::handles(Id id) const { return id == Id::UpdateBanner; }

void UpdateScreen::activate(Id id) {
    if (id == Id::UpdateBanner) show_prompt();
}

}  // namespace nla
