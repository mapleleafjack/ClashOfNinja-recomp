// DVD Interface: command execution against the disc, incl. audio stream commands.
#include "../runtime.h"
#include "../platform.h"

static uint32_t g_disr, g_dicvr, g_cmd[3], g_dimar, g_dilen, g_dicr, g_diimm, g_dicfg;
static bool g_busy;

#include "dtk.h"
#include "patch.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// The disc is either an image -- a plain .iso, or a CISO: a 0x8000-byte header, "CISO", a
// little-endian block size, then one byte per block saying whether that block is stored,
// followed by the stored blocks in order, a block that is not stored reading as zero
// (Dolphin writes CISO for the trailing zero space of a disc, which can be most of an
// image) -- or a directory that tools/extract_disc.py made from one: the disc's files in
// full, with a disc.manifest naming the disc offset each came from. The image itself is
// then not needed, so the user can delete it once the tree exists.
struct DiscExtent {
    uint64_t start, end;   // disc offsets this file covers
    uint64_t file_off;     // where those bytes begin in the file
    uint64_t available;    // bytes the file holds; the rest of the extent reads as zero
    PlatFile* f;
};
// One disc, read either from such a tree or from an image. A patch decodes against the same
// tree the game reads from, so one of these serves both.
struct Disc {
    std::vector<DiscExtent> extents;  // a tree's files, sorted, no overlaps
    uint64_t size = 0;                // the disc the tree was made from
    PlatFile* image = nullptr;        // an image: a plain .iso, or a CISO
    uint32_t ciso_block = 0;
    std::vector<int32_t> ciso_map;    // CISO: disc block -> stored block, -1 if absent

    bool read(uint64_t offset, void* dst, uint32_t len) const;
};
static Disc g_disc;  // what the game reads, and what a patch decodes against
// A mod's files, served in place of the disc's at the same offsets (mods.manifest, which
// the launcher writes beside disc.manifest). The tree itself is never written to, so a mod
// is switched on and off with a restart, not an extraction.
static std::vector<DiscExtent> g_overlays;  // sorted by start, no overlaps

// The extent covering `offset`, if any: the last one starting at or before it.
static const DiscExtent* find_extent(const std::vector<DiscExtent>& list, uint64_t offset,
                                     size_t* index) {
    size_t lo = 0, hi = list.size();
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (list[mid].start <= offset) lo = mid + 1;
        else hi = mid;
    }
    if (index) *index = lo;
    return lo && offset < list[lo - 1].end ? &list[lo - 1] : nullptr;
}

// Reads the disc, from the tree's files or from the image. Between a tree's files the disc
// reads as zero; past its end nothing does.
bool Disc::read(uint64_t offset, void* dst, uint32_t len) const {
    uint8_t* p = (uint8_t*)dst;
    if (!extents.empty()) {
        uint32_t done = 0;
        while (done < len) {
            size_t index = 0;
            // The last extent starting at or before the offset; if the offset is past its
            // end it is in the padding between files, which reads as zero.
            const DiscExtent* e = find_extent(extents, offset, &index);
            uint32_t n;
            if (e) {
                n = (uint32_t)std::min<uint64_t>(len - done, e->end - offset);
                if (!plat_read_at(e->f, e->file_off + (offset - e->start), p + done, n)) return false;
            } else {
                if (offset >= size) return false;
                const uint64_t next = index < extents.size() ? extents[index].start : size;
                n = (uint32_t)std::min<uint64_t>(len - done, next - offset);
                memset(p + done, 0, n);
            }
            offset += n;
            done += n;
        }
        return true;
    }
    if (!image) return false;
    if (!ciso_block) return plat_read_at(image, offset, p, len);
    while (len) {
        const uint64_t blk = offset / ciso_block;
        const uint32_t off = (uint32_t)(offset % ciso_block);
        const uint32_t n = (uint32_t)std::min<uint64_t>(len, ciso_block - off);
        const int32_t stored = blk < ciso_map.size() ? ciso_map[blk] : -1;
        if (stored < 0) memset(p, 0, n);
        else if (!plat_read_at(image, 0x8000 + (uint64_t)stored * ciso_block + off, p, n)) return false;
        p += n;
        offset += n;
        len -= n;
    }
    return true;
}

