// Checks the parts of the update check that can be checked without a network: how
// versions compare, how a release is read out of GitHub's answer, and the helper script
// that swaps a downloaded version into place. Run by ctest; `--live` also asks GitHub.

#include "git.h"
#include "update.h"
#include "util.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

namespace fs = nla::fs;

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    std::printf("%s %s\n", condition ? "ok  " : "FAIL", what);
    if (!condition) ++failures;
}

void check_text(const std::wstring& actual, const std::wstring& expected, const char* what) {
    bool same = actual == expected;
    std::printf("%s %s", same ? "ok  " : "FAIL", what);
    if (!same) std::printf(" (got \"%s\", wanted \"%s\")", nla::narrow(actual).c_str(),
                           nla::narrow(expected).c_str());
    std::printf("\n");
    if (!same) ++failures;
}

// The shape of what api.github.com/repos/<owner>/<repo>/releases/latest answers.
const char* kReleaseJson = R"({
  "url": "https://api.github.com/repos/owner/repo/releases/1",
  "html_url": "https://github.com/owner/repo/releases/tag/v1.1.0",
  "tag_name": "v1.1.0",
  "draft": false,
  "prerelease": false,
  "body": "A release with \"quotes\" and a \\ backslash.",
  "assets": [
    {
      "name": "NarutoLauncher-macOS-arm64-v1.1.0.zip",
      "browser_download_url": "https://github.com/owner/repo/releases/download/v1.1.0/NarutoLauncher-macOS-arm64-v1.1.0.zip"
    },
    {
      "name": "NarutoLauncher-tools-windows-x64-v1.1.0.zip",
      "browser_download_url": "https://github.com/owner/repo/releases/download/v1.1.0/NarutoLauncher-tools-windows-x64-v1.1.0.zip"
    },
    {
      "name": "NarutoLauncher-Windows-x64-v1.1.0.zip",
      "browser_download_url": "https://github.com/owner/repo/releases/download/v1.1.0/NarutoLauncher-Windows-x64-v1.1.0.zip"
    },
    {
      "name": "NarutoLauncher-Windows-x64-lean-v1.1.0.zip",
      "browser_download_url": "https://github.com/owner/repo/releases/download/v1.1.0/NarutoLauncher-Windows-x64-lean-v1.1.0.zip"
    }
  ]
})";

void versions() {
    using nla::is_newer_version;
    check(is_newer_version(L"1.1.0", L"1.0.0"), "1.1.0 is newer than 1.0.0");
    check(is_newer_version(L"v1.1.0", L"1.0"), "v1.1.0 is newer than 1.0");
    check(!is_newer_version(L"1.0.0", L"1.0.0"), "the same version is not newer");
    check(!is_newer_version(L"1.0", L"1.0.0"), "1.0 and 1.0.0 are the same version");
    check(!is_newer_version(L"1.0.9", L"1.1"), "1.0.9 is not newer than 1.1");
    check(is_newer_version(L"2", L"1.99.99"), "2 is newer than 1.99.99");
    check(!is_newer_version(L"1.2.0-beta1", L"1.2.0"), "a beta of a release is not newer");
    check(!is_newer_version(L"", L"1.0"), "an empty version is never newer");
    check(!is_newer_version(L"0.0.0", L"1.0"), "0.0.0 is not newer than 1.0");
}

void releases() {
    auto json = nla::widen(kReleaseJson);
    auto mac = nla::parse_release(json, L"macos");
    check_text(mac.version, L"1.1.0", "the tag is read without its v");
    check_text(mac.page_url, L"https://github.com/owner/repo/releases/tag/v1.1.0",
               "the release page is read");
    check_text(mac.asset_name, L"NarutoLauncher-macOS-arm64-v1.1.0.zip",
               "macOS picks the macOS zip");
    check(mac.asset_url == L"https://github.com/owner/repo/releases/download/v1.1.0/"
                           L"NarutoLauncher-macOS-arm64-v1.1.0.zip",
          "macOS takes the download link of that zip");

    auto windows = nla::parse_release(json, L"windows");
    check_text(windows.asset_name, L"NarutoLauncher-Windows-x64-v1.1.0.zip",
               "Windows picks the full zip, not the lean one");
    check(windows.asset_url.find(L"-tools-") == std::wstring::npos,
          "Windows never downloads the tools archive as an update");

    // A release that carries only the tools bundle has nothing this copy can become, so
    // the launcher offers its release page instead of downloading 160 MB and failing.
    const std::wstring tools_name = L"NarutoLauncher-tools-windows-x64-v1.1.0.zip";
    auto tools_only = nla::parse_release(
        L"{\"tag_name\":\"v1.1.0\",\"assets\":[{\"name\":\"" + tools_name +
            L"\",\"browser_download_url\":\"https://github.com/owner/repo/releases/download/"
            L"v1.1.0/" + tools_name + L"\"}]}",
        L"windows");
    check(tools_only.asset_url.empty(), "a tools-only release has no download for Windows");

    auto linux = nla::parse_release(json, L"linux");
    check(linux.version == L"1.1.0", "a release without this platform still names its version");
    check(linux.asset_url.empty(), "a release without a linux zip has no download for it");

    auto rubbish = nla::parse_release(L"<html>not json</html>", L"macos");
    check(rubbish.version.empty(), "rubbish answers no release");
}

