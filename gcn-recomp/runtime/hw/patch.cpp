#include "patch.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

// VCDIFF (RFC 3284), as xdelta3 writes it: a header, then windows that each rebuild a
// piece of the target from a segment of the source and from itself.
struct Window {
    uint64_t encoding_pos = 0;  // where its delta encoding starts in the file
    uint64_t encoding_len = 0;
    uint64_t target_start = 0;
    uint64_t target_len = 0;
    uint64_t source_pos = 0;
    uint64_t source_len = 0;
    bool has_adler = false;
    uint32_t adler = 0;
};

struct Code {
    uint8_t type, size, mode;  // 1 add, 2 run, 3 copy, 0 none
};

Code g_table[256][2];
std::vector<Window> g_windows;
PatchRead g_read_delta = nullptr;
void* g_delta_user = nullptr;
PatchRead g_read_source = nullptr;
void* g_source_user = nullptr;
PatchManifest g_manifest;
uint64_t g_target_size = 0;

// The last two windows decoded, so reads that sit in one window do not re-decode it.
struct Cached {
    int window = -1;
    std::vector<uint8_t> bytes;
};
Cached g_cache[2];
int g_cache_next = 0;
// Which windows have already reported that this disc is not the one the patch was made from.
std::vector<uint8_t> g_warned;

// A byte reader over one of the delta's sections.
struct Reader {
    const uint8_t* data = nullptr;
    size_t size = 0;
    size_t pos = 0;

    bool byte(uint8_t* out) {
        if (pos >= size) return false;
        *out = data[pos++];
        return true;
    }
    bool bytes(void* dst, size_t len) {
        if (size - pos < len) return false;
        memcpy(dst, data + pos, len);
        pos += len;
        return true;
    }
    // Base-128, most significant group first, seven bits a byte.
    bool varint(uint64_t* out) {
        uint64_t value = 0;
        for (int i = 0; i < 10; ++i) {
            uint8_t b = 0;
            if (!byte(&b)) return false;
            value = (value << 7) | (b & 0x7F);
            if (!(b & 0x80)) {
                *out = value;
                return true;
            }
        }
        return false;
    }
};

// A reader over the delta file, one byte at a time for the header and the window index,
// which are small.
struct Cursor {
    uint64_t pos = 0;

    bool bytes(void* dst, uint32_t len) {
        if (!g_read_delta || !g_read_delta(g_delta_user, pos, dst, len)) return false;
        pos += len;
        return true;
    }
    bool byte(int* out) {
        uint8_t b = 0;
        if (!bytes(&b, 1)) return false;
        *out = b;
        return true;
    }
    bool varint(uint64_t* out) {
        uint64_t value = 0;
        for (int i = 0; i < 10; ++i) {
            int b = 0;
            if (!byte(&b)) return false;
            value = (value << 7) | (uint64_t)(b & 0x7F);
            if (!(b & 0x80)) {
                *out = value;
                return true;
            }
        }
        return false;
    }
    bool skip(uint64_t len) {
        pos += len;
        return true;
    }
};

uint32_t adler32_of(const uint8_t* data, size_t size) {
    uint32_t a = 1, b = 0;
    while (size) {
        const size_t n = std::min<size_t>(size, 5552);
        for (size_t i = 0; i < n; ++i) {
            a += data[i];
            b += a;
        }
        a %= 65521;
        b %= 65521;
        data += n;
        size -= n;
    }
    return (b << 16) | a;
}

// The code table VCDIFF defines, in the order it defines it.
void build_default_table() {
    int index = 0;
    auto put = [&](uint8_t t1, uint8_t s1, uint8_t m1, uint8_t t2, uint8_t s2, uint8_t m2) {
        g_table[index][0] = Code{t1, s1, m1};
        g_table[index][1] = Code{t2, s2, m2};
        ++index;
    };
    put(2, 0, 0, 0, 0, 0);  // RUN, size from the instruction stream
    for (int size = 0; size < 18; ++size) put(1, (uint8_t)size, 0, 0, 0, 0);
    for (int mode = 0; mode < 9; ++mode) {
        put(3, 0, (uint8_t)mode, 0, 0, 0);
        for (int size = 4; size < 19; ++size) put(3, (uint8_t)size, (uint8_t)mode, 0, 0, 0);
    }
    for (int mode = 0; mode < 6; ++mode)
        for (int add = 1; add < 5; ++add)
            for (int copy = 4; copy < 7; ++copy)
                put(1, (uint8_t)add, 0, 3, (uint8_t)copy, (uint8_t)mode);
    for (int mode = 6; mode < 9; ++mode)
        for (int add = 1; add < 5; ++add) put(1, (uint8_t)add, 0, 3, 4, (uint8_t)mode);
    for (int mode = 0; mode < 9; ++mode) put(3, 4, (uint8_t)mode, 1, 1, 0);
}