// Reads the manifest and opens every file it names, read-only. Its lines are
// `extent <disc offset> <size> <offset in file> <path>`, all hex but the path, which may
// hold spaces and so is the rest of the line.
// A manifest may have been written on Windows and carried carriage returns: the path runs
// to the end of the line, which is not part of it.
static void trim_line_end(char* text) {
    size_t length = strlen(text);
    while (length && (text[length - 1] == '\r' || text[length - 1] == '\n' ||
                      text[length - 1] == ' ' || text[length - 1] == '\t'))
        text[--length] = 0;
}

// Opens the tree `dir` as a disc: its disc.manifest names the file each extent came from.
static bool di_open_tree(const char* dir, Disc* disc, std::string* error) {
    std::string manifest = std::string(dir) + "/disc.manifest";
    FILE* m = fopen(manifest.c_str(), "rb");
    if (!m) {
        *error = "cannot read " + manifest;
        return false;
    }
    char line[4096];
    while (fgets(line, sizeof(line), m)) {
        char path[3600];
        unsigned long long start, size, file_off;
        if (sscanf(line, "extent %llx %llx %llx %3599[^\n]", &start, &size, &file_off, path) != 4)
            continue;
        trim_line_end(path);
        std::string full = std::string(dir) + "/" + path;
        PlatFile* f = plat_open_read(full.c_str());
        if (!f) {
            fclose(m);
            *error = "cannot open " + full + " (" + manifest + ")";
            return false;
        }
        disc->extents.push_back(DiscExtent{start, start + size, file_off, size, f});
    }
    unsigned long long image_size = 0;
    rewind(m);
    while (fgets(line, sizeof(line), m))
        if (sscanf(line, "image_size %llx", &image_size) == 1) break;
    disc->size = image_size;
    fclose(m);

    if (disc->extents.empty()) {
        *error = manifest + " lists no files";
        return false;
    }
    std::sort(disc->extents.begin(), disc->extents.end(),
              [](const DiscExtent& a, const DiscExtent& b) { return a.start < b.start; });
    for (size_t i = 1; i < disc->extents.size(); i++)
        if (disc->extents[i].start < disc->extents[i - 1].end) {
            *error = manifest + " has overlapping files";
            return false;
        }
    if (!disc->size) disc->size = disc->extents.back().end;
    LOG(LOG_DVD, "disc tree: %llu files, image %llu bytes", (unsigned long long)disc->extents.size(),
        (unsigned long long)disc->size);
    return true;
}

// Opens an image as a disc: a plain .iso, or the CISO form of one.
static bool di_open_image(const char* path, Disc* disc, std::string* error) {
    disc->image = plat_open_read(path);
    if (!disc->image) {
        *error = std::string("cannot open the disc image ") + path;
        return false;
    }
    uint8_t hdr[0x8000];
    if (!plat_read_at(disc->image, 0, hdr, sizeof(hdr)) || memcmp(hdr, "CISO", 4) != 0) return true;
    disc->ciso_block = (uint32_t)hdr[4] | ((uint32_t)hdr[5] << 8) | ((uint32_t)hdr[6] << 16) |
                       ((uint32_t)hdr[7] << 24);
    if (!disc->ciso_block) {
        *error = std::string("CISO image ") + path + " has a zero block size";
        return false;
    }
    disc->ciso_map.assign(sizeof(hdr) - 8, -1);
    int32_t stored = 0;
    for (size_t i = 0; i < disc->ciso_map.size(); i++)
        if (hdr[8 + i] == 1) disc->ciso_map[i] = stored++;
    LOG(LOG_DVD, "CISO image: %u-byte blocks, %d stored", disc->ciso_block, stored);
    return true;
}

