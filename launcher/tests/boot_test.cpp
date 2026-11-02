// Checks which disc a boot accepts. A patch is a whole disc of its own, and a hack may
// rename the ID it carries (SCON4 is SG4JDA over a G4NJDA disc), so while one is on the
// check is made against the tree the patch decodes from -- and a tree of another game is
// still refused. Header-only logic, so it needs no runtime and no disc.

#include "boot.h"

#include <cstdio>
#include <cstring>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    std::printf("%s %s\n", condition ? "ok  " : "FAIL", what);
    if (!condition) ++failures;
}

// A disc header is its six-byte game code at offset 0; the rest of it is padding here.
struct Header {
    uint8_t bytes[0x440];
    explicit Header(const char* id) {
        std::memset(bytes, 0, sizeof(bytes));
        std::memcpy(bytes, id, 6);
    }
};

void checks(const char* expected) {
    const Header vanilla("G4NJDA");     // the retail disc the build was recompiled from
    const Header hacked("SG4JDA");      // the same disc as a hack renames it
    const Header other_game("G3NJDA");  // a different game's disc

    check(boot_check_disc(vanilla.bytes, nullptr, expected) == DiscCheck::Matched,
          "the disc the build was recompiled from is accepted");
    check(boot_check_disc(other_game.bytes, nullptr, expected) == DiscCheck::WrongDisc,
          "another game's disc is refused");

    // The regression: with a patch on, the ID the game reads is the patch's, and the disc
    // this build was made for is the tree the patch decodes from.
    check(boot_check_disc(hacked.bytes, vanilla.bytes, expected) == DiscCheck::Matched,
          "a patch that renames the disc it carries is played");
    check(boot_check_disc(hacked.bytes, other_game.bytes, expected) == DiscCheck::WrongTree,
          "a patch over another game's tree is refused");
    check(boot_check_disc(vanilla.bytes, other_game.bytes, expected) == DiscCheck::WrongTree,
          "the tree decides, whatever the patched disc says");

    // No tree to ask -- an image rather than a tree, or a patch whose source could not be
    // read -- falls back to the disc itself, so the check can only ever refuse, never wave
    // another game through.
    check(boot_check_disc(other_game.bytes, nullptr, expected) == DiscCheck::WrongDisc,
          "with no tree the disc itself is checked");
}

int run() {
    checks("G4NJDA");
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
