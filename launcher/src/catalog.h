#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace nla {

namespace fs = std::filesystem;

// One mod the launcher knows from `mods.json` beside it, whether or not this machine has
// its files yet. The catalog is what lets the Mods panel list a mod before it is
// downloaded, and what says where a checked one comes from.
struct CatalogMod {
    std::wstring id;  // the folder under mods/, or the patch's name there
    std::wstring name;
    std::wstring version;
    std::wstring author;
    std::wstring description;
    std::vector<std::wstring> disc_ids;
    bool patch = false;  // a disc patch (an xdelta), not a folder of replaced files
    // Where the files come from when they are not here yet. A GitHub release is asked for
    // its newest asset and `file` is looked for inside it; `url` names an archive directly.
    std::wstring owner;
    std::wstring repo;
    std::wstring asset;
    std::wstring file;    // the delta inside the asset, for a patch
    std::wstring url;     // a direct archive, used when no release is named
    std::wstring folder;  // the folder inside the archive, for a mod

    bool downloadable() const { return !url.empty() || (!owner.empty() && !repo.empty()); }
};

// The `mods.json` beside the launcher, a macOS bundle's Resources, or the checkout a
// development build lives in; empty when there is none.
fs::path mod_catalog_path();

// Reads the catalog. A file that cannot be read is no catalog rather than an error the
// player has to answer: `error` says why, for the Activity log.
std::vector<CatalogMod> load_mod_catalog(const fs::path& path, std::wstring* error = nullptr);
std::vector<CatalogMod> parse_mod_catalog(const std::wstring& text, std::wstring* error = nullptr);

// Whether a build's addresses are one this mod was written for.
bool catalog_supports(const CatalogMod& mod, const std::wstring& disc_id);

}  // namespace nla