static bool di_open_disc(const char* path, Disc* disc, std::string* error) {
    return plat_is_dir(path) ? di_open_tree(path, disc, error) : di_open_image(path, disc, error);
}

// A mod's files, which the launcher lays over the disc's by offset: `overlay <disc offset>
// <size> <available> <path>`. A replacement shorter than the disc's file leaves the rest of
// the extent as zeroes, so it needs no padding of its own.
static void di_open_overlays(const char* dir) {
    std::string manifest = std::string(dir) + "/mods.manifest";
    FILE* o = fopen(manifest.c_str(), "rb");
    if (!o) return;
    char line[4096];
    while (fgets(line, sizeof(line), o)) {
        char path[3600];
        unsigned long long start, size, available;
        if (sscanf(line, "overlay %llx %llx %llx %3599[^\n]", &start, &size, &available, path) != 4)
            continue;
        trim_line_end(path);
        std::string full = std::string(dir) + "/" + path;
        PlatFile* f = plat_open_read(full.c_str());
        if (!f) {
            // The launcher wrote the list, so the file was removed since: say so and run
            // without it rather than refuse to start the game.
            LOG(LOG_DVD, "mod: cannot open %s from %s; skipped", path, manifest.c_str());
            continue;
        }
        g_overlays.push_back(DiscExtent{start, start + size, 0, std::min(available, size), f});
    }
    fclose(o);
    std::sort(g_overlays.begin(), g_overlays.end(),
              [](const DiscExtent& a, const DiscExtent& b) { return a.start < b.start; });
    for (size_t i = 1; i < g_overlays.size(); i++)
        if (g_overlays[i].start < g_overlays[i - 1].end)
            fatal("%s has overlapping mod files", manifest.c_str());
    LOG(LOG_DVD, "mods: %llu files served in place of the disc's",
        (unsigned long long)g_overlays.size());
}

static bool patch_source_read(void* user, uint64_t offset, void* dst, uint32_t len) {
    return ((const Disc*)user)->read(offset, dst, len);
}

static bool patch_delta_read(void* user, uint64_t offset, void* dst, uint32_t len) {
    return plat_read_at((PlatFile*)user, offset, dst, len);
}

// A patch: the disc the game reads becomes the target of an xdelta delta, decoded window by
// window as the game asks for it. Nothing is written: the delta is read against the same
// extracted tree the game plays from, which is the disc the patch was made from, and the
// patched disc exists only as the bytes the game asks for. The manifest adds the
// normalisation that source needs -- the unused space a dump zeroes and a few header
// bytes, see patch.h.
static void di_open_patch(const char* dir) {
    const std::string manifest_path = std::string(dir) + "/patch.manifest";
    FILE* probe = fopen(manifest_path.c_str(), "rb");
    if (!probe) return;  // no patch: the usual case
    fclose(probe);

    PatchManifest manifest;
    std::string error;
    if (!patch_manifest_read(manifest_path.c_str(), &manifest, &error)) fatal("%s", error.c_str());
    PlatFile* delta = plat_open_read(manifest.delta.c_str());
    if (!delta) fatal("cannot read the patch delta %s", manifest.delta.c_str());
    PatchFiles files;
    files.delta = patch_delta_read;
    files.delta_user = delta;
    files.source = patch_source_read;
    files.source_user = &g_disc;
    if (!patch_open(manifest, files, &error)) fatal("%s", error.c_str());
    LOG(LOG_DVD, "patch: %s over the extracted disc, target %llu bytes", manifest.delta.c_str(),
        (unsigned long long)patch_target_size());
}