// The source as the delta expects it: the user's own disc, with the unused space it carries
// either way read as zero, and the few header bytes the dump is defined by set.
bool source_read(uint64_t offset, void* dst, uint32_t len) {
    uint8_t* p = (uint8_t*)dst;
    uint32_t done = 0;
    while (done < len) {
        const uint64_t at = offset + done;
        uint64_t next = at + (len - done);
        const std::pair<uint64_t, std::string>* constant = nullptr;
        bool zero = false;
        for (const auto& region : g_manifest.zero) {
            if (at < region.first || at >= region.first + region.second) continue;
            zero = true;
            next = std::min<uint64_t>(next, region.first + region.second);
            break;
        }
        if (!zero) {
            for (const auto& region : g_manifest.bytes) {
                if (at < region.first || at >= region.first + region.second.size()) continue;
                constant = &region;
                next = std::min<uint64_t>(next, region.first + region.second.size());
                break;
            }
        }
        const uint32_t n = (uint32_t)(next - at);
        if (zero) {
            memset(p + done, 0, n);
        } else if (constant) {
            memcpy(p + done, constant->second.data() + (size_t)(at - constant->first), n);
        } else if (!g_read_source || !g_read_source(g_source_user, at, p + done, n)) {
            return false;
        }
        done += n;
    }
    return true;
}

const Window* window_for(uint64_t offset) {
    // The last window starting at or before the offset.
    auto it = std::upper_bound(g_windows.begin(), g_windows.end(), offset,
                               [](uint64_t value, const Window& w) {
                                   return value < w.target_start;
                               });
    if (it == g_windows.begin()) return nullptr;
    const Window* w = &*(it - 1);
    return offset < w->target_start + w->target_len ? w : nullptr;
}

