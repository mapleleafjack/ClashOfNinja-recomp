// Gecko codes: what they say, and what this recompiler can do with them.
//
// A Gecko code is a list of 8-byte pairs, `XXXXXXXX YYYYYYYY`. The first byte of the
// first word is the codetype -- 0x04 writes a word, 0x02 a halfword, 0x20 compares a
// word, 0xC2 inserts PowerPC instructions -- and the rest of that word is an address
// offset from 0x80000000, or the codetype's own operand. Bit 4 of the codetype (0x10)
// asks for the pointer address to be added to a write's or compare's address. The second
// word is the value.
//
// The runtime's Gecko engine (gcn-recomp/runtime/gecko.cpp) executes the program this
// file writes, one line per operation:
//
//   write8  <addr> <value> <count> <-|p>
//   write16 <addr> <value> <count> <-|p>
//   write32 <addr> <value> <count> <-|p>
//   string  <addr> <length> <-|p> <hex>
//   if      <eq|ne|gt|lt> <16|32> <addr> <value> <mask|-> <-|p>
//   else
//   endif
//   baseload|baseset|basestore <set|add|setoff|addoff> <addr>
//   ptrload|ptrset|ptrstore     <set|add|setoff|addoff> <addr>
//   repeat <b> <count>
//   endrepeat <b>
//   end
//   stop
//
// `p` adds the pointer address to <addr>; `-` adds nothing (the base address is always
// added, as it is on a console). A count writes that many values consecutively, which is
// what a write's repeat field means. `end` clears the conditions and the base and pointer
// addresses, so each mod's list runs on its own even though every mod's operations are in
// one file; `stop` ends the frame's run.
//
// The codetypes not turned into operations are the ones that need a PowerPC core to
// execute: inserted assembly, switches, branches inside the code list, and the codes that
// keep their own registers. They are reported instead of half-applied.

#include "gecko.h"

#include "util.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cwchar>

