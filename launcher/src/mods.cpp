#include "mods.h"

#include "gecko.h"
#include "util.h"

#include "patch.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <map>

namespace nla {
namespace {

// The manifest's keys, lowercased, with the spaces the player may put around the "="
// taken off both ends.
std::wstring manifest_key(const std::wstring& line, std::wstring* value) {
    auto equals = line.find(L'=');
    if (equals == std::wstring::npos) return {};
    *value = trim(line.substr(equals + 1));
    return to_lower(trim(line.substr(0, equals)));
}

// A path a mod names in its manifest has to be inside the mod's own folder: nothing here
// reads a file the mod pointed at from somewhere else.
bool stays_inside(const std::wstring& relative) {
    if (relative.empty()) return false;
    const fs::path path(relative);
    if (path.is_absolute() || path.has_root_name() || path.has_root_directory()) return false;
    for (const auto& part : path) {
        if (part == L"..") return false;
    }
    return true;
}

// The files a mod supplies, as the paths an extracted disc uses.
std::vector<std::wstring> list_disc_files(const fs::path& disc_dir) {
    std::vector<std::wstring> files;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(
             disc_dir, fs::directory_options::skip_permission_denied, ec),
         end;
         !ec && it != end; it.increment(ec)) {
        std::error_code item_ec;
        if (!it->is_regular_file(item_ec)) continue;
        auto relative = fs::relative(it->path(), disc_dir, item_ec);
        if (item_ec) continue;
        files.push_back(relative.generic_wstring());
    }
    std::sort(files.begin(), files.end());
    return files;
}

// A delta in patches/ that a catalog entry names: SCON4-2.1.0.xdelta and scon4.xdelta are
// both SCON4's. The version is the catalog's business, not the file's, so an older download
// still counts as the entry being here.
bool delta_belongs(const std::wstring& id, const std::wstring& name, const fs::path& delta) {
    const auto file = to_lower(delta.filename().wstring());
    if (!ends_with_ci(file, L".xdelta")) return false;
    const auto wanted_id = to_lower(id);
    const auto wanted_name = to_lower(name);
    return file == wanted_id + L".xdelta" || file == wanted_name + L".xdelta" ||
           file.rfind(wanted_name + L"-", 0) == 0;
}

std::vector<fs::path> list_deltas(const fs::path& dir) {
    std::vector<fs::path> deltas;
    std::error_code ec;
    for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
         it != end; it.increment(ec)) {
        if (ec) break;
        std::error_code item_ec;
        if (!it->is_regular_file(item_ec)) continue;
        if (!ends_with_ci(it->path().filename().wstring(), L".xdelta")) continue;
        deltas.push_back(it->path());
    }
    std::sort(deltas.begin(), deltas.end());
    return deltas;
}

}  // namespace

bool parse_mod_manifest(const std::wstring& text, ModInfo* info) {
    if (!info) return false;
    info->name.clear();
    info->version.clear();
    info->author.clear();
    info->description.clear();
    info->disc_ids.clear();
    info->gecko_files.clear();

    for (const auto& raw : split(text, L'\n')) {
        auto line = trim(raw);
        if (line.empty() || line.front() == L'#') continue;
        std::wstring value;
        auto key = manifest_key(line, &value);
        if (key.empty()) continue;
        if (key == L"name") {
            info->name = value;
        } else if (key == L"version") {
            info->version = value;
        } else if (key == L"author") {
            info->author = value;
        } else if (key == L"description") {
            info->description = value;
        } else if (key == L"gecko") {
            if (!value.empty()) info->gecko_files.push_back(value);
        } else if (key == L"disc_id") {
            for (const auto& id : split(value, L',')) {
                auto trimmed = trim(id);
                if (!trimmed.empty()) info->disc_ids.push_back(trimmed);
            }
        }
    }
    for (auto& id : info->disc_ids) id = trim(id);
    // A mod with neither a name nor a build it is for is not a mod: an empty folder, or a
    // mod.txt written for something else.
    return !info->name.empty() || !info->disc_ids.empty();
}