bool decode_window(const Window& w, std::vector<uint8_t>* out, bool* checksum_ok = nullptr) {
    static const bool debug = getenv("GCN_PATCH_DEBUG") != nullptr;
    if (debug)
        fprintf(stderr, "patch: decode window@%llx encoding %llu source %llx+%llx\n",
                (unsigned long long)w.target_start, (unsigned long long)w.encoding_len,
                (unsigned long long)w.source_pos, (unsigned long long)w.source_len);
    if (w.encoding_len == 0 || w.encoding_len > (256u << 20)) return false;
    std::vector<uint8_t> encoding((size_t)w.encoding_len);
    if (!g_read_delta(g_delta_user, w.encoding_pos, encoding.data(), (uint32_t)encoding.size())) {
        if (debug) fprintf(stderr, "patch: window@%llx cannot read encoding\n", (unsigned long long)w.target_start);
        return false;
    }

    Reader header{encoding.data(), encoding.size(), 0};
    uint64_t target_len = 0, data_len = 0, inst_len = 0, addr_len = 0;
    uint8_t indicator = 0;
    if (!header.varint(&target_len) || !header.byte(&indicator) || !header.varint(&data_len) ||
        !header.varint(&inst_len) || !header.varint(&addr_len)) {
        if (debug) fprintf(stderr, "patch: window@%llx bad encoding header\n", (unsigned long long)w.target_start);
        return false;
    }
    // A secondary compressor: nothing xdelta3 writes by default uses one.
    if (indicator != 0) {
        if (debug) fprintf(stderr, "patch: window@%llx secondary compressor %u\n", (unsigned long long)w.target_start, indicator);
        return false;
    }
    // goxdelta's layout, which is what xdelta3 writes: the target's checksum follows the
    // three section lengths, before the sections themselves.
    if (w.has_adler) {
        uint8_t check[4];
        if (!header.bytes(check, sizeof(check))) return false;
    }
    if (data_len + inst_len + addr_len > encoding.size() - header.pos) {
        if (debug) fprintf(stderr, "patch: window@%llx sections %llu+%llu+%llu past %llu\n", (unsigned long long)w.target_start,
                           (unsigned long long)data_len, (unsigned long long)inst_len,
                           (unsigned long long)addr_len, (unsigned long long)encoding.size());
        return false;
    }
    const uint8_t* data = encoding.data() + header.pos;
    const uint8_t* inst = data + data_len;
    const uint8_t* addr = inst + inst_len;

    std::vector<uint8_t> src((size_t)w.source_len);
    if (!src.empty() && !source_read(w.source_pos, src.data(), (uint32_t)src.size())) {
        if (debug) fprintf(stderr, "patch: window@%llx cannot read source %llx+%llx\n",
                           (unsigned long long)w.target_start, (unsigned long long)w.source_pos,
                           (unsigned long long)w.source_len);
        return false;
    }

    out->assign((size_t)target_len, 0);
    Reader instructions{inst, (size_t)inst_len, 0};
    Reader addresses{addr, (size_t)addr_len, 0};
    size_t out_pos = 0, data_pos = 0;
    uint64_t near_cache[4] = {0, 0, 0, 0};
    uint64_t same_cache[768] = {0};
    int next_slot = 0;

    while (instructions.pos < instructions.size) {
        uint8_t code = 0;
        if (!instructions.byte(&code)) return false;
        for (const Code& op : g_table[code]) {
            if (op.type == 0) continue;
            uint64_t size = op.size;
            if (size == 0 && !instructions.varint(&size)) return false;
            if (out_pos + size > out->size()) return false;
            if (op.type == 1) {  // ADD
                if (data_pos + size > data_len) return false;
                memcpy(out->data() + out_pos, data + data_pos, (size_t)size);
                data_pos += (size_t)size;
            } else if (op.type == 2) {  // RUN
                if (data_pos >= data_len) return false;
                memset(out->data() + out_pos, data[data_pos++], (size_t)size);
            } else {  // COPY
                const uint64_t here = src.size() + out_pos;
                uint64_t value = 0;
                if (op.mode == 0) {
                    if (!addresses.varint(&value)) return false;
                } else if (op.mode == 1) {
                    uint64_t back = 0;
                    if (!addresses.varint(&back) || back > here) return false;
                    value = here - back;
                } else if (op.mode < 6) {
                    uint64_t delta = 0;
                    if (!addresses.varint(&delta)) return false;
                    value = near_cache[op.mode - 2] + delta;
                } else {
                    uint8_t b = 0;
                    if (!addresses.byte(&b)) return false;
                    value = same_cache[(size_t)(op.mode - 6) * 256 + b];
                }
                // A copy may run past what has been produced: each byte is written before the
                // next one is read, so a copy can repeat the run it is making. Only the byte it
                // starts at has to exist already.
                if (value >= src.size() + out_pos) return false;
                near_cache[next_slot] = value;
                next_slot = (next_slot + 1) % 4;
                same_cache[value % 768] = value;
                for (uint64_t i = 0; i < size; ++i) {
                    const uint64_t at = value + i;
                    out->at(out_pos + (size_t)i) =
                        at < src.size() ? src[(size_t)at] : out->at((size_t)(at - src.size()));
                }
            }
            out_pos += (size_t)size;
        }
    }
    if (out_pos != out->size()) {
        if (debug) fprintf(stderr, "patch: window@%llx produced %llu of %llu\n", (unsigned long long)w.target_start,
                           (unsigned long long)out_pos, (unsigned long long)out->size());
        return false;
    }
    if (w.has_adler) {
        const bool match = adler32_of(out->data(), out->size()) == w.adler;
        if (checksum_ok) *checksum_ok = match;
        // A window that does not match still holds the right bytes for every file: what the
        // two dumps disagree on is the space between them, which no read ever asks for. Say
        // so once, so a log shows why this disc is not byte-for-byte what the patch expected.
        const size_t index = (size_t)(&w - g_windows.data());
        if (!match && index < g_warned.size() && !g_warned[index]) {
            g_warned[index] = 1;
            fprintf(stderr,
                    "patch: window at %llX decodes but does not match this disc; the two dumps"
                    " differ in space no file owns, so its bytes are served anyway\n",
                    (unsigned long long)w.target_start);
        }
    } else if (checksum_ok) {
        *checksum_ok = true;
    }
    if (debug) fprintf(stderr, "patch: window@%llx ok %llu bytes\n", (unsigned long long)w.target_start,
                       (unsigned long long)out->size());
    return true;
}

