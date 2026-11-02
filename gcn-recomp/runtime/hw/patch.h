// A patch view: the disc the game reads is the target of an xdelta (VCDIFF) delta, decoded
// window by window as the game asks for it, instead of a second copy of the disc on disk.
//
// The launcher leaves a `patch.manifest` beside `disc.manifest` naming the delta and how the
// disc the delta was made against must be normalised for it to fit. That disc is the game's
// own extracted tree, so a patch needs no disc image and no second copy: nothing is
// written, and switching a patch off deletes the manifest.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// Supplies bytes of a file: the delta, or the disc the delta was made from. Returns false
// when the read is not in that file.
using PatchRead = bool (*)(void* user, uint64_t offset, void* dst, uint32_t len);

// The two files the decoder reads: the delta itself, and the source disc it rebuilds the
// target from.
struct PatchFiles {
    PatchRead delta = nullptr;
    void* delta_user = nullptr;
    PatchRead source = nullptr;
    void* source_user = nullptr;
};

struct PatchManifest {
    std::string delta;  // absolute path to the .xdelta
    // Regions of the source that read as zero, and short runs of constant bytes: the
    // normalisation a "bad dump" needs (unused space zeroed, a few header bytes set).
    std::vector<std::pair<uint64_t, uint64_t>> zero;
    std::vector<std::pair<uint64_t, std::string>> bytes;
};

// Reads `patch.manifest`. A relative path beside it is resolved against its folder.
bool patch_manifest_read(const char* manifest_path, PatchManifest* out, std::string* error);

// Indexes the delta's windows. `files` supplies both the delta and the disc it was made
// against, the tree the game itself reads from.
bool patch_open(const PatchManifest& manifest, const PatchFiles& files, std::string* error);
bool patch_active();
uint64_t patch_target_size();

// Reads the patched disc. False when the read falls outside the delta's target.
bool patch_read(uint64_t offset, void* dst, uint32_t len);

// What checking the delta against its source found. Each window carries the checksum of the
// piece of target it rebuilds, so a matching window is proof the source is the disc the
// patch was made from. A window can decode and still not match: the two discs differ
// somewhere the window copies through, which for a patch made against a "clean" dump is
// space no file owns. Those bytes are still served, from whatever this disc has there.
struct PatchCheck {
    int windows = 0;               // windows the delta holds
    int decoded = 0;               // windows that decoded, matching or not
    int matched = 0;               // windows whose checksum matched
    uint64_t first_unmatched = 0;  // target offset of the first that did not
};
// Checks every window. `sample` limits it to that many spread evenly over the disc, which
// is what a launcher checking a patch before offering it does: reading every window means
// reading the whole disc, a minute on a spinning disk.
PatchCheck patch_verify(int sample = 0);