std::vector<ModInfo> discover_mods(const fs::path& mods_dir) {
    std::vector<ModInfo> mods;
    std::error_code ec;
    if (!fs::is_directory(mods_dir, ec)) return mods;
    std::vector<fs::path> folders;
    for (const auto& entry : fs::directory_iterator(
             mods_dir, fs::directory_options::skip_permission_denied, ec)) {
        std::error_code item_ec;
        if (!entry.is_directory(item_ec)) continue;
        if (entry.path().filename().wstring().front() == L'.') continue;
        folders.push_back(entry.path());
    }
    std::sort(folders.begin(), folders.end(),
              [](const fs::path& a, const fs::path& b) {
                  return to_lower(a.filename().wstring()) < to_lower(b.filename().wstring());
              });
    for (const auto& dir : folders) {
        ModInfo mod;
        mod.dir = dir;
        mod.folder = dir.filename().wstring();
        if (!parse_mod_manifest(read_text_file(dir / L"mod.txt"), &mod)) continue;
        if (mod.name.empty()) mod.name = mod.folder;
        mod.files = list_disc_files(dir / L"disc");
        // A mod that carries no `gecko = ...` line is one whose codes are `gecko.txt`,
        // which is what a hand-written mod folder almost always has.
        std::error_code ec;
        if (mod.gecko_files.empty() && fs::is_regular_file(dir / L"gecko.txt", ec))
            mod.gecko_files.push_back(L"gecko.txt");
        auto outside = std::remove_if(mod.gecko_files.begin(), mod.gecko_files.end(),
                                      [](const std::wstring& file) {
                                          return !stays_inside(file);
                                      });
        mod.gecko_files.erase(outside, mod.gecko_files.end());
        mods.push_back(std::move(mod));
    }
    return mods;
}

bool mod_supports(const ModInfo& mod, const std::wstring& disc_id) {
    auto wanted = to_lower(disc_id);
    for (const auto& id : mod.disc_ids) {
        if (to_lower(id) == wanted) return true;
    }
    return false;
}

std::wstring unique_mod_id(const std::vector<ModChoice>& entries, const std::wstring& wanted) {
    std::wstring id = wanted;
    for (int suffix = 2;; ++suffix) {
        bool taken = false;
        for (const auto& entry : entries) {
            if (entry.id == id) taken = true;
        }
        if (!taken) return id;
        id = wanted + L"-" + std::to_wstring(suffix);
    }
}

std::vector<ModChoice> collect_mods(const std::vector<CatalogMod>& catalog,
                                    const fs::path& mods_dir, const fs::path& patches_dir,
                                    const std::wstring& disc_id) {
    std::vector<ModChoice> entries;

    for (std::size_t i = 0; i < catalog.size(); ++i) {
        const CatalogMod& known = catalog[i];
        if (!catalog_supports(known, disc_id)) continue;
        ModChoice entry;
        entry.id = known.id;
        entry.name = known.name.empty() ? known.id : known.name;
        entry.version = known.version;
        entry.author = known.author;
        entry.description = known.description;
        entry.disc_ids = known.disc_ids;
        entry.patch = known.patch;
        entry.known = true;
        entry.catalog = i;
        entries.push_back(std::move(entry));
    }

    for (auto& mod : discover_mods(mods_dir)) {
        if (!mod_supports(mod, disc_id)) continue;
        ModChoice* found = nullptr;
        for (auto& candidate : entries) {
            if (!candidate.patch && candidate.id == mod.folder) {
                found = &candidate;
                break;
            }
        }
        if (!found) {
            entries.push_back(ModChoice{});
            found = &entries.back();
            found->id = mod.folder;
        }
        found->installed = true;
        found->mod = std::move(mod);
        if (!found->mod.name.empty()) found->name = found->mod.name;
        if (!found->mod.version.empty()) found->version = found->mod.version;
        if (!found->mod.author.empty()) found->author = found->mod.author;
        if (!found->mod.description.empty()) found->description = found->mod.description;
    }

    for (const auto& delta : list_deltas(patches_dir)) {
        // A delta the catalog names is that mod's, and only a disc the mod names may list
        // it: a patch is written against one build, so a game's delta sitting in patches/
        // is not a row for every other game as well.
        bool named = false;
        bool supported = false;
        for (const auto& known : catalog) {
            if (!known.patch || !delta_belongs(known.id, known.name, delta)) continue;
            named = true;
            if (catalog_supports(known, disc_id)) supported = true;
        }
        if (named && !supported) continue;

        ModChoice* found = nullptr;
        for (auto& candidate : entries) {
            if (candidate.patch && delta_belongs(candidate.id, candidate.name, delta)) {
                found = &candidate;
                break;
            }
        }
        if (!found) {
            ModChoice local;
            local.id = unique_mod_id(entries, L"patch:" + delta.filename().wstring());
            local.name = delta.stem().wstring();
            local.patch = true;
            local.installed = true;
            local.delta = delta;
            entries.push_back(std::move(local));
            found = &entries.back();
        }
        found->installed = true;
        if (found->delta.empty() || found->delta.filename() != delta.filename())
            found->delta = delta;
    }

    return entries;
}