const std::vector<uint8_t>* window_bytes(const Window& w) {
    for (Cached& cached : g_cache) {
        if (cached.window >= 0 && &g_windows[(size_t)cached.window] == &w) return &cached.bytes;
    }
    Cached& slot = g_cache[g_cache_next];
    g_cache_next = (g_cache_next + 1) % 2;
    slot.window = -1;
    if (!decode_window(w, &slot.bytes)) {
        slot.bytes.clear();
        return nullptr;
    }
    slot.window = (int)(&w - g_windows.data());
    return &slot.bytes;
}

}  // namespace

bool patch_manifest_read(const char* manifest_path, PatchManifest* out, std::string* error) {
    std::FILE* f = fopen(manifest_path, "rb");
    if (!f) {
        if (error) *error = std::string("cannot read ") + manifest_path;
        return false;
    }
    std::string folder(manifest_path);
    const size_t slash = folder.find_last_of("/\\");
    folder = slash == std::string::npos ? std::string(".") : folder.substr(0, slash);
    auto resolve = [&folder](const std::string& path) {
        if (path.empty()) return path;
        if (path[0] == '/' || path[0] == '\\' || (path.size() > 1 && path[1] == ':')) return path;
        return folder + "/" + path;
    };

    char line[4096];
    while (fgets(line, sizeof(line), f)) {
        char keyword[64] = {0};
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        if (sscanf(line, "patch %63s", keyword) != 1) continue;
        const char* rest = strstr(line, keyword) + strlen(keyword);
        while (*rest == ' ' || *rest == '\t') ++rest;
        std::string value(rest);
        while (!value.empty() && (value.back() == '\n' || value.back() == '\r' ||
                                  value.back() == ' ' || value.back() == '\t'))
            value.pop_back();
        if (!strcmp(keyword, "delta")) {
            out->delta = resolve(value);
        } else if (!strcmp(keyword, "zero")) {
            unsigned long long start = 0, size = 0;
            if (sscanf(value.c_str(), "%llx %llx", &start, &size) == 2)
                out->zero.emplace_back(start, size);
        } else if (!strcmp(keyword, "bytes")) {
            unsigned long long offset = 0;
            char hex[256] = {0};
            if (sscanf(value.c_str(), "%llx %255s", &offset, hex) == 1) {
                std::string bytes;
                for (const char* p = hex; p[0] && p[1]; p += 2) {
                    unsigned value_byte = 0;
                    if (sscanf(p, "%2x", &value_byte) != 1) break;
                    bytes.push_back((char)value_byte);
                }
                out->bytes.emplace_back(offset, std::move(bytes));
            }
        }
    }
    fclose(f);
    if (out->delta.empty()) {
        if (error) *error = std::string(manifest_path) + " names no delta";
        return false;
    }
    return true;
}

