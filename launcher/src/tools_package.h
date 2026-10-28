#pragma once

#include <string>
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

// Download, check and unpack that archive into tools\ beside the launcher.
std::vector<Task> tools_package_tasks();

// The same steps without a window, for `--fetch-tools <log>`: everything the tasks report
// goes to that file. Returns the job's exit code. Used by the release check, which has to
// prove the pinned archive still downloads and still matches its digest.
int fetch_tools_to_log(const fs::path& log);

}  // namespace nla
