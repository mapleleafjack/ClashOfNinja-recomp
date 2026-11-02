// Checks the Gecko half of the mod loader: the codetypes that become operations, the
// instruction words handed to recompilation, and the ones that are refused with a reason
// instead of half-applied. Run by ctest; no game and no network.

#include "gecko.h"
#include "util.h"

#include <cstdio>
#include <string>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    std::printf("%s %s\n", condition ? "ok  " : "FAIL", what);
    if (!condition) ++failures;
}

void check_ops(const nla::GeckoProgram& program, const std::string& expected, const char* what) {
    const bool same = program.ops == expected;
    std::printf("%s %s", same ? "ok  " : "FAIL", what);
    if (!same)
        std::printf(" (got \"%s\", wanted \"%s\")", program.ops.c_str(), expected.c_str());
    std::printf("\n");
    if (!same) ++failures;
}

bool has_warning(const nla::GeckoProgram& program, const wchar_t* needle) {
    for (const auto& warning : program.warnings) {
        if (warning.find(needle) != std::wstring::npos) return true;
    }
    return false;
}

// The words a 32-bit write carries, which recompilation applies.
void writes() {
    auto program = nla::parse_gecko(
        L"# Skip Three Intro Videos\n"
        L"$$$$$$$$\n"
        L"Skip Three Intro Videos [Nick]\n"
        L"0400CB14 60000000\n",
        L"codes.txt");
    check_ops(program, "write32 8000CB14 60000000 1 -\n",
              "a 32-bit write becomes a write of a word");
    check(program.words.size() == 1 && program.words[0].first == 0x8000CB14u &&
              program.words[0].second == 0x60000000u,
          "its word is handed to recompilation");
    check(program.warnings.empty(), "a code with nothing unusual says nothing");
    check(program.writes == 1 && program.conditions == 0, "the write is counted");

    // A halfword write's repeat is how many halfwords it fills, so unlocking a roster of
    // 24 characters is one code, not 24.
    auto roster = nla::parse_gecko(L"02223258 0017FFFF\n", L"codes.txt");
    check_ops(roster, "write16 80223258 FFFF 24 -\n",
              "a 16-bit write's repeat becomes a count");
    check(roster.words.empty(), "a halfword write is not an instruction");

    auto byte = nla::parse_gecko(L"00001000 00000005\n", L"codes.txt");
    check_ops(byte, "write8 80001000 05 1 -\n", "an 8-bit write keeps its byte");

    auto pointer = nla::parse_gecko(L"14001000 00000001\n", L"codes.txt");
    check_ops(pointer, "write32 80001000 00000001 1 p\n",
              "the pointer flag is kept, and not resolved at build time");
    check(pointer.words.empty(),
          "a write through the pointer address is not an instruction word");

    // An address a write cannot be made at is refused, not rounded.
    auto misaligned = nla::parse_gecko(L"04001002 00000001\n", L"codes.txt");
    check(misaligned.ops.empty() && misaligned.words.empty(),
          "a word write off a word boundary does nothing");
    check(has_warning(misaligned, L"not aligned"), "and says why");
}

void strings_and_conditions() {
    // 12 bytes: "Hello, world".
    auto string = nla::parse_gecko(
        L"06001000 0000000C\n48656C6C 6F2C2077\n6F726C64 00000000\n", L"codes.txt");
    check_ops(string, "string 80001000 12 - 48656C6C6F2C20776F726C64\n",
              "a string write carries its bytes");
    check(string.writes == 1, "and counts as a write");

    auto guarded = nla::parse_gecko(
        L"28223258 FFFF0001\n"
        L"04223260 00000001\n"
        L"E2000001 00000000\n",
        L"codes.txt");
    check_ops(guarded,
              "if eq 16 80223258 0001 FFFF -\n"
              "write32 80223260 00000001 1 -\n"
              "endif\n",
              "a halfword condition keeps its mask, and its block closes");
    check(guarded.words.empty(),
          "a write a condition guards is not handed to recompilation");
    check(guarded.conditions == 1, "the condition is counted");

    auto elif = nla::parse_gecko(
        L"20001000 00000001\n"
        L"04001010 00000002\n"
        L"F0000000 00000000\n"
        L"042232FF 00000003\n",  // the old way of saying "else": a flag on the endif
        L"codes.txt");
    check(elif.ops.find("if eq 32 80001000 00000001 - -\n") == 0,
              "a word condition compares the word");

    auto with_else = nla::parse_gecko(
        L"20001000 00000001\n04001010 00000002\nE2100001 00000000\n", L"codes.txt");
    check_ops(with_else,
              "if eq 32 80001000 00000001 - -\n"
              "write32 80001010 00000002 1 -\n"
              "else\n"
              "endif\n",
              "an endif with the else bit inverses the block");

    // Two conditions close in one line, which editors write for nested blocks.
    auto nested = nla::parse_gecko(
        L"20001000 00000001\n28001004 00000002\n04001010 00000003\n"
        L"E2000002 00000000\n",
        L"codes.txt");
    check_ops(nested,
              "if eq 32 80001000 00000001 - -\n"
              "if eq 16 80001004 0002 0000 -\n"
              "write32 80001010 00000003 1 -\n"
              "endif\n"
              "endif\n",
              "an endif can close more than one block");
}