std::vector<DiscExtent> read_disc_extents(const fs::path& manifest) {
    std::vector<DiscExtent> extents;
    for (const auto& line : split(read_text_file(manifest), L'\n')) {
        auto trimmed = trim(line);
        if (trimmed.rfind(L"extent ", 0) != 0) continue;
        auto words = split(trimmed, L' ');
        if (words.size() < 5) continue;
        DiscExtent extent;
        try {
            extent.offset = std::stoull(narrow(words[1]), nullptr, 16);
            extent.size = std::stoull(narrow(words[2]), nullptr, 16);
            extent.file_offset = std::stoull(narrow(words[3]), nullptr, 16);
        } catch (...) {
            continue;
        }
        std::wstring path = words[4];
        for (size_t i = 5; i < words.size(); ++i) path += L" " + words[i];
        extent.path = path;
        extents.push_back(std::move(extent));
    }
    return extents;
}

std::wstring read_disc_id(const fs::path& manifest) {
    for (const auto& line : split(read_text_file(manifest), L'\n')) {
        auto trimmed = trim(line);
        if (trimmed.rfind(L"disc_id ", 0) != 0) continue;
        return trim(trimmed.substr(8));
    }
    return {};
}

OverlayBuild build_overlays(const std::vector<DiscExtent>& disc,
                            const std::vector<ModInfo>& mods, const fs::path& manifest_dir) {
    OverlayBuild build;
    // By path, so a later mod's copy of a file replaces an earlier mod's.
    std::map<std::wstring, OverlayExtent> chosen;
    // Which mod put each file there, so a second one replacing it can be reported: on a
    // console only one file can be at an offset, and a player should know which they see.
    std::map<std::wstring, std::wstring> chosen_by;
    for (const auto& mod : mods) {
        for (const auto& path : mod.files) {
            auto wanted = to_lower(path);
            const DiscExtent* found = nullptr;
            for (const auto& extent : disc) {
                if (to_lower(extent.path) == wanted) {
                    found = &extent;
                    break;
                }
            }
            if (!found) {
                build.warnings.push_back(format(L"%ls: %ls is not a file on this disc",
                                                mod.name.c_str(), path.c_str()));
                continue;
            }
            auto file = mod.dir / L"disc";
            for (const auto& part : split(path, L'/')) file /= part;
            std::error_code ec;
            auto size = fs::file_size(file, ec);
            if (ec) {
                build.warnings.push_back(format(L"%ls: cannot read %ls", mod.name.c_str(),
                                                path.c_str()));
                continue;
            }
            if (size > found->size) {
                build.warnings.push_back(
                    format(L"%ls: %ls is %llu bytes and the disc's file is %llu; it is left "
                           L"as the disc has it",
                           mod.name.c_str(), path.c_str(),
                           static_cast<unsigned long long>(size),
                           static_cast<unsigned long long>(found->size)));
                continue;
            }
            OverlayExtent overlay;
            overlay.offset = found->offset;
            overlay.size = found->size;
            overlay.available = size;
            auto relative = fs::relative(file, manifest_dir, ec);
            overlay.path = (ec ? file : relative).generic_wstring();
            auto previous = chosen_by.find(wanted);
            if (previous != chosen_by.end())
                build.warnings.push_back(
                    format(L"%ls replaces %ls, which %ls also replaces; %ls wins",
                           mod.name.c_str(), path.c_str(), previous->second.c_str(),
                           mod.name.c_str()));
            chosen_by[wanted] = mod.name;
            chosen[wanted] = std::move(overlay);
        }
    }
    for (auto& [path, extent] : chosen) {
        (void)path;
        build.extents.push_back(std::move(extent));
    }
    std::sort(build.extents.begin(), build.extents.end(),
              [](const OverlayExtent& a, const OverlayExtent& b) { return a.offset < b.offset; });
    return build;
}

