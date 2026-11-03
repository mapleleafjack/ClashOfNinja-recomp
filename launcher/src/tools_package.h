#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "jobs.h"

namespace nla {

// The tool bundle the launcher can fetch for itself.
//
// A release is small: the launcher, the notices, and nothing else. The toolchain a build
// needs (CMake, Ninja, MinGW-w64, Python, SDL2, nodtool) is a separate archive beside the
// launcher's own on the release page, so only a player who has to build the game downloads
// it. The URL and the digest are pinned when that archive is written — see
// launcher/packaging/build-release.ps1 -Split — so a build knows exactly what it is
// fetching and refuses anything else.
bool tools_package_available();

// The pinned size in the form the button shows, e.g. "187 MB"; empty when unknown.
std::wstring tools_package_size();

// Which archive the tools\ folder came from, kept in tools\.nla-tools: the digest of the
// archive it was unpacked from, or "bundled" for the copies that shipped beside the launcher.
//
// A launcher replaces tools\ it did not fetch from the archive it pins. An update replaces the
// launcher and gcn-recomp and never tools\, so without this a folder fetched from an archive
// that turned out to be broken would be built with for the life of the install - which is what
// v1.7.0 shipped, with a Chocolatey shim in place of ninja.exe.
constexpr const wchar_t* kToolsMarkerName = L".nla-tools";
constexpr const wchar_t* kToolsMarkerBundled = L"bundled";
bool tools_marker_current(std::wstring_view marker, std::wstring_view pinned);

// The marker a tools folder holds: its text, empty when the folder has none.
std::wstring read_tools_marker(const fs::path& tools_dir);
// The same, for the tools\ beside the launcher.
std::wstring tools_marker();
// Whether a tools folder is shaped like one these archives unpack - cmake/, ninja/, zig/ and
// the rest - rather than a folder holding something else.
bool tools_holds_toolchain(const fs::path& tools_dir);
// Whether the tools beside the launcher came from an archive other than the pinned one.
bool tools_package_stale();

// Download, check and unpack that archive into tools\ beside the launcher.
std::vector<Task> tools_package_tasks();

// Whether the archive is the one the release pinned: its SHA-256 against `expected`, and a
// mismatch deletes it so a second attempt cannot reuse a bad download. The digest is taken
// here rather than by the system's sha256sum, so the answer is the same on every machine.
bool check_archive(const fs::path& archive, const std::wstring& expected,
                   std::wstring* error);

// The same steps without a window, for `--fetch-tools <log>`: everything the tasks report
// goes to that file. Returns the job's exit code. Used by the release check, which has to
// prove the pinned archive still downloads and still matches its digest.
int fetch_tools_to_log(const fs::path& log);

}  // namespace nla
