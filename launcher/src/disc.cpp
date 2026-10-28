#include "disc.h"

#include "util.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <map>
#include <set>

namespace nla {
namespace {

constexpr uint64_t kBootEnd = 0x0440;
constexpr uint64_t kBi2End = 0x2000;
constexpr uint64_t kApploader = 0x2440;
constexpr uint64_t kApploaderHeader = 0x20;
constexpr uint64_t kHoleChunk = 1 << 21;

uint32_t be32(const uint8_t* data) {
    return (static_cast<uint32_t>(data[0]) << 24) | (static_cast<uint32_t>(data[1]) << 16) |
           (static_cast<uint32_t>(data[2]) << 8) | data[3];
}

uint32_t le32(const uint8_t* data) {
    return (static_cast<uint32_t>(data[3]) << 24) | (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[1]) << 8) | data[0];
}

class DiscImage {
  public:
    bool open(const fs::path& path, std::wstring& error) {
        file_.open(path, std::ios::binary);
        if (!file_) {
            error = format(L"cannot open %ls", path.c_str());
            return false;
        }
        std::error_code ec;
        size_ = fs::file_size(path, ec);
        if (ec) {
            error = format(L"cannot measure %ls", path.c_str());
            return false;
        }
        std::vector<uint8_t> header(0x8000);
        file_.read(reinterpret_cast<char*>(header.data()),
                   static_cast<std::streamsize>(header.size()));
        auto got = static_cast<size_t>(file_.gcount());
        file_.clear();
        if (got >= 8 && std::memcmp(header.data(), "CISO", 4) == 0) {
            block_ = le32(header.data() + 4);
            if (block_ == 0) {
                error = L"the CISO header has a block size of zero";
                return false;
            }
            size_t blocks = got > 8 ? got - 8 : 0;
            uint64_t stored = 0;
            for (size_t i = 0; i < blocks; ++i) {
                if (header[8 + i] == 1) map_[i] = static_cast<int64_t>(stored++);
            }
            size_ = std::max<uint64_t>(size_, static_cast<uint64_t>(blocks) * block_);
        }
        return true;
    }

    uint64_t size() const { return size_; }

    bool read(uint64_t offset, size_t bytes, std::vector<uint8_t>& out,
              std::wstring& error) {
        out.assign(bytes, 0);
        if (bytes == 0) return true;
        if (offset >= size_) {
            error = format(L"read of %zu bytes at %#llx is past the end of the image",
                           bytes, static_cast<unsigned long long>(offset));
            return false;
        }
        if (map_.empty()) {
            file_.clear();
            file_.seekg(static_cast<std::streamoff>(offset));
            file_.read(reinterpret_cast<char*>(out.data()),
                       static_cast<std::streamsize>(bytes));
            file_.clear();
            return true;
        }
        size_t done = 0;
        while (done < bytes) {
            uint64_t at = offset + done;
            uint64_t block = at / block_;
            uint64_t within = at % block_;
            size_t take = static_cast<size_t>(
                std::min<uint64_t>(bytes - done, block_ - within));
            auto it = map_.find(static_cast<size_t>(block));
            if (it == map_.end()) {
                done += take;
                continue;
            }
            file_.clear();
            file_.seekg(static_cast<std::streamoff>(0x8000 + it->second * block_ + within));
            file_.read(reinterpret_cast<char*>(out.data() + done),
                       static_cast<std::streamsize>(take));
            auto got = static_cast<size_t>(file_.gcount());
            file_.clear();
            if (got < take) {
                error = format(L"the CISO is shorter than its block table says");
                return false;
            }
            done += take;
        }
        return true;
    }