std::string overlay_manifest_text(const std::vector<OverlayExtent>& extents) {
    std::string text =
        "# GameCube disc overlays: files a mod replaces, by the disc offsets they cover.\n"
        "# Written by the launcher; `overlay <offset> <size> <available> <path>`.\n";
    for (const auto& extent : extents) {
        text += narrow(format(L"overlay %08llx %08llx %08llx %ls\n",
                              static_cast<unsigned long long>(extent.offset),
                              static_cast<unsigned long long>(extent.size),
                              static_cast<unsigned long long>(extent.available),
                              extent.path.c_str()));
    }
    return text;
}

OverlaySync sync_overlay_manifest(const fs::path& disc_dir, const fs::path& disc_manifest,
                                  const std::vector<ModInfo>& mods) {
    OverlaySync result;
    const fs::path path = disc_dir / L"mods.manifest";
    // The tree's manifest names the disc it was extracted from, so even a list handed the
    // wrong mods cannot lay one build's files over another build's addresses.
    std::vector<DiscExtent> disc;
    std::wstring disc_id;
    if (!mods.empty()) {
        disc = read_disc_extents(disc_manifest);
        if (!disc.empty()) disc_id = read_disc_id(disc_manifest);
    }
    std::vector<ModInfo> fitting;
    std::vector<std::wstring> refused;
    for (const auto& mod : mods) {
        if (disc_id.empty() || mod_supports(mod, disc_id)) {
            fitting.push_back(mod);
        } else {
            refused.push_back(format(L"%ls is for disc %ls, and this disc is %ls",
                                     mod.name.c_str(), join(mod.disc_ids, L", ").c_str(),
                                     disc_id.c_str()));
        }
    }
    OverlayBuild build;
    if (!disc.empty()) build = build_overlays(disc, fitting, disc_dir);
    build.warnings.insert(build.warnings.begin(), refused.begin(), refused.end());
    result.warnings = build.warnings;
    if (build.extents.empty()) {
        std::error_code ec;
        result.wrote = fs::remove(path, ec);
        return result;
    }
    result.files = build.extents.size();
    auto text = overlay_manifest_text(build.extents);
    if (read_text_file(path) == widen(text)) return result;
    result.wrote = write_text_file(path, text);
    return result;
}

ModProgram sync_mod_programs(const fs::path& disc_dir, const std::vector<ModInfo>& mods) {
    ModProgram result;
    // The same disc check the overlays get: a code written for one build's addresses must
    // never run on another's.
    const auto disc_id = mods.empty() ? std::wstring() : read_disc_id(disc_dir / L"disc.manifest");
    std::string ops;
    std::vector<std::pair<uint32_t, uint32_t>> words;
    for (const auto& mod : mods) {
        // A mod without a disc_id is read as willing to run anywhere, as its files are.
        if (!disc_id.empty() && !mod.disc_ids.empty() && !mod_supports(mod, disc_id)) continue;
        std::string mod_ops;
        for (const auto& file : mod.gecko_files) {
            auto program = parse_gecko(read_text_file(mod.dir / file), mod.folder + L"/" + file);
            mod_ops += program.ops;
            words.insert(words.end(), program.words.begin(), program.words.end());
            result.warnings.insert(result.warnings.end(), program.warnings.begin(),
                                   program.warnings.end());
        }
        if (mod_ops.empty()) continue;
        ++result.codes;
        ops += narrow(format(L"# %ls\n", mod.name.c_str()));
        ops += mod_ops;
    }
    // A later mod's word wins, as its files do where two replace the same one.
    std::map<uint32_t, uint32_t> last;
    for (const auto& [address, word] : words) last[address] = word;
    std::string code;
    for (const auto& [address, word] : last)
        code += narrow(format(L"%08X %08X\n", address, word));

    for (const auto& line : split(widen(ops), L'\n')) {
        auto text = trim(line);
        if (!text.empty() && text.front() != L'#') ++result.ops;
    }
    result.words = last.size();

    auto sync = [&result](const fs::path& path, const std::string& text) {
        if (text.empty()) {
            std::error_code ec;
            if (fs::remove(path, ec)) result.wrote = true;
            return;
        }
        if (read_text_file(path) == widen(text)) return;
        if (write_text_file(path, text)) result.wrote = true;
    };
    sync(disc_dir / L"mods.gecko", ops);
    sync(disc_dir / L"mods.code", code);
    return result;
}