void base_and_pointer_addresses() {
    // The operand of these is in the second word: an address to read, or a value to set.
    auto base = nla::parse_gecko(
        L"42000000 8023E000\n"
        L"04000010 00000001\n"
        L"E0000000 00000000\n",
        L"codes.txt");
    check_ops(base,
              "baseset set 8023E000\n"
              "write32 80000010 00000001 1 -\n"
              "end\n",
              "a base address is set, and end clears it");

    auto load = nla::parse_gecko(L"40000000 8023E000\n", L"codes.txt");
    check_ops(load, "baseload set 8023E000\n", "a base address is loaded from memory");

    auto ptr = nla::parse_gecko(
        L"4A000000 81234567\n"
        L"48000000 8023E000\n"
        L"4C000000 8023E000\n",
        L"codes.txt");
    check_ops(ptr,
              "ptrset set 81234567\n"
              "ptrload set 8023E000\n"
              "ptrstore set 8023E000\n",
              "the pointer address has its own load, set and store");

    // The flags in the first word: add rather than set, and offset by the register.
    auto flags = nla::parse_gecko(
        L"42100000 00000004\n"
        L"42010000 00000008\n"
        L"42110000 0000000C\n",
        L"codes.txt");
    check_ops(flags,
              "baseset add 00000004\n"
              "baseset setoff 00000008\n"
              "baseset addoff 0000000C\n",
              "a base operation says how it adds and whether it is offset");

    auto register_flag = nla::parse_gecko(L"42000001 8023E000\n", L"codes.txt");
    check(register_flag.ops.empty() && has_warning(register_flag, L"gecko register"),
          "a base address through a gecko register is refused, and said so");
}

void refusals() {
    // Inserted assembly needs a PowerPC core: the lines that belong to the code are
    // consumed with it, so the next code is still read.
    auto assembly = nla::parse_gecko(
        L"C2042C40 00000002\n"
        L"54A007BD 41820008\n"
        L"60000000 00000000\n"
        L"0400CB14 60000000\n",
        L"codes.txt");
    check(assembly.ops == "write32 8000CB14 60000000 1 -\n",
          "an inserted assembly block is skipped whole, and the next code read");
    check(has_warning(assembly, L"needs a PowerPC core"), "and says what it needs");
    check(assembly.words.size() == 1 && assembly.words[0].first == 0x8000CB14u,
          "the code after it is still recompiled in");

    auto unknown = nla::parse_gecko(L"CC000000 00000000\n", L"codes.txt");
    check(unknown.ops.empty() && has_warning(unknown, L"codetype 0xCC"),
          "a codetype with no operation says which one it was");

    auto broken = nla::parse_gecko(L"0400CB14\n", L"codes.txt");
    check(broken.ops.empty() && has_warning(broken, L"no value"),
          "an address with no value is reported");

    // Controls: the end of a code keeps the next mod's operations running, exit stops.
    auto controls = nla::parse_gecko(
        L"E0000000 00000000\nF0000000 00000000\n", L"codes.txt");
    check_ops(controls, "end\nstop\n", "the terminator and the exit are kept apart");
}

// A list an editor exported: each code named with `$`, separated by a line of `$`s. The
// Geckos panel switches these on one at a time, so the split has to be exact.
void named_codes() {
    auto codes = nla::parse_named_codes(
        L"$Infinite Time\n"
        L"040374B8 60000000\n"
        L"$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$\n"
        L"$Skip Intros\n"
        L"# the three videos\n"
        L"0400CB14 60000000\n"
        L"0400CB28 60000000\n"
        L"$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$\n",
        L"Code");
    check(codes.size() == 2, "each named code is its own entry");
    check(codes[0].name == L"Infinite Time", "the first keeps its name");
    check(codes[1].name == L"Skip Intros", "so does the second");
    check(codes[0].text.find(L"040374B8") != std::wstring::npos &&
              codes[0].text.find(L"0400CB14") == std::wstring::npos,
          "a code's words stay with it");
    check(codes[1].text.find(L"# the three videos") != std::wstring::npos,
          "a note under a code stays with it");

    // A block pasted without names still becomes something to switch on, and a repeat of
    // one name is numbered so two codes cannot share a check.
    auto bare = nla::parse_named_codes(L"040374B8 60000000\n", L"Pasted code");
    check(bare.size() == 1 && bare[0].name == L"Pasted code",
          "a block with no name takes the fallback");

    auto repeated = nla::parse_named_codes(
        L"$Skip Intros\n0400CB14 60000000\n$$$$$$$$\n$Skip Intros\n0400CB28 60000000\n",
        L"Code");
    check(repeated.size() == 2 && repeated[1].name == L"Skip Intros (2)",
          "a second code of the same name is numbered");

    auto empty = nla::parse_named_codes(L"$Nothing Here\n$$$$$$$$\n", L"Code");
    check(empty.empty(), "a name with no words under it is not a code");
}

int run() {
    writes();
    strings_and_conditions();
    base_and_pointer_addresses();
    refusals();
    named_codes();
    std::printf("%s\n", failures ? "some checks failed" : "all checks passed");
    return failures ? 1 : 0;
}

}  // namespace

// MinGW links with -municode on Windows, which wants wmain.
#if defined(_WIN32)
int wmain() { return run(); }
#else
int main() { return run(); }
#endif
