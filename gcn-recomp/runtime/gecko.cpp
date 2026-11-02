// The mods' Gecko codes: see gecko.h, and launcher/src/gecko.cpp for the operations the
// launcher writes into mods.gecko.
//
// Nothing here knows about codetypes. The launcher resolves those once, when a mod is
// switched on, so a frame's work is a walk over operations: compare, write, move the base
// or pointer address. A console's code handler runs every code once a frame, and so does
// this; a write therefore lands again each frame, which is what the codes expect.
#include "gecko.h"

#include "runtime.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

enum class Kind {
    Write8,
    Write16,
    Write32,
    String,
    If,
    Else,
    Endif,
    BaseLoad,
    BaseSet,
    BaseStore,
    PtrLoad,
    PtrSet,
    PtrStore,
    Repeat,
    EndRepeat,
    End,
    Stop,
};

struct Op {
    Kind kind = Kind::End;
    bool pointer = false;  // add the pointer address to this write's or compare's address
    bool wide = true;      // a compare of 32 bits rather than 16
    int compare = 0;       // 0 equal, 1 not equal, 2 greater, 3 less
    int mode = 0;          // base/pointer operations: 0 set, 1 add, 2 set offset, 3 add offset
    int reg = 0;           // the repeat register
    uint32_t address = 0;
    uint32_t value = 0;
    uint32_t mask = 0;
    uint32_t count = 1;
    std::vector<uint8_t> bytes;
};

std::vector<Op> g_ops;
bool g_loaded;
bool g_complained;

// Where a write or compare lands. Gecko's base address is always added -- a code that never
// touches it leaves it 0 -- and a write whose codetype asked for the pointer address adds
// that instead.
uint32_t g_base;
uint32_t g_pointer;

struct Level {
    bool parent = true;  // the status outside this block, restored by endif
    bool condition = true;
};
std::vector<Level> g_levels;
bool g_enabled = true;

struct Loop {
    int reg = 0;
    uint32_t left = 0;
    size_t start = 0;
};
std::vector<Loop> g_loops;

// Only main RAM is written: a code that chases a pointer into whatever it finds must not
// wander into the address space this process mapped for other purposes.
bool ram_ok(uint32_t address) { return (address & 0x3FFFFFFFu) < 0x01800000u; }

void complain(const char* what, uint32_t address) {
    if (g_complained) return;
    g_complained = true;
    LOG(LOG_DVD, "gecko: %s at %08X is not in RAM; the write is skipped", what, address);
}

uint32_t& own_register(const Op& op) {
    const bool pointer = op.kind == Kind::PtrLoad || op.kind == Kind::PtrSet ||
                         op.kind == Kind::PtrStore;
    return pointer ? g_pointer : g_base;
}

bool condition_holds(const Op& op) {
    const uint32_t address = op.address + (op.pointer ? g_pointer : g_base);
    if (!ram_ok(address)) return false;
    if (op.wide) {
        const uint32_t actual = LD32(address);
        switch (op.compare) {
        case 0: return actual == op.value;
        case 1: return actual != op.value;
        case 2: return actual > op.value;
        default: return actual < op.value;
        }
    }
    // A halfword compare masks the bits it was told to ignore.
    const uint32_t actual = LD16(address) & ~op.mask;
    const uint32_t wanted = op.value & ~op.mask;
    switch (op.compare) {
    case 0: return actual == wanted;
    case 1: return actual != wanted;
    case 2: return actual > wanted;
    default: return actual < wanted;
    }
}

void run_write(const Op& op, uint32_t address) {
    const uint32_t step = op.kind == Kind::Write8 ? 1 : op.kind == Kind::Write16 ? 2 : 4;
    for (uint32_t k = 0; k < op.count; ++k) {
        const uint32_t at = address + k * step;
        if (!ram_ok(at)) {
            complain("a write", at);
            return;
        }
        if (op.kind == Kind::Write8) ST8(at, op.value);
        else if (op.kind == Kind::Write16) ST16(at, op.value);
        else ST32(at, op.value);
    }
}

