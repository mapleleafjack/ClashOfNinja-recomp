#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "jobs.h"
#include "version.h"

namespace nla {

namespace fs = std::filesystem;

struct ReleaseInfo {
    std::wstring version;    // "1.1.0", without the leading v
    std::wstring page_url;   // the release page, for when the launcher cannot replace itself
    std::wstring asset_url;  // the zip built for this platform; empty when there is none
    std::wstring asset_name;
};

struct UpdateCheck {
    enum class State {
        Failed,     // the check itself did not work: no release, no network, no curl
        UpToDate,
        Available,
    };
    State state = State::Failed;
    ReleaseInfo release;
    std::wstring error;
};

// The newest published release, asked of the GitHub API. Blocking: run it off the UI
// thread. A build without a released version never asks.
UpdateCheck check_for_update();

// curl, which the launcher uses for everything it fetches: the update check, a release
// download, and the games' box art. Empty when the machine has none.
std::wstring curl_tool();

// Dotted versions, compared field by field; a leading v and a -suffix are ignored, so
// "v1.2.0" is newer than "1.1.9" and "1.2.0-beta1" is not newer than "1.2.0".
bool is_newer_version(const std::wstring& candidate, const std::wstring& current);

// Reads the release out of the API's JSON. `platform` is the asset name part that makes
// an asset this platform's: "macos", "windows" or "linux". An update replaces the launcher
// alone, so the tools bundle published beside it is never the asset that is chosen.
ReleaseInfo parse_release(const std::wstring& json, const std::wstring& platform);

// The path an update replaces: the .app bundle on macOS, the install folder on Windows,
// the executable elsewhere. Empty when this copy cannot safely be replaced in place.
fs::path install_target();

// False when the folder the swap happens in is not writable, so the launcher must offer
// the release page instead of quitting and then failing to put the new version in place.
bool install_writable();

// Where the download and unpacking happen, and what an earlier update left behind.
fs::path update_dir();
void clean_up_after_update();

// Writes the helper that waits for this process to exit, puts `staged` in place of
// `target` and starts `relaunch` on it. Split from start so a test can run it against a
// stand-in install with a relaunch command of its own.
bool write_swap_script(const fs::path& script, std::wstring* error);
bool start_swap_script(const fs::path& script, const fs::path& target, const fs::path& staged,
                       const fs::path& work, const fs::path& relaunch, std::wstring* error);

// Downloads, unpacks and hands over to the helper. The steps run in the launcher's job,
// so Activity shows the download and the progress bar is live while it runs.
std::vector<Task> update_tasks(ReleaseInfo release);

}  // namespace nla