// -- disc patches -----------------------------------------------------------

namespace {

// The delta and the disc tree it decodes from, as the decoder asks for them.
struct ReadFile {
    std::FILE* f = nullptr;
    std::uint64_t size = 0;
};

bool read_file_at(void* user, std::uint64_t offset, void* dst, std::uint32_t len) {
    ReadFile* file = static_cast<ReadFile*>(user);
    if (!file->f) return false;
    if (offset >= file->size) return false;
    const std::uint32_t n = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(len, file->size - offset));
    if (std::fseek(file->f, static_cast<long>(offset), SEEK_SET) != 0) return false;
    if (std::fread(dst, 1, n, file->f) != n) return false;
    if (n < len) std::memset(static_cast<std::uint8_t*>(dst) + n, 0, len - n);
    return true;
}

ReadFile open_for_read(const fs::path& path) {
    ReadFile file;
#if defined(_WIN32)
    file.f = _wfopen(path.c_str(), L"rb");
#else
    file.f = std::fopen(path.c_str(), "rb");
#endif
    if (!file.f) return file;
    std::fseek(file.f, 0, SEEK_END);
    file.size = static_cast<std::uint64_t>(std::ftell(file.f));
    return file;
}

// The disc a tree holds, read by offset: its files where they cover the offset, zeroes in
// the space between them -- which is what the disc itself reads as there. Files are opened
// as a read needs them and not before: a disc's tree is hundreds of files, and holding
// every one of them open runs into the C library's limit on open streams.
struct Tree {
    std::vector<DiscExtent> extents;  // sorted by offset, no overlaps
    std::vector<fs::path> paths;      // one per extent, in the same order
    std::uint64_t size = 0;
    // The file the last read used, kept open because a window's reads run through the
    // same few extents one after another.
    std::size_t open_index = static_cast<std::size_t>(-1);
    ReadFile open;

    const DiscExtent* at(std::uint64_t offset, std::size_t* index) const {
        std::size_t lo = 0, hi = extents.size();
        while (lo < hi) {
            const std::size_t mid = lo + (hi - lo) / 2;
            if (extents[mid].offset <= offset) lo = mid + 1;
            else hi = mid;
        }
        if (index) *index = lo;
        return lo && offset < extents[lo - 1].offset + extents[lo - 1].size
                   ? &extents[lo - 1]
                   : nullptr;
    }

    ReadFile* file_at(std::size_t index) {
        if (index == open_index) return open.f ? &open : nullptr;
        if (open.f) std::fclose(open.f);
        open = open_for_read(paths[index]);
        open_index = index;
        return open.f ? &open : nullptr;
    }

    void close() {
        if (open.f) std::fclose(open.f);
        open.f = nullptr;
        open_index = static_cast<std::size_t>(-1);
    }
};

bool read_tree_at(void* user, std::uint64_t offset, void* dst, std::uint32_t len) {
    Tree* tree = static_cast<Tree*>(user);
    std::uint8_t* p = static_cast<std::uint8_t*>(dst);
    std::uint32_t done = 0;
    while (done < len) {
        std::size_t index = 0;
        const DiscExtent* e = tree->at(offset, &index);
        std::uint32_t n;
        if (e) {
            n = static_cast<std::uint32_t>(
                std::min<std::uint64_t>(len - done, e->offset + e->size - offset));
            const std::size_t nth = static_cast<std::size_t>(e - tree->extents.data());
            ReadFile* file = tree->file_at(nth);
            if (!file || !read_file_at(file, e->file_offset + (offset - e->offset), p + done, n)) {
                return false;
            }
        } else {
            if (offset >= tree->size) return false;
            const std::uint64_t next =
                index < tree->extents.size() ? tree->extents[index].offset : tree->size;
            n = static_cast<std::uint32_t>(
                std::min<std::uint64_t>(len - done, next - offset));
            std::memset(p + done, 0, n);
        }
        offset += n;
        done += n;
    }
    return true;
}