void run_op(const Op& op, uint32_t address) {
    switch (op.kind) {
    case Kind::Write8:
    case Kind::Write16:
    case Kind::Write32:
        run_write(op, address);
        return;
    case Kind::String:
        // A string is a run of bytes, not values to swap.
        for (size_t k = 0; k < op.bytes.size(); ++k) {
            const uint32_t at = address + static_cast<uint32_t>(k);
            if (!ram_ok(at)) {
                complain("a string write", at);
                return;
            }
            ST8(at, op.bytes[k]);
        }
        return;
    case Kind::BaseLoad:
    case Kind::PtrLoad: {
        uint32_t& reg = own_register(op);
        const uint32_t at = op.address + (op.mode >= 2 ? reg : 0u);
        if (!ram_ok(at)) {
            complain("a base address read", at);
            return;
        }
        const uint32_t loaded = LD32(at);
        reg = (op.mode & 1) ? reg + loaded : loaded;
        return;
    }
    case Kind::BaseSet:
    case Kind::PtrSet: {
        uint32_t& reg = own_register(op);
        const uint32_t value = op.mode >= 2 ? reg + op.address : op.address;
        reg = (op.mode & 1) ? reg + value : value;
        return;
    }
    case Kind::BaseStore:
    case Kind::PtrStore: {
        uint32_t& reg = own_register(op);
        const uint32_t at = op.address + (op.mode >= 2 ? reg : 0u);
        if (!ram_ok(at)) {
            complain("a base address write", at);
            return;
        }
        ST32(at, reg);
        return;
    }
    default:
        return;
    }
}

void run_frame() {
    g_base = 0;
    g_pointer = 0;
    g_levels.clear();
    g_loops.clear();
    g_enabled = true;
    // A malformed program must not hold a frame: a runaway repeat is stopped, once, and
    // said so.
    constexpr size_t kMaxSteps = 1u << 20;
    size_t steps = 0;
    for (size_t pc = 0; pc < g_ops.size(); ++pc) {
        if (++steps > kMaxSteps) {
            if (!g_complained) {
                g_complained = true;
                LOG(LOG_DVD, "gecko: the program does not finish; the rest is skipped");
            }
            return;
        }
        const Op& op = g_ops[pc];
        switch (op.kind) {
        case Kind::If: {
            const bool holds = g_enabled && condition_holds(op);
            g_levels.push_back(Level{g_enabled, holds});
            g_enabled = holds;
            break;
        }
        case Kind::Else:
            if (!g_levels.empty()) {
                const Level& level = g_levels.back();
                g_enabled = level.parent && !level.condition;
            }
            break;
        case Kind::Endif:
            if (!g_levels.empty()) {
                g_enabled = g_levels.back().parent;
                g_levels.pop_back();
            }
            break;
        case Kind::Repeat:
            g_loops.push_back(Loop{op.reg, op.count, pc + 1});
            break;
        case Kind::EndRepeat: {
            for (size_t k = g_loops.size(); k-- > 0;) {
                if (g_loops[k].reg != op.reg) continue;
                if (g_loops[k].left > 1) {
                    --g_loops[k].left;
                    pc = g_loops[k].start - 1;  // the loop's ++ lands on the block again
                } else {
                    g_loops.erase(g_loops.begin() + static_cast<long>(k));
                }
                break;
            }
            break;
        }
        case Kind::End:
            // The end of one code: its state goes, the next mod's operations still run.
            g_base = 0;
            g_pointer = 0;
            g_levels.clear();
            g_loops.clear();
            g_enabled = true;
            break;
        case Kind::Stop:
            return;
        default:
            if (g_enabled) run_op(op, op.address + (op.pointer ? g_pointer : g_base));
            break;
        }
    }
}

bool equals_ci(const std::string& a, const char* b) {
    size_t i = 0;
    for (; i < a.size() && b[i]; ++i) {
        const char x = a[i] >= 'A' && a[i] <= 'Z' ? static_cast<char>(a[i] + 32) : a[i];
        const char y = b[i] >= 'A' && b[i] <= 'Z' ? static_cast<char>(b[i] + 32) : b[i];
        if (x != y) return false;
    }
    return i == a.size() && !b[i];
}

bool parse_hex(const std::string& text, uint32_t* out) {
    if (text.empty()) return false;
    char* end = nullptr;
    const unsigned long value = std::strtoul(text.c_str(), &end, 16);
    if (!end || *end) return false;
    *out = static_cast<uint32_t>(value);
    return true;
}

bool parse_number(const std::string& text, int* out) {
    if (text.empty()) return false;
    char* end = nullptr;
    const long value = std::strtol(text.c_str(), &end, 10);
    if (!end || *end) return false;
    *out = static_cast<int>(value);
    return true;
}

int parse_mode(const std::string& text) {
    if (equals_ci(text, "set")) return 0;
    if (equals_ci(text, "add")) return 1;
    if (equals_ci(text, "setoff")) return 2;
    if (equals_ci(text, "addoff")) return 3;
    return -1;
}