// Where the disc's bytes come from, in priority order: a mod's file (mods.manifest), the
// patch's target, then the disc the tree holds.
bool iso_read(uint64_t offset, void* dst, uint32_t len) {
    uint8_t* p = (uint8_t*)dst;
    while (len) {
        if (const DiscExtent* overlay = find_extent(g_overlays, offset, nullptr)) {
            const uint32_t n = (uint32_t)std::min<uint64_t>(len, overlay->end - offset);
            const uint64_t within = offset - overlay->start;
            const uint32_t have =
                within < overlay->available
                    ? (uint32_t)std::min<uint64_t>(n, overlay->available - within)
                    : 0;
            if (have && !plat_read_at(overlay->f, overlay->file_off + within, p, have))
                return false;
            if (have < n) memset(p + have, 0, n - have);
            p += n;
            offset += n;
            len -= n;
            continue;
        }
        const uint64_t patched = patch_active() ? patch_target_size() : 0;
        if (offset < patched) {
            const uint32_t n = (uint32_t)std::min<uint64_t>(len, patched - offset);
            if (!patch_read(offset, p, n)) return false;
            p += n;
            offset += n;
            len -= n;
            continue;
        }
        return g_disc.read(offset, p, len);
    }
    return true;
}

void di_init(const char* iso_path) {
    std::string error;
    if (!di_open_disc(iso_path, &g_disc, &error)) fatal("%s", error.c_str());
    if (!plat_is_dir(iso_path)) return;
    di_open_overlays(iso_path);
    di_open_patch(iso_path);
}

// The disc the game's tree holds, before any patch: what says whether the game under a
// patch is the one this build was recompiled from. A patch may carry a disc of its own, and
// a hack may rename the ID it carries (SCON4 is SG4JDA over a G4NJDA disc), so the patched
// view cannot be the thing that is checked.
bool di_source_read(uint64_t offset, void* dst, uint32_t len) { return g_disc.read(offset, dst, len); }

bool di_patch_active() { return patch_active(); }

bool di_verify_patch(int* decoded, int* matched, int* windows, uint64_t* first_unmatched) {
    if (decoded) *decoded = 0;
    if (matched) *matched = 0;
    if (windows) *windows = 0;
    if (first_unmatched) *first_unmatched = 0;
    if (!patch_active()) return false;
    const PatchCheck check = patch_verify();
    if (decoded) *decoded = check.decoded;
    if (matched) *matched = check.matched;
    if (windows) *windows = check.windows;
    if (first_unmatched) *first_unmatched = check.first_unmatched;
    return true;
}

static void di_update_irq() {
    bool irq = ((g_disr & 0x10) && (g_disr & 0x08)) || ((g_disr & 0x04) && (g_disr & 0x02)) ||
               ((g_disr & 0x40) && (g_disr & 0x20)) || ((g_dicvr & 0x04) && (g_dicvr & 0x02));
    pi_set_interrupt(INT_DI, irq);
}

static void di_complete() {
    g_busy = false;
    g_dicr &= ~1u;
    g_disr |= 0x10;  // TCINT
    di_update_irq();
}