// The space between a tree's files: what a dump may hold as zeros and another dump of the
// same disc may hold as whatever was on the medium.
std::vector<std::pair<std::uint64_t, std::uint64_t>> gaps_of(const Tree& tree) {
    std::vector<std::pair<std::uint64_t, std::uint64_t>> gaps;
    std::uint64_t end = 0;
    for (const DiscExtent& e : tree.extents) {
        if (e.offset > end) gaps.emplace_back(end, e.offset - end);
        end = std::max(end, e.offset + e.size);
    }
    if (tree.size > end) gaps.emplace_back(end, tree.size - end);
    return gaps;
}

// The parts of the disc the extractor did not find a file in, written out as padding. A
// dump of a disc fills those with whatever was on the medium; a delta made from a dump
// that zeroed them reads them as zeros, which is what this normalisation says.
std::vector<std::pair<std::uint64_t, std::uint64_t>> padding_regions(const Tree& tree) {
    std::vector<std::pair<std::uint64_t, std::uint64_t>> regions;
    for (const DiscExtent& e : tree.extents) {
        const std::wstring path = to_lower(e.path);
        if (path.find(L"padding") == std::wstring::npos) continue;
        regions.emplace_back(e.offset, e.size);
    }
    return regions;
}

// The lines of a file, with the CR a Windows-written one carries taken off each.
std::vector<std::wstring> text_lines(const std::wstring& text) {
    std::vector<std::wstring> lines;
    std::wstring line;
    for (const wchar_t c : text) {
        if (c == L'\n') {
            lines.push_back(line);
            line.clear();
        } else if (c != L'\r') {
            line.push_back(c);
        }
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

Tree open_tree(const fs::path& disc_dir, std::wstring* error) {
    Tree tree;
    const fs::path manifest_path = disc_dir / L"disc.manifest";
    const std::wstring text = read_text_file(manifest_path);
    if (text.empty()) {
        if (error) *error = L"The disc has not been extracted yet.";
        return tree;
    }
    for (const std::wstring& line : text_lines(text)) {
        std::wstring rest = trim(line);
        if (rest.empty() || rest.front() == L'#') continue;
        if (rest.rfind(L"image_size ", 0) == 0) {
            tree.size = std::stoull(trim(rest.substr(11)), nullptr, 16);
            continue;
        }
        if (rest.rfind(L"extent ", 0) != 0) continue;
        rest = trim(rest.substr(7));
        const std::size_t a = rest.find(L' ');
        const std::size_t b = a == std::wstring::npos ? a : rest.find(L' ', a + 1);
        const std::size_t c = b == std::wstring::npos ? b : rest.find(L' ', b + 1);
        if (c == std::wstring::npos) continue;
        DiscExtent extent;
        extent.offset = std::stoull(rest.substr(0, a), nullptr, 16);
        extent.size = std::stoull(rest.substr(a + 1, b - a - 1), nullptr, 16);
        extent.file_offset = std::stoull(rest.substr(b + 1, c - b - 1), nullptr, 16);
        extent.path = trim(rest.substr(c + 1));
        tree.extents.push_back(std::move(extent));
    }
    std::sort(tree.extents.begin(), tree.extents.end(),
              [](const DiscExtent& a, const DiscExtent& b) { return a.offset < b.offset; });
    if (tree.extents.empty()) {
        if (error) *error = L"The disc manifest lists no files.";
        return tree;
    }
    if (!tree.size) tree.size = tree.extents.back().offset + tree.extents.back().size;
    tree.paths.reserve(tree.extents.size());
    for (const DiscExtent& e : tree.extents) tree.paths.push_back(disc_dir / e.path);
    return tree;
}

}  // namespace

PatchFit check_patch(const fs::path& delta, const fs::path& disc_dir) {
    PatchFit fit;
    std::wstring tree_error;
    Tree tree = open_tree(disc_dir, &tree_error);
    ReadFile file = open_for_read(delta);
    if (!file.f) {
        fit.error = L"That file could not be read.";
        return fit;
    }
    if (tree.paths.empty()) {
        fit.error = tree_error;
        std::fclose(file.f);
        return fit;
    }

    // What the delta expects the space between files to hold. A dump usually zeroes it, but
    // which regions those are is the delta's to say: each candidate is decoded and the one
    // whose windows agree most is kept, so nothing here is specific to one patch.
    std::vector<std::vector<std::pair<std::uint64_t, std::uint64_t>>> candidates;
    candidates.push_back({});  // the dump left that space alone
    const auto padding = padding_regions(tree);
    if (!padding.empty()) candidates.push_back(padding);
    const auto gaps = gaps_of(tree);
    if (!gaps.empty()) candidates.push_back(gaps);

    std::string decode_error;
    PatchManifest manifest;
    manifest.delta = narrow(delta.wstring());
    PatchFiles files;
    files.delta = read_file_at;
    files.delta_user = &file;
    files.source = read_tree_at;
    files.source_user = &tree;

    int best = -1;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        manifest.zero = candidates[i];
        if (!patch_open(manifest, files, &decode_error)) {
            if (i == 0) {
                fit.error = L"That file is not an xdelta patch the game can read.";
                std::fclose(file.f);
                return fit;
            }
            continue;
        }
        const PatchCheck check = patch_verify(12);
        if (best < 0 || check.matched > fit.matched) {
            best = static_cast<int>(i);
            fit.matched = check.matched;
            fit.sampled = check.decoded;
            fit.windows = check.windows;
            fit.zero = candidates[i];
        }
    }
    std::fclose(file.f);
    tree.close();
    if (best < 0) {
        fit.error = L"That file is not an xdelta patch the game can read.";
        return fit;
    }
    fit.opened = true;
    if (fit.sampled == 0) {
        fit.error = L"The patch decodes against no part of this disc.";
    }
    return fit;
}