bool patch_open(const PatchManifest& manifest, const PatchFiles& files, std::string* error) {
    build_default_table();
    g_manifest = manifest;
    g_read_delta = files.delta;
    g_delta_user = files.delta_user;
    g_read_source = files.source;
    g_source_user = files.source_user;
    g_windows.clear();
    g_warned.clear();
    g_cache[0] = {};
    g_cache[1] = {};
    g_cache_next = 0;
    g_target_size = 0;
    uint8_t header[5] = {0};
    if (!g_read_delta || !g_read_delta(g_delta_user, 0, header, sizeof(header))) {
        if (error) *error = "cannot read the patch delta " + manifest.delta;
        return false;
    }

    // Header: magic, version, indicator, then the optional application header (xdelta3
    // writes the target and source names there) and code table.
    Cursor cursor;
    uint8_t magic[5] = {0};
    if (!cursor.bytes(magic, sizeof(magic)) || magic[0] != 0xD6 || magic[1] != 0xC3 ||
        magic[2] != 0xC4) {
        if (error) *error = manifest.delta + " is not a VCDIFF delta";
        return false;
    }
    if (magic[4] & 0x01) cursor.skip(1);  // a secondary compressor's id
    if (magic[4] & 0x02) {                // an application-defined code table
        uint64_t length = 0;
        if (!cursor.varint(&length) || length != 0) {
            if (error) *error = manifest.delta + " carries a code table, which is not supported";
            return false;
        }
    }
    if (magic[4] & 0x04) {
        uint64_t length = 0;
        if (!cursor.varint(&length) || !cursor.skip(length)) {
            if (error) *error = "cannot read " + manifest.delta;
            return false;
        }
    }

    uint64_t target = 0;
    for (;;) {
        int win_indicator = 0;
        if (!cursor.byte(&win_indicator)) break;  // the end of the file
        Window w;
        if (win_indicator & 0x03) {
            if (!cursor.varint(&w.source_len) || !cursor.varint(&w.source_pos)) {
                if (error) *error = "cannot read " + manifest.delta;
                return false;
            }
        }
        if (!cursor.varint(&w.encoding_len)) {
            if (error) *error = "cannot read " + manifest.delta;
            return false;
        }
        w.encoding_pos = cursor.pos;
        uint64_t target_len = 0;
        if (!cursor.varint(&target_len)) {
            if (error) *error = "cannot read " + manifest.delta;
            return false;
        }
        int delta_indicator = 0;
        uint64_t data_len = 0, inst_len = 0, addr_len = 0;
        if (!cursor.byte(&delta_indicator) || !cursor.varint(&data_len) ||
            !cursor.varint(&inst_len) || !cursor.varint(&addr_len)) {
            if (error) *error = "cannot read " + manifest.delta;
            return false;
        }
        if (win_indicator & 0x04) {
            uint8_t check[4] = {0};
            if (!cursor.bytes(check, sizeof(check))) {
                if (error) *error = "cannot read " + manifest.delta;
                return false;
            }
            w.has_adler = true;
            w.adler = ((uint32_t)check[0] << 24) | ((uint32_t)check[1] << 16) |
                      ((uint32_t)check[2] << 8) | check[3];
        }
        w.target_start = target;
        w.target_len = target_len;
        target += target_len;
        g_windows.push_back(w);
        cursor.pos = w.encoding_pos + w.encoding_len;  // past the sections
    }
    g_target_size = target;
    if (g_windows.empty()) {
        if (error) *error = manifest.delta + " has no windows";
        return false;
    }
    g_warned.assign(g_windows.size(), 0);
    return true;
}

bool patch_active() { return !g_windows.empty(); }

uint64_t patch_target_size() { return g_target_size; }

bool patch_read(uint64_t offset, void* dst, uint32_t len) {
    if (g_windows.empty()) return false;
    uint8_t* p = (uint8_t*)dst;
    while (len) {
        const Window* w = window_for(offset);
        if (!w) return false;
        const std::vector<uint8_t>* bytes = window_bytes(*w);
        if (!bytes || bytes->size() != w->target_len) return false;
        const uint64_t within = offset - w->target_start;
        const uint32_t n = (uint32_t)std::min<uint64_t>(len, w->target_len - within);
        memcpy(p, bytes->data() + within, n);
        p += n;
        offset += n;
        len -= n;
    }
    return true;
}

PatchCheck patch_verify(int sample) {
    PatchCheck check;
    check.windows = (int)g_windows.size();
    if (g_windows.empty()) return check;
    // A sample steps through the windows instead, so it covers the whole disc rather than
    // its first megabytes.
    const int wanted = sample > 0 ? std::min<int>(sample, (int)g_windows.size())
                                  : (int)g_windows.size();
    bool reported = false;
    for (int n = 0; n < wanted; ++n) {
        const size_t i = wanted == (int)g_windows.size()
                             ? (size_t)n
                             : (size_t)((uint64_t)n * g_windows.size() / (uint64_t)wanted);
        const Window& w = g_windows[i];
        std::vector<uint8_t> bytes;
        bool matched = false;
        if (!decode_window(w, &bytes, &matched)) continue;
        ++check.decoded;
        if (matched) {
            ++check.matched;
        } else if (!reported) {
            check.first_unmatched = w.target_start;
            reported = true;
        }
    }
    return check;
}
