#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace nla {

namespace fs = std::filesystem;

struct ExtractRequest {
    fs::path image;
    fs::path outdir;
    std::wstring disc_id;
    std::wstring dol_sha1;
};

struct ExtractOutcome {
    bool ok = false;
    std::wstring error;
    int file_count = 0;
    uint64_t bytes = 0;
    std::wstring dol_sha1;
};

using ExtractProgress = std::function<void(int percent, const std::wstring& stage)>;

// True for an image that nodtool has to convert first (RVZ, WIA, GCZ, ...).
bool needs_nodtool(const fs::path& image);

// Writes the disc's boot area, main.dol, the file system and every file it names
// into `outdir`, plus the disc.manifest the runtime reads. Replaces an existing tree,
// waiting briefly for a file another program still has open, since Windows refuses to
// move the folder while one is.
ExtractOutcome extract_disc(const ExtractRequest& request, const ExtractProgress& progress,
                            const std::atomic_bool& cancel);

}  // namespace nla