// Runs the generated helper against a stand-in install and checks the swap it makes.
void swap() {
    std::error_code ec;
    auto root = nla::temp_dir() / (L"nla-update-test-" + std::to_wstring(nla::process_id()));
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);

    auto target = root / L"Install" / L"Stand In.app";
    auto staged = root / L"Staged" / L"Stand In.app";
    auto work = root / L"Work";
    std::wstring error;
    bool written = true;
#if defined(_WIN32)
    // The PowerShell helper waits for a pid, then copies the folder over the install.
    fs::create_directories(target, ec);
    fs::create_directories(staged, ec);
    fs::create_directories(work, ec);
    written = nla::write_text_file(target / L"old.txt", "old");
    written = nla::write_text_file(staged / L"new.txt", "new") && written;
    auto script = root / L"apply-update.ps1";
    written = nla::write_swap_script(script, &error) && written;
#else
    // The shell helper waits for a pid, then puts the staged folder in place of the
    // install and starts the relaunch command on it.
    fs::create_directories(target, ec);
    fs::create_directories(work, ec);
    fs::create_directories(staged / L"Contents" / L"MacOS", ec);
    written = nla::write_text_file(target / L"old-marker", "old");
    written = nla::write_text_file(staged / L"Contents" / L"MacOS" / L"new-marker", "new") &&
              written;
    auto script = root / L"apply-update.sh";
    written = nla::write_swap_script(script, &error) && written;
#endif
    check(written, "the helper script is written");
    if (!written) {
        std::printf("     %s\n", nla::narrow(error).c_str());
        fs::remove_all(root, ec);
        return;
    }

#if defined(_WIN32)
    // Nothing here can start the helper the way Windows does and wait for it, so only the
    // script's text is checked.
    auto text = nla::read_text_file(root / L"apply-update.ps1");
    check(text.find(L"Copy-Item") != std::wstring::npos, "the helper copies over the install");
    check(text.find(L"Start-Process") != std::wstring::npos, "the helper starts the launcher");
#else
    // A pid that is certainly gone, so the helper does not wait for anything.
    std::wstring gone = L"2147483647";
    auto command = nla::command_line(
        L"/bin/sh", {script.wstring(), gone, target.wstring(), staged.wstring(),
                     work.wstring(), L"/usr/bin/true"});
    std::system(nla::narrow(command).c_str());
    check(!fs::exists(target / L"old-marker", ec), "the old copy is gone after the swap");
    check(fs::exists(target / L"Contents" / L"MacOS" / L"new-marker", ec),
          "the new copy is in place after the swap");
    check(!fs::exists(work, ec), "the helper removes what it downloaded");
    check(!fs::exists(script, ec), "the helper removes itself");
#endif
    fs::remove_all(root, ec);
}

