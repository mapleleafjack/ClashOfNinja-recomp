// Looking for a newer release of the launcher, and installing it.

#include "app.h"

#include "util.h"

#include <thread>

namespace nla {

void App::start_update_check(bool manual) {
    if (update_state_ == UpdateState::Checking) return;
    if (!manual && !settings_.check_updates) return;
    if (development_build()) {
        update_state_ = UpdateState::Failed;
        update_error_ = L"This build was not made from a release tag.";
        if (manual) show_message(L"Updates", update_error_);
        invalidate();
        return;
    }
    if (update_thread_.joinable()) update_thread_.join();
    update_state_ = UpdateState::Checking;
    update_error_.clear();
    append_log(format(L"[launcher] checking for updates (version %ls)\n",
                      launcher_version().c_str()));
    update_thread_ = std::thread([this, manual] {
        auto result = check_for_update();
        post([this, result, manual] { on_update_checked(result, manual); });
    });
    invalidate();
}

void App::on_update_checked(UpdateCheck result, bool manual) {
    switch (result.state) {
        case UpdateCheck::State::Available:
            update_state_ = UpdateState::Available;
            update_release_ = result.release;
            append_log(format(L"[launcher] version %ls is available (this is %ls)\n",
                              result.release.version.c_str(), launcher_version().c_str()));
            // At start the prompt is an interruption, so a version the player already
            // answered "not now" to waits behind the header button instead, and a step
            // that is running is never talked over.
            if (!busy_ && (manual || settings_.skipped_update != result.release.version)) {
                show_update_prompt();
            }
            break;
        case UpdateCheck::State::UpToDate:
            update_state_ = UpdateState::UpToDate;
            append_log(L"[launcher] this is the newest release\n");
            if (manual) {
                settings_note_ = L"GitHub was checked: this is the newest release.";
            }
            break;
        case UpdateCheck::State::Failed:
            update_state_ = UpdateState::Failed;
            update_error_ = result.error;
            append_log(L"[launcher] update check failed: " + result.error + L"\n");
            if (manual) show_message(L"Cannot check for updates", result.error);
            break;
    }
    invalidate();
}

void App::show_update_prompt() {
    if (update_release_.version.empty()) return;
    auto target = install_target();
    bool writable = install_writable();
    if (update_release_.asset_url.empty() || !writable) {
        std::wstring text = format(L"You have version %ls. ", launcher_version().c_str());
        text += update_release_.asset_url.empty()
                    ? L"This release has no download for this system, so the launcher "
                      L"cannot install it itself."
                    : L"This copy of the launcher sits in a folder it cannot write to, so "
                      L"it cannot replace itself.";
        text += L" The release page has the download.";
        show_confirm(format(L"Version %ls is available", update_release_.version.c_str()),
                     text, L"Open release page", [this] { open_update_page(); });
        overlay_update_ = true;
        return;
    }
    std::wstring text = format(L"You have version %ls. The launcher can download version "
                               L"%ls, replace this copy and start it again.",
                               launcher_version().c_str(),
                               update_release_.version.c_str());
    if (!target.empty()) {
        // The path is cut in the middle, not at the end: the folder it is in is as much
        // part of "what gets replaced" as the name is, and a path left to wrap breaks at a
        // slash in the middle of nothing.
        text += L"\n\nIt replaces:\n" +
                ui_.elide_middle(path_display(target), Face::Body, kMessagePanelWidth - 52.0f);
    }
    show_confirm(format(L"Version %ls is available", update_release_.version.c_str()), text,
                 L"Update now", [this] { begin_update(); });
    overlay_update_ = true;
}

void App::begin_update() {
    if (busy_ || update_release_.version.empty()) return;
    if (game_.running()) {
        // Replacing the launcher ends this process, and the game goes with it.
        show_message(L"Close the game first",
                     L"The update restarts the launcher, which would end the game. Stop it, "
                     L"then install the update.");
        return;
    }
    if (update_release_.asset_url.empty() || !install_writable()) {
        open_update_page();
        return;
    }
    start_job(update_tasks(update_release_), false, true);
}

void App::open_update_page() {
    if (update_release_.page_url.empty()) {
        show_message(L"Updates", L"This release has no page to open.");
        return;
    }
    std::wstring error;
    if (!open_url(update_release_.page_url, &error)) {
        show_message(L"Cannot open", error.empty() ? update_release_.page_url : error);
    }
}

}  // namespace nla
