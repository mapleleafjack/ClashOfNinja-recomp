#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "catalog.h"

namespace nla {

namespace fs = std::filesystem;

// One mod: a folder under mods/ with a mod.txt naming the builds it is for and a disc/
// tree of the files it replaces. The tree mirrors an extracted disc ("sys/main.dol",
// "files/audio/bgm/m000.trk"), and the launcher turns it into the extents the runtime
// serves in their place, so nothing is rebuilt and the extracted disc is left alone.
struct ModInfo {
    std::wstring folder;  // the folder's name under mods/, which identifies the mod
    fs::path dir;
    std::wstring name;
    std::wstring version;
    std::wstring author;
    std::wstring description;
    // The disc IDs the mod may be enabled for, e.g. "G4NJDA". A build's address space is
    // its own, so a mod never runs on a disc it did not name.
    std::vector<std::wstring> disc_ids;
    // The disc-tree paths the mod replaces, relative to its disc/ folder, '/'-separated.
    std::vector<std::wstring> files;
    // The mod's Gecko codes, relative to the mod folder: the files `gecko = ...` names,
    // or a `gecko.txt` beside mod.txt when it names none.
    std::vector<std::wstring> gecko_files;
};

// `key = value` lines, as the launcher's other files use. `#` starts a comment; `name`,
// `version`, `author`, `description` and one or more `disc_id` lines are read.
bool parse_mod_manifest(const std::wstring& text, ModInfo* info);

// Every mods/<folder> holding a mod.txt, by folder name, whether or not this game uses it.
std::vector<ModInfo> discover_mods(const fs::path& mods_dir);

bool mod_supports(const ModInfo& mod, const std::wstring& disc_id);

// One row of the launcher's Mods table: a mod folder, or a disc patch, that a build can
// play. A row may be known from `mods.json` before its files are on this machine, which is
// what lets the panel list it and download it when it is checked.
struct ModChoice {
    std::wstring id;
    std::wstring name;
    std::wstring version;
    std::wstring author;
    std::wstring description;
    std::vector<std::wstring> disc_ids;
    bool patch = false;      // a disc patch rather than a folder of replaced files
    bool installed = false;  // its files are here, so applying it needs no download
    bool known = false;      // `mods.json` has an entry, so it can be downloaded
    std::size_t catalog = 0; // index into the catalog this row came from, when known
    ModInfo mod;             // the files, when a mod is here
    fs::path delta;          // the delta, when a patch is here
};

// The table for a disc: the catalog's entries for it, the folders in `mods_dir` whose
// mod.txt names it, and the deltas in `patches_dir`, merged by id. A folder's own manifest
// is the truth about what it is; the catalog only says where a row comes from while its
// files are missing. A delta the catalog names belongs to that entry, so it is offered for
// the discs the entry names and no others.
std::vector<ModChoice> collect_mods(const std::vector<CatalogMod>& catalog,
                                    const fs::path& mods_dir, const fs::path& patches_dir,
                                    const std::wstring& disc_id);

// An id no row has yet, for a delta added from this machine. The wanted name is used as
// it is when it is free, else with "-2", "-3" and so on.
std::wstring unique_mod_id(const std::vector<ModChoice>& entries, const std::wstring& wanted);

// One entry of an extracted disc's disc.manifest.
struct DiscExtent {
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
    std::uint64_t file_offset = 0;
    std::wstring path;
};

std::vector<DiscExtent> read_disc_extents(const fs::path& manifest);

// The disc the extracted tree came from, from its manifest's `disc_id` line: the check
// that keeps a mod written for one build off another build's addresses.
std::wstring read_disc_id(const fs::path& manifest);

// One mod file served in place of a disc extent. `available` is how much of the file
// there is; a shorter replacement leaves the rest of the extent as zeroes, the same way
// the padding between files reads.
struct OverlayExtent {
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
    std::uint64_t available = 0;
    std::wstring path;  // relative to the manifest's folder, '/'-separated
};

// The mods.manifest written beside disc.manifest, which the runtime reads at boot.
std::string overlay_manifest_text(const std::vector<OverlayExtent>& extents);

struct OverlayBuild {
    std::vector<OverlayExtent> extents;
    // What was skipped and why, for the Activity log: a file this disc does not have, or
    // one too large to fit the extent it would replace.
    std::vector<std::wstring> warnings;
};

// Lays the mods' files over the disc's extents, in the order given: a later mod wins
// where two replace the same file. Paths in the result are relative to `manifest_dir`,
// which is the disc folder the manifest is written into.
OverlayBuild build_overlays(const std::vector<DiscExtent>& disc,
                            const std::vector<ModInfo>& mods, const fs::path& manifest_dir);

// Writes `mods.manifest` beside `disc_manifest`, which is what the runtime reads at boot,
// or removes it when nothing is laid over the disc. Only touches the file when the list
// changed, so a launcher that refreshes often is not writing all the time.
struct OverlaySync {
    bool wrote = false;  // the file was written, or removed
    std::size_t files = 0;
    std::vector<std::wstring> warnings;
};
OverlaySync sync_overlay_manifest(const fs::path& disc_dir, const fs::path& disc_manifest,
                                  const std::vector<ModInfo>& mods);

// What the mods' Gecko codes became. `ops` is the program the runtime runs once a frame;
// `words` are the instruction words handed to recompilation, because a write into code
// changes nothing once the code has been recompiled (see gecko.h).
struct ModProgram {
    bool wrote = false;   // mods.gecko or mods.code was written, or removed
    std::size_t codes = 0;  // mods that carry codes
    std::size_t ops = 0;    // operations in mods.gecko
    std::size_t words = 0;  // instruction words in mods.code
    std::vector<std::wstring> warnings;
};

// Reads every mod's codes, in the order given, and writes `mods.gecko` and `mods.code`
// beside `disc.manifest`. Both are removed when no mod carries codes. Only touches a file
// when its text changed, so a launcher that refreshes often does not write constantly.
ModProgram sync_mod_programs(const fs::path& disc_dir, const std::vector<ModInfo>& mods);

// A disc patch: an xdelta (.xdelta) delta that turns the extracted disc into another one --
// SCON4, a translation, a rebalancing hack. The runtime decodes it window by window over the
// tree the game already reads, so no second disc and no second extraction is needed. The
// launcher's job is to work out whether the delta really was made from this disc, and to
// leave `patch.manifest` beside `disc.manifest` saying how to read it.
struct PatchFit {
    bool opened = false;   // the delta could be read at all
    int windows = 0;       // windows it holds
    int sampled = 0;       // windows decoded for the check
    int matched = 0;       // of those, how many checksums agreed
    std::wstring error;    // why it could not be used, when it could not
    // The `patch zero <start> <size>` lines that fitted this disc best: the space no file
    // owns, which a dump of the disc may hold either as zeros or as whatever was on it.
    std::vector<std::pair<std::uint64_t, std::uint64_t>> zero;
};

// Opens `delta` and decodes a sample of its windows against the disc tree in `disc_dir`.
// The zeros that make it fit are chosen here, so nothing about a particular patch is
// hard-coded: the delta itself says whether the disc is the one it was made from.
PatchFit check_patch(const fs::path& delta, const fs::path& disc_dir);

// Writes `patch.manifest` beside `disc.manifest` from a fit that was accepted, or removes it
// when `delta` is empty. The text is only replaced when it changed.
bool write_patch_manifest(const fs::path& disc_dir, const fs::path& delta, const PatchFit& fit);

// The fit `patch.manifest` already records for `delta`, when it names that same file: a
// delta that fitted this disc is not decoded again for every Apply, and the runtime does not
// re-verify either — it reads the zeros the file says.
std::optional<PatchFit> read_patch_fit(const fs::path& disc_dir, const fs::path& delta);

// The delta a tree's `patch.manifest` names, or "" when it carries none.
fs::path read_patch_delta(const fs::path& disc_dir);

}  // namespace nla