  private:
    std::ifstream file_;
    uint64_t size_ = 0;
    uint32_t block_ = 0;
    std::map<size_t, int64_t> map_;
};

struct Piece {
    uint64_t offset;
    uint64_t size;
    std::wstring path;
    uint64_t file_offset;
};

bool trim_name(std::wstring& path) {
    std::vector<wstring> parts;
    for (const auto& part : split(path, L'/')) {
        if (part.empty() || part == L"." || part == L"..") continue;
        parts.push_back(part);
    }
    if (parts.empty()) return false;
    path = join(parts, L"/");
    return true;
}

std::vector<Piece> cover(std::vector<Piece> pieces) {
    std::sort(pieces.begin(), pieces.end(),
              [](const Piece& a, const Piece& b) { return a.offset < b.offset; });
    std::vector<Piece> out;
    uint64_t end = 0;
    for (auto piece : pieces) {
        if (piece.offset < end) {
            uint64_t skip = end - piece.offset;
            if (skip >= piece.size) continue;
            piece.offset += skip;
            piece.file_offset += skip;
            piece.size -= skip;
        }
        out.push_back(piece);
        end = piece.offset + piece.size;
    }
    return out;
}

bool read_exact(DiscImage& image, uint64_t offset, size_t bytes, std::vector<uint8_t>& out,
                std::wstring& error) {
    if (!image.read(offset, bytes, out, error)) return false;
    if (out.size() < bytes) {
        out.resize(bytes, 0);
    }
    return true;
}

struct FstFile {
    std::wstring path;
    uint64_t offset;
    uint64_t size;
};

bool walk_fst(const std::vector<uint8_t>& fst, std::vector<FstFile>& files,
              std::wstring& error) {
    if (fst.size() < 12) {
        error = L"the disc's file table is too small";
        return false;
    }
    uint32_t count = be32(fst.data() + 8);
    size_t string_table = static_cast<size_t>(count) * 12;
    if (string_table > fst.size()) {
        error = L"the disc's file table name area is past its end";
        return false;
    }
    auto name_at = [&](uint32_t offset, std::wstring& name) {
        size_t at = string_table + offset;
        if (at >= fst.size()) return false;
        size_t end = at;
        while (end < fst.size() && fst[end] != 0) ++end;
        if (end == at) return false;
        std::string raw(reinterpret_cast<const char*>(fst.data() + at), end - at);
        name = widen(raw);
        return true;
    };

    std::function<bool(uint32_t, const std::wstring&, uint32_t&)> walk;
    walk = [&](uint32_t index, const std::wstring& prefix, uint32_t& next) -> bool {
        size_t at = static_cast<size_t>(index) * 12;
        if (at + 12 > fst.size()) {
            error = L"a file table entry is past the end of the table";
            return false;
        }
        uint32_t type = be32(fst.data() + at);
        uint32_t offset = be32(fst.data() + at + 4);
        uint32_t size = be32(fst.data() + at + 8);
        std::wstring name;
        if (!name_at(type & 0xFFFFFF, name)) {
            error = L"a file table name is missing or unterminated";
            return false;
        }
        if ((type >> 24) == 1) {
            std::wstring path = prefix + L"/" + name;
            uint32_t end = size;
            if (end > count) {
                error = format(L"directory %ls ends past the file table", path.c_str());
                return false;
            }
            uint32_t i = index + 1;
            while (i < end) {
                uint32_t resumed = 0;
                if (!walk(i, path, resumed)) return false;
                i = resumed;
            }
            next = end;
            return true;
        }
        files.push_back({prefix + L"/" + name, offset, size});
        next = index + 1;
        return true;
    };

    uint32_t index = 1;
    while (index < count) {
        uint32_t next = 0;
        if (!walk(index, L"files", next)) return false;
        if (next == 0 || next <= index) return false;
        index = next;
    }
    return true;
}

int entry_shift(const std::vector<uint8_t>& fst, DiscImage& image, uint64_t image_size,
                std::wstring& error) {
    if (fst.size() < 12) {
        error = L"the disc's file table is too small";
        return -1;
    }
    uint32_t count = be32(fst.data() + 8);
    struct Entry {
        uint64_t size;
        uint64_t offset;
    };
    std::vector<Entry> files;
    for (uint32_t i = 0; i < count; ++i) {
        size_t at = static_cast<size_t>(i) * 12;
        if (at + 12 > fst.size()) break;
        uint32_t type = be32(fst.data() + at);
        uint32_t offset = be32(fst.data() + at + 4);
        uint32_t size = be32(fst.data() + at + 8);
        if ((type >> 24) == 0 && size != 0) files.push_back({size, offset});
    }
    std::sort(files.begin(), files.end(),
              [](const Entry& a, const Entry& b) { return a.size > b.size; });
    uint64_t last = 0;
    for (const auto& file : files) last = std::max(last, file.offset + file.size);

    std::vector<int> fits;
    for (int shift : {2, 0}) {
        if ((last << shift) <= image_size) fits.push_back(shift);
    }
    if (fits.empty()) {
        error = format(L"the disc file table does not fit in the image (%llu bytes)",
                       static_cast<unsigned long long>(image_size));
        return -1;
    }
    if (fits.size() == 1) return fits[0];
    for (int shift : fits) {
        for (size_t i = 0; i < files.size() && i < 4; ++i) {
            uint64_t at = (files[i].offset << shift) + files[i].size / 4;
            size_t length = static_cast<size_t>(
                std::min<uint64_t>(files[i].size / 4 ? files[i].size / 4 : 1, 256));
            std::vector<uint8_t> sample;
            if (!read_exact(image, at, length, sample, error)) return -1;
            if (std::any_of(sample.begin(), sample.end(),
                            [](uint8_t byte) { return byte != 0; }))
                return shift;
        }
    }
    return fits[0];
}

bool write_all(const fs::path& path, const std::vector<uint8_t>& data, std::wstring& error) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        error = format(L"cannot write %ls", path.c_str());
        return false;
    }
    file.write(reinterpret_cast<const char*>(data.data()),
               static_cast<std::streamsize>(data.size()));
    if (!file.good()) {
        error = format(L"cannot write %ls", path.c_str());
        return false;
    }
    return true;
}

}  // namespace