static void di_execute() {
    uint32_t cmd = g_cmd[0] >> 24;
    uint64_t delay = TB_FREQ / 10000;  // 100us base latency
    switch (cmd) {
    case 0xA8: {
        uint32_t sub = (g_cmd[0] >> 16) & 0xFF;
        uint64_t offset = sub == 0x40 ? 0 : (uint64_t)g_cmd[1] << 2;
        uint32_t len = g_dilen;
        LOG(LOG_DVD, "read off=%09llX len=%X -> %08X", (unsigned long long)offset, len, g_dimar);
        const uint32_t fit = dma_fit("DVD", g_dimar, len);
        debug_guest_check("before DVD read");
        if (fit && !iso_read(offset, phys_ptr(g_dimar), fit))
            LOG(LOG_DVD, "read past end of image");
        debug_guard_dma("DVD read", g_dimar, len);
        debug_guest_check("after DVD read");
        g_dimar += len;
        g_dilen = 0;
        delay += (uint64_t)len * TB_FREQ / (64 << 20);  // ~64MB/s
        break;
    }
    case 0x12: {  // inquiry
        static const uint8_t inq[0x20] = {0x00, 0x00, 0x00, 0x02, 0x20, 0x06, 0x05, 0x26, 0x41};
        memcpy(phys_ptr(g_dimar), inq, dma_fit("DVD inquiry", g_dimar, sizeof(inq)));
        g_dimar += 0x20;
        g_dilen = 0;
        break;
    }
    case 0xAB: break;  // seek
    case 0xE0: g_diimm = 0; break;  // request error: none
    case 0xE1: {  // audio stream
        std::lock_guard<std::mutex> lk(g_dtk_mutex);
        uint32_t sub = (g_cmd[0] >> 16) & 0xFF;
        if (sub == 0x00) {
            uint64_t off = (uint64_t)g_cmd[1] << 2;
            uint32_t len = g_cmd[2];
            LOG(LOG_DVD, "stream queue off=%llX len=%X", (unsigned long long)off, len);
            if (len == 0) g_dtk.stop_at_end = true;
            else if (!g_dtk.playing || g_dtk.len == 0) {
                g_dtk.start = g_dtk.cur = off; g_dtk.len = len; g_dtk.playing = true;
                memset(g_dtk.hist, 0, sizeof(g_dtk.hist));
                g_dtk.next_start = off; g_dtk.next_len = len; g_dtk.stop_at_end = false;
            } else {
                g_dtk.next_start = off; g_dtk.next_len = len; g_dtk.stop_at_end = false;
            }
        } else if (sub == 0x01) {
            LOG(LOG_DVD, "stream cancel");
            g_dtk.playing = false; g_dtk.len = 0;
        }
        break;
    }
    case 0xE2: {  // stream status
        std::lock_guard<std::mutex> lk(g_dtk_mutex);
        uint32_t sub = (g_cmd[0] >> 16) & 0xFF;
        switch (sub) {
        case 0x00: g_diimm = g_dtk.playing ? 1 : 0; break;
        case 0x01: g_diimm = (uint32_t)((g_dtk.cur & ~0x7FFFull) >> 2); break;
        case 0x02: g_diimm = (uint32_t)(g_dtk.start >> 2); break;
        case 0x03: g_diimm = (uint32_t)g_dtk.len; break;
        }
        break;
    }
    case 0xE3: break;  // stop motor
    case 0xE4: LOG(LOG_DVD, "audio buffer config %08X", g_cmd[0]); break;
    default:
        LOG(LOG_DVD, "unknown DI command %08X %08X %08X", g_cmd[0], g_cmd[1], g_cmd[2]);
        break;
    }
    g_busy = true;
    event_schedule_in(delay, di_complete);
}

uint32_t di_read32(uint32_t off) {
    switch (off) {
    case 0x00: return g_disr;
    case 0x04: return g_dicvr;
    case 0x08: return g_cmd[0];
    case 0x0C: return g_cmd[1];
    case 0x10: return g_cmd[2];
    case 0x14: return g_dimar;
    case 0x18: return g_dilen;
    case 0x1C: return g_dicr;
    case 0x20: return g_diimm;
    case 0x24: return g_dicfg;
    }
    return 0;
}

void di_write32(uint32_t off, uint32_t v) {
    switch (off) {
    case 0x00:
        g_disr = (g_disr & ~0x2Au) | (v & 0x2A);           // masks
        g_disr &= ~(v & 0x54);                             // write-1-to-clear ints
        if (v & 1) g_disr |= 0x40;                         // break request -> BRKINT
        di_update_irq();
        break;
    case 0x04:
        g_dicvr = (g_dicvr & ~0x2u) | (v & 0x2);
        g_dicvr &= ~(v & 0x4);
        di_update_irq();
        break;
    case 0x08: g_cmd[0] = v; break;
    case 0x0C: g_cmd[1] = v; break;
    case 0x10: g_cmd[2] = v; break;
    case 0x14: g_dimar = v & 0x03FFFFE0; break;
    case 0x18: g_dilen = v & ~0x1Fu; break;
    case 0x1C:
        g_dicr = v & 7;
        if ((v & 1) && !g_busy) di_execute();
        break;
    case 0x20: g_diimm = v; break;
    case 0x24: g_dicfg = v; break;
    }
}