bool write_patch_manifest(const fs::path& disc_dir, const fs::path& delta, const PatchFit& fit) {
    const fs::path path = disc_dir / L"patch.manifest";
    std::error_code ec;
    if (delta.empty()) return fs::remove(path, ec);
    std::string text;
    text += "# Written by the launcher: the patch this disc is played with.\n";
    text += "patch delta " + narrow(delta.wstring()) + "\n";
    for (const auto& region : fit.zero) {
        char line[128];
        std::snprintf(line, sizeof(line), "patch zero %llx %llx\n",
                      static_cast<unsigned long long>(region.first),
                      static_cast<unsigned long long>(region.second));
        text += line;
    }
    if (read_text_file(path) == widen(text)) return false;
    return write_text_file(path, text);
}

fs::path read_patch_delta(const fs::path& disc_dir) {
    for (const std::wstring& line : text_lines(read_text_file(disc_dir / L"patch.manifest"))) {
        const std::wstring rest = trim(line);
        if (rest.rfind(L"patch delta ", 0) != 0) continue;
        return fs::path(trim(rest.substr(12)));
    }
    return {};
}

std::optional<PatchFit> read_patch_fit(const fs::path& disc_dir, const fs::path& delta) {
    if (delta.empty()) return std::nullopt;
    const fs::path named = read_patch_delta(disc_dir);
    if (named.empty()) return std::nullopt;
    const auto same = [](const fs::path& a, const fs::path& b) {
        return to_lower(a.lexically_normal().generic_wstring()) ==
               to_lower(b.lexically_normal().generic_wstring());
    };
    if (!same(named, delta)) return std::nullopt;

    PatchFit fit;
    fit.opened = true;
    for (const std::wstring& line : text_lines(read_text_file(disc_dir / L"patch.manifest"))) {
        const std::wstring rest = trim(line);
        if (rest.rfind(L"patch zero ", 0) != 0) continue;
        unsigned long long offset = 0, size = 0;
        if (std::swscanf(rest.c_str() + 11, L"%llx %llx", &offset, &size) == 2)
            fit.zero.emplace_back(offset, size);
    }
    return fit;
}

}  // namespace nla
