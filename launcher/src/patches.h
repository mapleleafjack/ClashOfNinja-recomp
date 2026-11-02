#pragma once

#include <functional>
#include <string>
#include <vector>

#include "util.h"

namespace nla {

// A patch the launcher can fetch for the player: a GitHub release whose asset holds an
// xdelta of a whole disc, which the launcher then leaves beside the games. The fields
// come from an entry in `mods.json`; the launcher does not search GitHub for these,
// because a patch has to name the game it is for and the file inside its release.
struct PatchSource {
    std::wstring game;   // the game folder it is for, e.g. "gnt4"
    std::wstring name;   // what the release holds, e.g. "SCON4"
    std::wstring owner;  // the repository the releases are published in
    std::wstring repo;
    std::wstring asset;  // the asset to download, a zip
    std::wstring delta;  // the delta inside that asset
};

struct PatchRelease {
    std::wstring version;  // the release's tag, without a leading v
    std::wstring asset_url;
    std::wstring asset_name;
    std::wstring page_url;
    std::wstring error;    // why there is no release, when there is not
};

// The newest release of `source` that still carries its asset, asked of GitHub's API.
PatchRelease find_patch_release(const PatchSource& source);

// Where a fetched delta is kept: beside the games, so it outlives the tree it patches and
// the player can keep using a patch after extracting the disc again.
fs::path patch_directory(const fs::path& project_root);

}  // namespace nla