// Drives the real update-the-project steps against a throwaway repository: a clone that
// is behind its origin fast-forwards, and a dirty clone is refused.
void project() {
    std::wstring git_exe = nla::git_tool();
    if (git_exe.empty()) {
        std::printf("skip the project update checks: git was not found\n");
        return;
    }
    std::error_code ec;
    auto root = nla::temp_dir() / (L"nla-git-test-" + std::to_wstring(nla::process_id()));
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    auto origin = root / L"origin";
    auto work = root / L"work";
    auto other = root / L"other";

    auto run = [&](const fs::path& where, const std::vector<std::wstring>& extra) {
        // autocrlf off: the test compares file contents, which a checkout would rewrite.
        std::vector<std::wstring> args{L"-c", L"user.name=Launcher Test", L"-c",
                                       L"user.email=test@example.com", L"-c",
                                       L"core.autocrlf=false", L"-C", where.wstring()};
        args.insert(args.end(), extra.begin(), extra.end());
        std::string output;
        int status = -1;
        nla::capture_output(git_exe, args, &output, &status);
        return status;
    };

    // An origin with one commit, cloned twice.
    bool setup = run(root, {L"init", L"--bare", L"origin"}) == 0;
    setup = setup && run(root, {L"clone", origin.wstring(), work.wstring()}) == 0;
    setup = setup && nla::write_text_file(work / L"file.txt", "one");
    setup = setup && run(work, {L"add", L"."}) == 0;
    setup = setup && run(work, {L"commit", L"-m", L"one"}) == 0;
    setup = setup && run(work, {L"push", L"-u", L"origin", L"HEAD"}) == 0;
    setup = setup && run(root, {L"clone", origin.wstring(), other.wstring()}) == 0;
    // ... and a commit in the second clone, pushed to origin.
    setup = setup && nla::write_text_file(other / L"file.txt", "two");
    setup = setup && run(other, {L"add", L"."}) == 0;
    setup = setup && run(other, {L"commit", L"-m", L"two"}) == 0;
    setup = setup && run(other, {L"push", L"origin", L"HEAD"}) == 0;
    check(setup, "the throwaway repository is set up");
    if (!setup) {
        fs::remove_all(root, ec);
        return;
    }

    check(nla::project_is_checkout(work), "a clone is recognised as a checkout");
    check(nla::project_is_clean(work, nullptr), "a fresh clone is clean");
    check(!nla::project_revision(work).empty(), "a clone reports its revision");

    // The commit the first clone does not have yet: the skew the panel warns about.
    std::string newer_output;
    int newer_status = -1;
    nla::capture_output(git_exe, {L"-C", other.wstring(), L"rev-parse", L"HEAD"},
                        &newer_output, &newer_status);
    auto newer = nla::trim(nla::widen(newer_output));
    check(newer_status == 0 && !newer.empty(), "the newest commit is known");
    check(nla::project_is_older_than(work, newer, 4102444800LL),
          "the older clone is reported as older than a later source");

    auto apply = [](const fs::path& root) {
        nla::Job job;
        std::atomic<int> code{-1};
        bool done = false;
        job.start(nla::source_tasks(root), [](const std::wstring&) {}, [](const std::wstring&) {},
                  nullptr, [&](int c) { code.store(c); done = true; });
        for (int i = 0; i < 600 && !done; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        job.shutdown();
        return code.load();
    };

    check(apply(work) == 0, "an out-of-date clone updates");
    check(nla::trim(nla::read_text_file(work / L"file.txt")) == L"two",
          "the new source is in place");
    check(!nla::project_is_older_than(work, newer, 4102444800LL),
          "the updated clone is not older");

    nla::write_text_file(work / L"file.txt", nla::narrow(L"local change"));
    check(apply(work) != 0, "a clone with local changes is refused");
    check(nla::trim(nla::read_text_file(work / L"file.txt")) == L"local change",
          "a refused update leaves the local change alone");
    fs::remove_all(root, ec);
}

int run(bool live) {
    versions();
    releases();
    swap();
    project();

    if (live) {
        auto result = nla::check_for_update();
        std::printf("live check: state %d, version \"%s\", asset \"%s\"\n",
                    static_cast<int>(result.state),
                    nla::narrow(result.release.version).c_str(),
                    nla::narrow(result.release.asset_name).c_str());
        if (!result.error.empty()) {
            std::printf("live check said: %s\n", nla::narrow(result.error).c_str());
        }
    }

    std::printf("%s\n", failures ? "some checks failed" : "all checks passed");
    return failures ? 1 : 0;
}

}  // namespace

// MinGW links with -municode on Windows, which wants wmain.
#if defined(_WIN32)
int wmain(int argc, wchar_t** argv) {
    return run(argc > 1 && std::wstring(argv[1]) == L"--live");
}
#else
int main(int argc, char** argv) {
    return run(argc > 1 && std::string(argv[1]) == "--live");
}
#endif