bool parse_line(const std::string& line, Op* op) {
    std::vector<std::string> w;
    for (size_t i = 0; i < line.size();) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
        const size_t start = i;
        while (i < line.size() && line[i] != ' ' && line[i] != '\t') ++i;
        if (i > start) w.push_back(line.substr(start, i - start));
    }
    if (w.empty()) return false;
    const std::string& name = w[0];
    auto flag = [&w](size_t index) { return index < w.size() && w[index] == "p"; };

    if (name == "write8" || name == "write16" || name == "write32") {
        if (w.size() < 4) return false;
        op->kind = name == "write8" ? Kind::Write8
                   : name == "write16" ? Kind::Write16
                                       : Kind::Write32;
        int count = 1;
        if (!parse_hex(w[1], &op->address) || !parse_hex(w[2], &op->value) ||
            !parse_number(w[3], &count))
            return false;
        op->count = static_cast<uint32_t>(std::max(count, 0));
        op->pointer = flag(4);
        return true;
    }
    if (name == "string") {
        if (w.size() < 4) return false;
        op->kind = Kind::String;
        int length = 0;
        if (!parse_hex(w[1], &op->address) || !parse_number(w[2], &length)) return false;
        op->pointer = flag(3);
        const std::string& data = w.size() > 4 ? w[4] : std::string();
        for (size_t i = 0; i + 1 < data.size(); i += 2) {
            const char pair[3] = {data[i], data[i + 1], 0};
            op->bytes.push_back(static_cast<uint8_t>(std::strtoul(pair, nullptr, 16)));
        }
        if (static_cast<int>(op->bytes.size()) > length)
            op->bytes.resize(static_cast<size_t>(std::max(length, 0)));
        return true;
    }
    if (name == "if") {
        if (w.size() < 7) return false;
        op->kind = Kind::If;
        op->compare = equals_ci(w[1], "eq")   ? 0
                      : equals_ci(w[1], "ne") ? 1
                      : equals_ci(w[1], "gt") ? 2
                                              : 3;
        op->wide = w[2] != "16";
        if (!parse_hex(w[3], &op->address) || !parse_hex(w[4], &op->value)) return false;
        op->mask = 0xFFFF;
        if (w[5] != "-" && !parse_hex(w[5], &op->mask)) return false;
        op->pointer = flag(6);
        return true;
    }
    if (name == "else") {
        op->kind = Kind::Else;
        return true;
    }
    if (name == "endif") {
        op->kind = Kind::Endif;
        return true;
    }
    if (name == "end") {
        op->kind = Kind::End;
        return true;
    }
    if (name == "stop") {
        op->kind = Kind::Stop;
        return true;
    }
    if (name == "repeat") {
        int reg = 0, count = 0;
        if (w.size() < 3 || !parse_number(w[1], &reg) || !parse_number(w[2], &count))
            return false;
        op->kind = Kind::Repeat;
        op->reg = reg;
        op->count = static_cast<uint32_t>(std::max(count, 0));
        return true;
    }
    if (name == "endrepeat") {
        int reg = 0;
        if (w.size() > 1) parse_number(w[1], &reg);
        op->kind = Kind::EndRepeat;
        op->reg = reg;
        return true;
    }
    struct Named {
        const char* name;
        Kind kind;
    };
    static const Named kNamed[] = {
        {"baseload", Kind::BaseLoad},   {"baseset", Kind::BaseSet},
        {"basestore", Kind::BaseStore}, {"ptrload", Kind::PtrLoad},
        {"ptrset", Kind::PtrSet},       {"ptrstore", Kind::PtrStore},
    };
    for (const auto& entry : kNamed) {
        if (name != entry.name) continue;
        if (w.size() < 3) return false;
        op->kind = entry.kind;
        op->mode = parse_mode(w[1]);
        if (op->mode < 0) return false;
        return parse_hex(w[2], &op->address);
    }
    return false;
}

}  // namespace

void gecko_init(const char* disc_dir) {
    g_loaded = true;
    if (!disc_dir || !*disc_dir) return;
    const std::string path = std::string(disc_dir) + "/mods.gecko";
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return;  // no mod with codes: the usual case
    std::string line;
    size_t line_number = 0, skipped = 0;
    char buffer[4096];
    while (fgets(buffer, sizeof buffer, f)) {
        ++line_number;
        line.assign(buffer);
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
        const size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos || line[start] == '#') continue;
        Op op;
        if (!parse_line(line.substr(start), &op)) {
            ++skipped;
            LOG(LOG_DVD, "gecko: %s:%zu is not an operation: %s", path.c_str(), line_number,
                line.substr(start).c_str());
            continue;
        }
        g_ops.push_back(std::move(op));
    }
    fclose(f);
    LOG(LOG_DVD, "gecko: %zu operations from %s%s", g_ops.size(), path.c_str(),
        skipped ? " (some skipped)" : "");
}

void gecko_frame() {
    if (!g_loaded || g_ops.empty()) return;
    run_frame();
}