bool needs_nodtool(const fs::path& image) {
    auto extension = to_lower(image.extension().wstring());
    return extension != L".iso" && extension != L".ciso";
}

ExtractOutcome extract_disc(const ExtractRequest& request, const ExtractProgress& progress,
                            const std::atomic_bool& cancel) {
    ExtractOutcome outcome;
    fs::path staging = fs::path(request.outdir.wstring() + L".new");
    auto fail = [&](wstring message) {
        std::error_code remove_ec;
        fs::remove_all(staging, remove_ec);
        outcome.error = std::move(message);
        return outcome;
    };
    auto report = [&](int percent, const std::wstring& stage) {
        if (progress) progress(percent, stage);
    };

    DiscImage image;
    if (!image.open(request.image, outcome.error)) return outcome;

    std::vector<uint8_t> header;
    if (!read_exact(image, 0, 0x440, header, outcome.error)) return outcome;
    if (header.size() < 0x440) return fail(L"the image is too small to be a GameCube disc");
    std::string disc_bytes(reinterpret_cast<const char*>(header.data()), 6);
    auto found_id = widen(disc_bytes);
    if (found_id.rfind(request.disc_id, 0) != 0) {
        return fail(format(L"%ls is disc %ls, not %ls", request.image.filename().c_str(),
                           found_id.c_str(), request.disc_id.c_str()));
    }

    uint64_t dol_offset = be32(header.data() + 0x420);
    uint64_t fst_offset = be32(header.data() + 0x424);
    uint64_t fst_size = be32(header.data() + 0x428);

    std::vector<uint8_t> dol_header;
    if (!read_exact(image, dol_offset, 0x100, dol_header, outcome.error)) return outcome;
    uint64_t dol_size = 0;
    for (int i = 0; i < 18; ++i) {
        uint32_t offset = be32(dol_header.data() + i * 4);
        uint32_t size = be32(dol_header.data() + 0x90 + i * 4);
        if (size) dol_size = std::max<uint64_t>(dol_size, static_cast<uint64_t>(offset) + size);
    }
    if (dol_size == 0) return fail(L"the disc's main.dol header names no sections");

    report(4, L"Reading main.dol…");
    std::vector<uint8_t> dol;
    if (!read_exact(image, dol_offset, static_cast<size_t>(dol_size), dol, outcome.error))
        return outcome;
    outcome.dol_sha1 = sha1_hex(dol.data(), dol.size());
    if (!request.dol_sha1.empty() &&
        to_lower(outcome.dol_sha1) != to_lower(request.dol_sha1)) {
        return fail(format(
            L"main.dol SHA-1 %ls is not the supported build (%ls).\n\n"
            L"This launcher only builds the supported USA disc.",
            outcome.dol_sha1.c_str(), request.dol_sha1.c_str()));
    }
    if (cancel) return fail(L"cancelled");

    std::vector<uint8_t> fst;
    if (!read_exact(image, fst_offset, static_cast<size_t>(fst_size), fst, outcome.error))
        return outcome;
    int shift = entry_shift(fst, image, image.size(), outcome.error);
    if (shift < 0) return outcome;

    std::vector<FstFile> disc_files;
    if (!walk_fst(fst, disc_files, outcome.error)) return outcome;

    std::vector<uint8_t> apploader_header;
    if (!read_exact(image, kApploader, kApploaderHeader, apploader_header, outcome.error))
        return outcome;
    uint64_t apploader_code = be32(apploader_header.data() + 0x14);
    uint64_t apploader_trailer = be32(apploader_header.data() + 0x18);

    std::vector<Piece> pieces;
    pieces.push_back({0, kBootEnd, L"sys/boot.bin", 0});
    pieces.push_back({kBootEnd, kBi2End - kBootEnd, L"sys/bi2.bin", 0});
    pieces.push_back({kApploader, kApploaderHeader + apploader_code + apploader_trailer,
                      L"sys/apploader.img", 0});
    pieces.push_back({dol_offset, dol_size, L"sys/main.dol", 0});
    pieces.push_back({fst_offset, fst_size, L"sys/fst.bin", 0});
    for (const auto& file : disc_files) {
        std::wstring path = file.path;
        if (!trim_name(path)) return fail(L"the disc's file table has an unsafe path");
        pieces.push_back({file.offset << shift, file.size, path, 0});
    }

    auto extents = cover(pieces);
    uint64_t total = 0;
    for (const auto& piece : extents) total += piece.size;

    // Extract into a fresh tree, then swap it in, so a failed run leaves the old disc.
    std::error_code ec;
    fs::remove_all(staging, ec);

    report(8, L"Writing the disc tree…");
    uint64_t written = 0;
    for (const auto& piece : extents) {
        if (cancel) return fail(L"cancelled");
        std::vector<uint8_t> data;
        if (!read_exact(image, piece.offset, static_cast<size_t>(piece.size), data,
                        outcome.error))
            return outcome;
        fs::path target = staging;
        for (const auto& part : split(piece.path, L'/')) target /= part;
        if (!write_all(target, data, outcome.error)) return outcome;
        written += piece.size;
        int percent = 8 + static_cast<int>(total ? written * 82 / total : 0);
        report(std::min(percent, 90), format(L"Writing %ls…", piece.path.c_str()));
        ++outcome.file_count;
    }

    std::string manifest =
        "# GameCube disc manifest: offsets of the files beside it, extents to read them\n";
    manifest += "disc_id " + narrow(request.disc_id) + "\n";
    manifest += "dol_sha1 " + narrow(outcome.dol_sha1) + "\n";
    manifest += narrow(format(L"image_size %llx\n",
                              static_cast<unsigned long long>(image.size())));
    for (const auto& piece : extents) {
        manifest += narrow(format(L"extent %08llx %08llx %08llx %ls\n",
                                  static_cast<unsigned long long>(piece.offset),
                                  static_cast<unsigned long long>(piece.size),
                                  static_cast<unsigned long long>(piece.file_offset),
                                  piece.path.c_str()));
    }
    if (!write_text_file(staging / L"disc.manifest", manifest))
        return fail(L"cannot write disc.manifest");

    // Swap the new tree in.
    fs::path previous = fs::path(request.outdir.wstring() + L".old");
    fs::remove_all(previous, ec);
    if (fs::exists(request.outdir, ec)) fs::rename(request.outdir, previous, ec);
    fs::rename(staging, request.outdir, ec);
    if (ec) {
        return fail(format(L"cannot put the extracted disc in place: %ls",
                           widen(ec.message()).c_str()));
    }
    fs::remove_all(previous, ec);

    outcome.ok = true;
    outcome.bytes = written;
    report(100, L"Disc extracted");
    return outcome;
}

}  // namespace nla
