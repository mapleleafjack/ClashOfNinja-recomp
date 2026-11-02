#include "update.h"

#include "tools.h"
#include "util.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <initializer_list>

namespace nla {

namespace {

// The launcher asks this repository for its releases, unauthenticated.
constexpr wchar_t kRepository[] = L"mapleleafjack/ClashOfNinja-recomp";

std::wstring latest_release_url() {
    return std::wstring(L"https://api.github.com/repos/") + kRepository + L"/releases/latest";
}

// The string value of `key`, searched from `from`. `after` receives the offset just past
// the closing quote, so a caller can walk a list of values.
std::wstring json_string(const std::wstring& json, const wchar_t* key, size_t from,
                         size_t* after) {
    std::wstring needle = std::wstring(L"\"") + key + L"\"";
    size_t at = json.find(needle, from);
    if (at == std::wstring::npos) return {};
    at = json.find(L':', at + needle.size());
    if (at == std::wstring::npos) return {};
    ++at;
    while (at < json.size() && (json[at] == L' ' || json[at] == L'\t' || json[at] == L'\n' ||
                                json[at] == L'\r')) {
        ++at;
    }
    if (at >= json.size() || json[at] != L'"') return {};
    std::wstring value;
    for (size_t index = at + 1; index < json.size(); ++index) {
        wchar_t character = json[index];
        if (character == L'\\' && index + 1 < json.size()) {
            value.push_back(json[++index]);
            continue;
        }
        if (character == L'"') {
            if (after) *after = index + 1;
            return value;
        }
        value.push_back(character);
    }
    return {};
}

std::wstring json_string(const std::wstring& json, const wchar_t* key) {
    return json_string(json, key, 0, nullptr);
}

std::vector<std::wstring> json_strings(const std::wstring& json, const wchar_t* key) {
    std::vector<std::wstring> values;
    size_t from = 0;
    for (;;) {
        size_t after = 0;
        auto value = json_string(json, key, from, &after);
        if (after == 0) break;
        values.push_back(value);
        from = after;
    }
    return values;
}

std::wstring file_name_of(const std::wstring& url) {
    auto slash = url.find_last_of(L'/');
    return slash == std::wstring::npos ? url : url.substr(slash + 1);
}

// "v1.2.3-beta.1" compares as 1.2.3: a suffix does not order against the release itself.
std::vector<int> version_fields(const std::wstring& text) {
    std::vector<int> fields;
    int value = -1;
    for (wchar_t character : text) {
        if (character >= L'0' && character <= L'9') {
            if (value < 0) value = 0;
            if (value < 100000) value = value * 10 + (character - L'0');
        } else if (value >= 0) {
            fields.push_back(value);
            value = -1;
        }
    }
    if (value >= 0) fields.push_back(value);
    return fields;
}

std::vector<int> version_fields_of(wstring_view text) {
    auto core = text.substr(0, text.find_first_of(L"-+"));
    return version_fields(std::wstring(core));
}

std::wstring platform_asset_tag() {
#if defined(_WIN32)
    return L"windows";
#elif defined(__APPLE__)
    return L"macos";
#else
    return L"linux";
#endif
}

// The archive a release publishes for a platform: the Linux job writes a tar.xz, because
// GNU tar is all a Linux copy has to unpack with, and the macOS and Windows jobs write a
// zip, which their bsdtar reads. Comparing only against ".zip" left a Linux copy with
// nothing to download.
std::wstring_view launcher_archive_suffix(std::wstring_view platform) {
    return platform == L"linux" ? std::wstring_view(L".tar.xz")
                                : std::wstring_view(L".zip");
}

fs::path find_payload(const fs::path& root) {
    std::error_code ec;
    if (!fs::is_directory(root, ec)) return {};
    auto exe_name = fs::path(exe_path()).filename();
    std::vector<fs::path> level{root};
    for (int depth = 0; depth < 3 && !level.empty(); ++depth) {
        std::vector<fs::path> next;
        for (const auto& directory : level) {
            // macOS ships the app in a folder named after the release.
            if (ends_with_ci(directory.filename().wstring(), L".app")) return directory;
            if (!exe_name.empty() && fs::exists(directory / exe_name, ec)) return directory;
            for (const auto& entry : fs::directory_iterator(
                     directory, fs::directory_options::skip_permission_denied, ec)) {
                std::error_code item_ec;
                if (entry.is_directory(item_ec)) next.push_back(entry.path());
            }
        }
        level = std::move(next);
    }
    return {};
}

}  // namespace

namespace {

// The tar the launcher unpacks with. It is bsdtar on Windows and macOS, which reads a zip
// as well; a Linux /usr/bin/tar is GNU tar and reads only tar archives.
std::wstring tar_tool() {
#if defined(_WIN32)
    return find_tool_from({L"C:\\Windows\\System32\\tar.exe"}, L"tar.exe");
#else
    return find_tool_from({L"/usr/bin/tar", L"/usr/local/bin/tar"}, L"tar");
#endif
}

// The interpreter Settings already lists as needed on every platform, and the last tool
// that can open a zip where neither bsdtar nor unzip is installed.
std::wstring python_tool() {
    auto python = find_tool(L"python3" + std::wstring(exe_suffix()));
    if (python.empty()) python = find_tool(L"python" + std::wstring(exe_suffix()));
    return python;
}

// The bytes a zip starts with: a local file header, an empty archive's end record, or a
// record of a spanned one. A tar, compressed or not, begins with something else.
bool looks_like_zip(const fs::path& archive) {
    std::ifstream file(archive, std::ios::binary);
    unsigned char magic[4] = {};
    if (!file.read(reinterpret_cast<char*>(magic), sizeof magic)) return false;
    return magic[0] == 'P' && magic[1] == 'K' &&
           (magic[2] == 3 || magic[2] == 5 || magic[2] == 7);
}

struct Unpacker {
    std::wstring exe;
    std::vector<std::wstring> args;
};

// The tools that can open a zip here, best first. Linux tar is left out: GNU tar answers a
// zip with "This does not look like a tar archive" and nothing else, so offering it only
// fails once before the tool that works is tried.
std::vector<Unpacker> zip_unpackers(const fs::path& archive, const fs::path& into) {
    std::vector<Unpacker> unpackers;
#if defined(__linux__)
    const auto bsdtar =
        find_tool_from({L"/usr/bin/bsdtar", L"/usr/local/bin/bsdtar"}, L"bsdtar");
    if (!bsdtar.empty())
        unpackers.push_back({bsdtar, {L"-xf", archive.wstring(), L"-C", into.wstring()}});
    const auto unzip = find_tool(L"unzip");
    if (!unzip.empty())
        unpackers.push_back({unzip, {L"-q", L"-o", archive.wstring(), L"-d", into.wstring()}});
#else
    const auto tar = tar_tool();
    if (!tar.empty())
        unpackers.push_back({tar, {L"-xf", archive.wstring(), L"-C", into.wstring()}});
#endif
    const auto python = python_tool();
    if (!python.empty())
        unpackers.push_back(
            {python, {L"-m", L"zipfile", L"-e", archive.wstring(), into.wstring()}});
    return unpackers;
}

}  // namespace

bool unpack_archive(const fs::path& archive, const fs::path& into, Job& job,
                    std::wstring* error) {
    std::vector<Unpacker> unpackers;
    if (looks_like_zip(archive)) {
        unpackers = zip_unpackers(archive, into);
        if (unpackers.empty()) {
            if (error)
                *error = format(L"%ls is a zip and no tool here opens one: install bsdtar, "
                                L"unzip or python3",
                                archive.filename().c_str());
            return false;
        }
    } else {
        const auto tar = tar_tool();
        if (tar.empty()) {
            if (error) *error = L"tar was not found, so the download cannot be unpacked";
            return false;
        }
        unpackers.push_back({tar, {L"-xf", archive.wstring(), L"-C", into.wstring()}});
    }

    int code = 1;
    std::wstring tool;
    for (size_t index = 0; index < unpackers.size(); ++index) {
        if (index > 0) {
            // A tool that failed part-way leaves files behind, so the next one starts from
            // the empty folder the caller made.
            std::error_code ec;
            fs::remove_all(into, ec);
            fs::create_directories(into, ec);
        }
        code = job.run_process(unpackers[index].exe, unpackers[index].args, fs::path());
        if (code == 0) return true;
        if (job.cancelled()) {
            if (error) *error = L"cancelled";
            return false;
        }
        tool = fs::path(unpackers[index].exe).filename().wstring();
    }
    if (error)
        *error = format(L"%ls could not be unpacked: %ls exited with status %d",
                        archive.filename().c_str(), tool.c_str(), code);
    return false;
}

bool is_newer_version(const std::wstring& candidate, const std::wstring& current) {
    auto left = version_fields_of(candidate);
    auto right = version_fields_of(current);
    if (left.empty()) return false;
    size_t count = std::max(left.size(), right.size());
    for (size_t index = 0; index < count; ++index) {
        int a = index < left.size() ? left[index] : 0;
        int b = index < right.size() ? right[index] : 0;
        if (a != b) return a > b;
    }
    return false;
}

ReleaseInfo parse_release(const std::wstring& json, const std::wstring& platform) {
    ReleaseInfo release;
    auto tag = json_string(json, L"tag_name");
    if (tag.empty()) return release;
    release.version = (tag.front() == L'v' || tag.front() == L'V') ? tag.substr(1) : tag;
    release.page_url = json_string(json, L"html_url");

    // The asset for this machine: an archive named after the platform, the newest version
    // and this architecture. The lean Windows zip is never a release, but skip it anyway.
    // The tools archive sits on the release page beside the launcher's own and holds no
    // launcher, so an update must never take it however its name compares: the tools a
    // player already has stay where they are.
    auto version = to_lower(release.version);
    auto launcher_name = to_lower(fs::path(exe_path()).filename().stem().wstring());
    const std::wstring_view suffix = launcher_archive_suffix(platform);
    int best = -1;
    for (const auto& url : json_strings(json, L"browser_download_url")) {
        auto name = file_name_of(url);
        auto folded = to_lower(name);
        if (!ends_with_ci(folded, suffix)) continue;
        if (folded.find(L"lean") != std::wstring::npos) continue;
        if (folded.find(L"tools") != std::wstring::npos) continue;
        if (folded.find(platform) == std::wstring::npos) continue;
        int score = 0;
        if (!launcher_name.empty() && folded.find(launcher_name) != std::wstring::npos) score += 4;
        if (folded.find(version) != std::wstring::npos) score += 2;
#if defined(__aarch64__) || defined(_M_ARM64)
        if (folded.find(L"arm64") != std::wstring::npos ||
            folded.find(L"aarch64") != std::wstring::npos) {
            score += 1;
        }
#elif defined(__x86_64__) || defined(_M_X64)
        if (folded.find(L"x64") != std::wstring::npos ||
            folded.find(L"x86_64") != std::wstring::npos) {
            score += 1;
        }
#endif
        if (score > best) {
            best = score;
            release.asset_url = url;
            release.asset_name = name;
        }
    }
    return release;
}

UpdateCheck check_for_update() {
    UpdateCheck result;
    if (development_build()) {
        result.error = L"this build has no released version";
        return result;
    }
    auto curl = curl_tool();
    if (curl.empty()) {
        result.error = L"curl was not found, and the launcher uses it to reach GitHub";
        return result;
    }
    // The answer is small; the download itself runs in the launcher's job instead, so
    // its output and Cancel work like any other step.
    std::string body;
    int status = -1;
    bool ok = capture_output(curl,
                             {L"-fsSL", L"--max-time", L"12", L"--connect-timeout", L"6",
                              L"-H", L"Accept: application/vnd.github+json", L"-A",
                              L"NarutoLauncher/" + launcher_version(), latest_release_url()},
                             &body, &status) &&
              status == 0;
    if (!ok) {
        result.error = L"GitHub did not answer; there may be no network connection";
        return result;
    }
    result.release = parse_release(widen(body), platform_asset_tag());
    if (result.release.version.empty()) {
        result.error = L"the release could not be read from GitHub's answer";
        return result;
    }
    result.state = is_newer_version(result.release.version, launcher_version())
                       ? UpdateCheck::State::Available
                       : UpdateCheck::State::UpToDate;
    return result;
}

fs::path install_target() {
#if defined(__APPLE__)
    // .../Naruto Launcher.app/Contents/MacOS/NarutoLauncher gives the bundle two above.
    fs::path executable(exe_path());
    fs::path macos = executable.parent_path();
    fs::path contents = macos.parent_path();
    if (to_lower(macos.filename().wstring()) != L"macos" ||
        to_lower(contents.filename().wstring()) != L"contents") {
        return {};
    }
    auto bundle = contents.parent_path();
    if (!ends_with_ci(bundle.filename().wstring(), L".app")) return {};
    return bundle;
#elif defined(_WIN32)
    return fs::path(exe_dir());
#else
    return fs::path(exe_path());
#endif
}

bool install_writable() {
    auto target = install_target();
    if (target.empty()) return false;
#if defined(_WIN32)
    // The new files are copied over the install folder itself.
    auto folder = target;
#else
    // The old copy is renamed before the new one takes its place, so the folder that
    // holds it has to allow both.
    auto folder = target.parent_path();
#endif
    std::error_code ec;
    auto probe = folder / L".naruto-launcher-write-test";
    if (!write_text_file(probe, "probe")) return false;
    fs::remove(probe, ec);
    return true;
}

fs::path update_dir() { return temp_dir() / L"NarutoLauncher-update"; }

void clean_up_after_update() {
    std::error_code ec;
    fs::remove_all(update_dir(), ec);
}

namespace {

fs::path staging_dir(const ReleaseInfo& release) {
    return update_dir() / (release.version.empty() ? std::wstring(L"latest") : release.version);
}

}  // namespace

bool write_swap_script(const fs::path& script, std::wstring* error) {
    std::error_code ec;
    fs::create_directories(script.parent_path(), ec);
#if defined(_WIN32)
    std::wstring text =
        L"# Written by the Naruto: Clash of Ninja launcher. It waits for the launcher to\n"
        L"# exit, copies the downloaded version over the install and starts it again.\n"
        L"param(\n"
        L"    [int]$LauncherPid,\n"
        L"    [string]$Target,\n"
        L"    [string]$Staged,\n"
        L"    [string]$Work,\n"
        L"    [string]$Relaunch\n"
        L")\n"
        L"while (Get-Process -Id $LauncherPid -ErrorAction SilentlyContinue) {\n"
        L"    Start-Sleep -Milliseconds 200\n"
        L"}\n"
        L"for ($attempt = 0; $attempt -lt 60; $attempt++) {\n"
        L"    try {\n"
        L"        Get-ChildItem -LiteralPath $Staged -Force | ForEach-Object {\n"
        L"            Copy-Item -LiteralPath $_.FullName -Destination $Target -Recurse -Force "
        L"-ErrorAction Stop\n"
        L"        }\n"
        L"        break\n"
        L"    } catch {\n"
        L"        Start-Sleep -Milliseconds 500\n"
        L"    }\n"
        L"}\n"
        L"Start-Process -FilePath $Relaunch -WorkingDirectory $Target\n"
        L"Remove-Item -LiteralPath $Work -Recurse -Force -ErrorAction SilentlyContinue\n"
        L"Remove-Item -LiteralPath $MyInvocation.MyCommand.Path -Force "
        L"-ErrorAction SilentlyContinue\n";
#else
    std::wstring text =
        L"#!/bin/sh\n"
        L"# Written by the Naruto: Clash of Ninja launcher. It waits for the launcher to\n"
        L"# exit, puts the downloaded version in its place and starts it.\n"
        L"pid=\"$1\"\n"
        L"target=\"$2\"\n"
        L"staged=\"$3\"\n"
        L"work=\"$4\"\n"
        L"relaunch=\"$5\"\n"
        L"while kill -0 \"$pid\" 2>/dev/null; do sleep 0.2; done\n";
#if defined(__APPLE__)
    text +=
        L"xattr -dr com.apple.quarantine \"$staged\" 2>/dev/null\n"
        L"backup=\"$target.old\"\n"
        L"rm -rf \"$backup\"\n"
        L"if ! mv \"$target\" \"$backup\"; then\n"
        L"    rm -rf \"$work\"\n"
        L"    exit 1\n"
        L"fi\n"
        L"if ! ditto \"$staged\" \"$target\"; then\n"
        L"    rm -rf \"$target\"\n"
        L"    mv \"$backup\" \"$target\"\n"
        L"    \"$relaunch\" \"$target\"\n"
        L"    rm -rf \"$work\"\n"
        L"    exit 1\n"
        L"fi\n"
        L"rm -rf \"$backup\"\n"
        L"\"$relaunch\" \"$target\"\n";
#else
    text +=
        L"if ! cp -f \"$staged\" \"$target\"; then\n"
        L"    rm -rf \"$work\"\n"
        L"    exit 1\n"
        L"fi\n"
        L"chmod +x \"$target\"\n"
        L"\"$relaunch\" >/dev/null 2>&1 &\n";
#endif
    text +=
        L"rm -rf \"$work\"\n"
        L"rm -f \"$0\"\n";
#endif
    if (!write_text_file(script, narrow(text))) {
        if (error) *error = L"cannot write " + script.wstring();
        return false;
    }
    fs::permissions(script,
                    fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exec |
                        fs::perms::group_read | fs::perms::group_exec |
                        fs::perms::others_read | fs::perms::others_exec,
                    fs::perm_options::replace, ec);
    return true;
}

bool start_swap_script(const fs::path& script, const fs::path& target, const fs::path& staged,
                       const fs::path& work, const fs::path& relaunch, std::wstring* error) {
#if defined(_WIN32)
    auto powershell = find_tool_from(
        {L"C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe"},
        L"powershell.exe");
    if (powershell.empty()) {
        if (error) *error = L"powershell was not found";
        return false;
    }
    std::vector<std::wstring> args{L"-NoProfile", L"-ExecutionPolicy", L"Bypass",
                                   L"-WindowStyle", L"Hidden", L"-File", script.wstring(),
                                   L"-LauncherPid", std::to_wstring(process_id()), L"-Target",
                                   target.wstring(), L"-Staged", staged.wstring(), L"-Work",
                                   work.wstring(), L"-Relaunch", relaunch.wstring()};
    return start_detached(powershell, args, error);
#else
    return start_detached(L"/bin/sh",
                          {script.wstring(), std::to_wstring(process_id()), target.wstring(),
                           staged.wstring(), work.wstring(), relaunch.wstring()},
                          error);
#endif
}

std::vector<Task> update_tasks(ReleaseInfo release) {
    auto work = staging_dir(release);
    auto zip = work / (release.asset_name.empty() ? std::wstring(L"update.zip")
                                                  : release.asset_name);
    auto unpacked = work / L"unpacked";
    std::vector<Task> tasks;

    tasks.push_back(
        {format(L"Downloading version %ls…", release.version.c_str()),
         [release, work, zip](Job& job) {
             std::error_code ec;
             fs::remove_all(work, ec);
             fs::create_directories(work, ec);
             if (release.asset_url.empty()) {
                 job.log(L"[update] this release has no download for this platform\n");
                 return 1;
             }
             auto curl = curl_tool();
             if (curl.empty()) {
                 job.log(L"[update] curl was not found, so the download cannot start\n");
                 return 127;
             }
             job.log(L"[update] " + release.asset_url + L"\n");
             // -sS: no progress meter in Activity, but curl's own errors still reach
             // the log; the step label and the progress bar carry the waiting.
             return job.run_process(curl,
                                    {L"-fsSL", L"--retry", L"2", L"--connect-timeout", L"15",
                                     L"-o", zip.wstring(), release.asset_url},
                                    fs::path());
         }});

    tasks.push_back({L"Unpacking the new version…",
                     [zip, unpacked](Job& job) {
                         std::error_code ec;
                         fs::remove_all(unpacked, ec);
                         fs::create_directories(unpacked, ec);
                         std::wstring error;
                         if (!unpack_archive(zip, unpacked, job, &error)) {
                             job.log(L"[update] " + error + L"\n");
                             return 1;
                         }
                         return 0;
                     }});

    tasks.push_back({L"Restarting the launcher…",
                     [work, unpacked](Job& job) {
                         auto target = install_target();
                         if (target.empty()) {
                             job.log(L"[update] this copy of the launcher cannot be replaced "
                                     L"in place\n");
                             return 1;
                         }
                         auto payload = find_payload(unpacked);
                         if (payload.empty()) {
                             job.log(L"[update] the download holds no launcher\n");
                             return 1;
                         }
#if !defined(_WIN32) && !defined(__APPLE__)
                         payload /= fs::path(exe_path()).filename();
#endif
#if defined(_WIN32)
                         auto script = update_dir() / L"apply-update.ps1";
#else
                         auto script = update_dir() / L"apply-update.sh";
#endif
                         std::wstring error;
#if defined(__APPLE__)
                         fs::path relaunch(L"/usr/bin/open");
#elif defined(_WIN32)
                         fs::path relaunch = target / L"NarutoLauncher.exe";
#else
                         fs::path relaunch = target;
#endif
                         if (!write_swap_script(script, &error) ||
                             !start_swap_script(script, target, payload, work, relaunch,
                                                &error)) {
                             job.log(L"[update] " + error + L"\n");
                             return 1;
                         }
                         job.log(format(L"[update] version %ls replaces %ls\n",
                                        launcher_version().c_str(),
                                        target.wstring().c_str()));
                         return 0;
                     }});
    return tasks;
}

}  // namespace nla