namespace nla {
namespace {

void add(std::string* out, const char* fmt, ...) {
    char text[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(text, sizeof text, fmt, args);
    va_end(args);
    out->append(text);
    out->push_back('\n');
}

std::string hex_bytes(const std::string& bytes) {
    static const char* digits = "0123456789ABCDEF";
    std::string text;
    text.reserve(bytes.size() * 2);
    for (unsigned char byte : bytes) {
        text.push_back(digits[byte >> 4]);
        text.push_back(digits[byte & 0xF]);
    }
    return text;
}

// The codetypes, as the community's editors number them.
const wchar_t* codetype_name(unsigned type) {
    switch (type) {
    case 0x00: return L"8-bit write";
    case 0x02: return L"16-bit write";
    case 0x04: return L"32-bit write";
    case 0x06: return L"string write";
    case 0x08: return L"serial write";
    case 0x20: return L"if equal (32-bit)";
    case 0x22: return L"if not equal (32-bit)";
    case 0x24: return L"if greater (32-bit)";
    case 0x26: return L"if less (32-bit)";
    case 0x28: return L"if equal (16-bit)";
    case 0x2A: return L"if not equal (16-bit)";
    case 0x2C: return L"if greater (16-bit)";
    case 0x2E: return L"if less (16-bit)";
    case 0x40: return L"base address load";
    case 0x42: return L"base address set";
    case 0x44: return L"base address store";
    case 0x46: return L"base address of next code";
    case 0x48: return L"pointer address load";
    case 0x4A: return L"pointer address set";
    case 0x4C: return L"pointer address store";
    case 0x4E: return L"pointer address of next code";
    case 0x60: return L"repeat set";
    case 0x62: return L"repeat execute";
    case 0x64: return L"return";
    case 0x66: return L"goto";
    case 0x68: return L"gosub";
    case 0x80: return L"gecko register set";
    case 0x82: return L"gecko register load";
    case 0x84: return L"gecko register store";
    case 0x86: return L"gecko register operate (immediate)";
    case 0x88: return L"gecko register operate";
    case 0x8A: return L"memory copy";
    case 0x8C: return L"memory copy (pointer)";
    case 0xA0: return L"gecko if equal";
    case 0xA2: return L"gecko if not equal";
    case 0xA4: return L"gecko if greater";
    case 0xA6: return L"gecko if less";
    case 0xA8: return L"counter if equal";
    case 0xAA: return L"counter if not equal";
    case 0xAC: return L"counter if greater";
    case 0xAE: return L"counter if less";
    case 0xC0: return L"insert assembly";
    case 0xC2: return L"insert assembly";
    case 0xC4: return L"insert assembly and link";
    case 0xC6: return L"write branch";
    case 0xCC: return L"switch";
    case 0xCE: return L"address range check";
    case 0xE0: return L"terminator";
    case 0xE2: return L"endif";
    case 0xF0: return L"exit";
    case 0xF2: return L"insert assembly (exclusive or)";
    case 0xF6: return L"brainslug search";
    default: return L"unknown";
    }
}

// Codetypes that carry lines of PowerPC instructions after them, where the second word is
// how many.
bool is_assembly(unsigned type) {
    return type == 0xC0 || type == 0xC2 || type == 0xC4 || type == 0xF2;
}

struct Line {
    uint32_t meta = 0;
    uint32_t value = 0;
};

bool parse_word(const std::wstring& text, uint32_t* out) {
    if (text.empty()) return false;
    wchar_t* end = nullptr;
    const unsigned long long parsed = std::wcstoull(text.c_str(), &end, 16);
    if (!end || *end != L'\0' || parsed > 0xFFFFFFFFull) return false;
    *out = static_cast<uint32_t>(parsed);
    return true;
}

}  // namespace

GeckoProgram parse_gecko(const std::wstring& text, const std::wstring& source) {
    GeckoProgram program;
    std::vector<Line> lines;
    std::size_t line_number = 0;
    for (const auto& raw : split(text, L'\n')) {
        ++line_number;
        const auto line = trim(raw);
        if (line.empty()) continue;
        const wchar_t first = line.front();
        if (first == L'#' || first == L'*' || first == L'$' || first == L'"') continue;
        std::vector<std::wstring> parts;
        for (auto& word : split(line, L' ')) {
            if (!word.empty()) parts.push_back(word);
        }
        if (parts.size() < 2) {
            if (parts.size() == 1 &&
                parts[0].find_first_not_of(L"0123456789abcdefABCDEF") == std::wstring::npos)
                program.warnings.push_back(
                    format(L"%ls: line %zu has an address with no value: %ls", source.c_str(),
                           line_number, line.c_str()));
            continue;
        }
        Line code;
        const bool address_ok = parse_word(parts[0], &code.meta);
        const bool value_ok = address_ok && parse_word(parts[1], &code.value);
        if (!address_ok) {
            // An editor's export names its codes on their own lines, and a note may sit
            // beside one: a line that is not hex is not a code, and not an error.
            continue;
        }
        if (!value_ok) {
            program.warnings.push_back(format(L"%ls: line %zu has an address with no value: %ls",
                                              source.c_str(), line_number, line.c_str()));
            continue;
        }
        lines.push_back(code);
    }

    // How many conditions are open: a word written under one is not handed to
    // recompilation, since whether it should apply is only known while the game runs.
    std::size_t depth = 0;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const uint32_t meta = lines[i].meta;
        const uint32_t value = lines[i].value;
        const unsigned raw = (meta >> 24) & 0xFF;
        // Bit 4 of the codetype asks for the pointer address on a write or a compare, and
        // is part of the codetype itself for the rest.
        const unsigned type = raw & 0xEFu;
        const uint32_t offset = meta & 0x1FFFFFFu;
        const uint32_t address = 0x80000000u | offset;
        const char* where = (raw & 0x10) ? "p" : "-";

        if (type == 0x00 || type == 0x02 || type == 0x04 || type == 0x06) {
            const uint32_t size = type == 0x00 ? 1 : type == 0x02 ? 2 : 4;
            if (type != 0x06 && (offset & (size - 1))) {
                program.warnings.push_back(
                    format(L"%ls: a %u-byte write at 0x%08X is not aligned; the code is "
                           L"skipped",
                           source.c_str(), size, address));
                continue;
            }
            if (type == 0x04) {
                add(&program.ops, "write32 %08X %08X 1 %s", address, value, where);
                if (!(raw & 0x10) && depth == 0)
                    program.words.emplace_back(address, value);
            } else if (type == 0x06) {
                // The second word is the length in bytes; its data follows as words, in
                // whole 8-byte lines the way an editor writes it.
                const uint32_t length = value;
                const uint32_t wanted = ((length + 7) / 8) * 2;
                std::string bytes;
                uint32_t read = 0;
                while (read < wanted && i + 1 < lines.size()) {
                    for (uint32_t word : {lines[++i].meta, lines[i].value}) {
                        if (read >= wanted) break;
                        bytes.push_back(static_cast<char>(word >> 24));
                        bytes.push_back(static_cast<char>(word >> 16));
                        bytes.push_back(static_cast<char>(word >> 8));
                        bytes.push_back(static_cast<char>(word));
                        ++read;
                    }
                }
                if (read < wanted)
                    program.warnings.push_back(
                        format(L"%ls: a string write at 0x%08X says %u bytes but the code "
                               L"ends before them",
                               source.c_str(), address, length));
                if (bytes.size() > length) bytes.resize(length);
                add(&program.ops, "string %08X %zu %s %s", address, bytes.size(), where,
                    hex_bytes(bytes).c_str());
            } else {
                const uint32_t count = ((value >> 16) & 0xFFF) + 1;
                if (type == 0x00)
                    add(&program.ops, "write8 %08X %02X %u %s", address, value & 0xFF, count,
                        where);
                else
                    add(&program.ops, "write16 %08X %04X %u %s", address, value & 0xFFFF,
                        count, where);
            }
            ++program.writes;
            continue;
        }
        if (type >= 0x20 && type <= 0x2E) {
            // 0x20-0x26 compare a word, 0x28-0x2E a halfword through a mask of the bits
            // to ignore. Bit 0 of the first word says the block is closed by the next
            // endif or terminator, which is how it closes here either way.
            const bool wide = type <= 0x26;
            const char* compare = (type & 0x06) == 0x00   ? "eq"
                                  : (type & 0x06) == 0x02 ? "ne"
                                  : (type & 0x06) == 0x04 ? "gt"
                                                          : "lt";
            char mask[16] = "-";
            if (wide) {
                add(&program.ops, "if %s 32 %08X %08X %s %s", compare, address & ~3u, value,
                    mask, where);
            } else {
                std::snprintf(mask, sizeof mask, "%04X", value >> 16);
                add(&program.ops, "if %s 16 %08X %04X %s %s", compare, address & ~1u,
                    value & 0xFFFF, mask, where);
            }
            ++program.conditions;
            ++depth;
            continue;
        }
        if (raw >= 0x40 && raw <= 0x4E && !(raw & 1)) {
            // These keep their operand in the second word -- an address to read, a value
            // to set -- and use the first for the flags: bit 16 offsets the address by the
            // register the operation acts on, bit 20 adds instead of setting, and the low
            // nibble names a gecko register.
            const bool pointer = raw >= 0x48;
            const bool offset_by_register = (meta >> 16) & 1;
            const bool adds = (meta >> 20) & 1;
            const unsigned register_number = meta & 0xF;
            const unsigned operation = raw & 0x06;  // 0 load, 2 set, 4 store, 6 next code
            if (register_number) {
                program.warnings.push_back(
                    format(L"%ls: %ls (0x%08X) uses a gecko register, which is not "
                           L"supported; the code is skipped",
                           source.c_str(), codetype_name(raw), meta));
            } else if (operation == 0x6) {
                program.warnings.push_back(
                    format(L"%ls: %ls (0x%08X) is not supported; the code is skipped",
                           source.c_str(), codetype_name(raw), meta));
            } else {
                const char* verb = operation == 0x0   ? "load"
                                   : operation == 0x2 ? "set"
                                                      : "store";
                const char* mode = adds ? (offset_by_register ? "addoff" : "add")
                                   : offset_by_register ? "setoff"
                                                        : "set";
                add(&program.ops, "%s%s %s %08X", pointer ? "ptr" : "base", verb, mode,
                    value);
            }
            continue;
        }
        if (type == 0x60) {
            // The count is in the first word's low half; the second word names the
            // register the loop keeps its place in.
            add(&program.ops, "repeat %u %u", value & 0xF, meta & 0xFFFF);
            continue;
        }
        if (type == 0x62) {
            add(&program.ops, "endrepeat %u", value & 0xF);
            continue;
        }
        if (raw == 0xE0) {
            // The end of a code: its conditions close and its base and pointer addresses
            // go, but the next mod's operations still run.
            if (value)
                program.warnings.push_back(
                    format(L"%ls: a terminator (0x%08X) names a base or pointer address, "
                           L"which is not applied",
                           source.c_str(), meta));
            add(&program.ops, "end");
            depth = 0;
            continue;
        }
        if (type == 0xE2) {
            if (meta & 0x100000u) add(&program.ops, "else");
            for (unsigned k = 0; k < std::max(1u, meta & 0xF); ++k) {
                add(&program.ops, "endif");
                if (depth) --depth;
            }
            continue;
        }
        if (raw == 0xF0) {
            add(&program.ops, "stop");
            continue;
        }
        if (is_assembly(raw)) {
            // The second word is how many lines of PowerPC instructions follow.
            program.warnings.push_back(
                format(L"%ls: %ls (0x%08X) needs a PowerPC core to run, which a recompiled "
                       L"game does not have at run time; the code is skipped",
                       source.c_str(), codetype_name(raw), meta));
            i += std::min<std::size_t>(value, lines.size() - i - 1);
            continue;
        }
        program.warnings.push_back(
            format(L"%ls: codetype 0x%02X (%ls) at 0x%08X is not supported; the code is "
                   L"skipped",
                   source.c_str(), raw, codetype_name(raw), meta));
    }
    return program;
}

std::vector<NamedCode> parse_named_codes(const std::wstring& text,
                                         const std::wstring& fallback) {
    std::vector<NamedCode> codes;
    NamedCode current;
    bool open = false;

    // A block is a code only when something in it is a word: a `$Name` with nothing under
    // it, or notes alone, is not one a player could switch on.
    auto flush = [&codes, &current, &open, &fallback] {
        if (!open) return;
        open = false;
        while (!current.text.empty() &&
               (current.text.back() == L'\n' || current.text.back() == L'\r'))
            current.text.pop_back();
        bool has_word = false;
        for (const auto& line : split(current.text, L'\n')) {
            const auto trimmed = trim(line);
            if (!trimmed.empty() && trimmed.front() != L'#' && trimmed.front() != L'*' &&
                trimmed.front() != L'"') {
                has_word = true;
                break;
            }
        }
        if (!has_word) {
            current = NamedCode{};
            return;
        }
        std::wstring name = trim(current.name);
        if (name.empty()) name = fallback.empty() ? L"Code" : fallback;
        // Two codes with one name would be one check in the panel and one entry in
        // settings, so a repeat is numbered the way an editor's own list does it.
        std::wstring unique = name;
        for (int n = 2;; ++n) {
            bool taken = false;
            for (const auto& code : codes) {
                if (code.name == unique) {
                    taken = true;
                    break;
                }
            }
            if (!taken) break;
            unique = format(L"%ls (%d)", name.c_str(), n);
        }
        current.name = unique;
        codes.push_back(current);
        current = NamedCode{};
    };

    for (const auto& raw : split(text, L'\n')) {
        if (!raw.empty() && raw.front() == L'$') {
            const auto line = trim(raw);
            bool separator = !line.empty();
            for (wchar_t c : line) {
                if (c != L'$') {
                    separator = false;
                    break;
                }
            }
            flush();
            if (separator) continue;
            current.name = line.substr(1);
            open = true;
            continue;
        }
        if (!open) {
            // A note before any name belongs to nothing, so it starts no code.
            const auto line = trim(raw);
            if (line.empty() || line.front() == L'#' || line.front() == L'*') continue;
            open = true;
        }
        current.text += raw;
        current.text += L'\n';
    }
    flush();
    return codes;
}

}  // namespace nla
